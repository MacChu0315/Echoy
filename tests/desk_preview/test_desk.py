"""Run with python3 -m unittest discover -s tests/desk_preview -v."""
import http.client
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import desk_preview as desk


class Commands(unittest.TestCase):
    def test_exact_native_protocol(self):
        cases = [({"action": "present", "tile": "B", "present": True}, "present B 1"),
                 ({"action": "edges", "edges": [{"left": "A", "right": "B"}, {"left": "B", "right": "C"}]}, "edges A B B C"),
                 ({"action": "edges", "edges": []}, "edges"),
                 ({"action": "condition", "tile": "C", "condition": "empty"}, "condition C empty"),
                 ({"action": "volume", "tile": "A", "volume": .65}, "volume A 0.65"),
                 ({"action": "example", "tile": "A"}, "example A"),
                 ({"action": "pause_all"}, "pause_all"), ({"action": "export"}, "export")]
        for value, expected in cases:
            self.assertEqual(desk.native_command(value), expected)

    def test_reject_injection_invalid_graph_and_extra_fields(self):
        invalid = [{"action": "example", "tile": "A\npause_all"},
                   {"action": "present", "tile": "A", "present": 1},
                   {"action": "volume", "tile": "A", "volume": float("nan")},
                   {"action": "volume", "tile": "A", "volume": True},
                   {"action": "export", "path": "/tmp/target"},
                   {"action": "edges", "edges": [{"left": "A", "right": "A"}]},
                   {"action": "edges", "edges": [{"left": "A", "right": "B"}, {"left": "B", "right": "A"}]},
                   {"action": "edges", "edges": [{"left": "A", "right": "B"}, {"left": "A", "right": "C"}]}]
        for value in invalid:
            with self.subTest(value=value), self.assertRaises(ValueError):
                desk.native_command(value)


class HTTP(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name)
        self.path.chmod(0o700)
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        self.sock.bind(str(self.path / "command.sock"))
        (self.path / "command.sock").chmod(0o600)
        self.sock.settimeout(1)
        self.saved = {"ready": True, "audio_connected": True, "export_available": False,
                      "export_counter": 0, "tiles": {"A": {"present": True}}, "edges": []}
        self.write("status.json", self.saved)
        self.controls = desk.DeskControls(self.path)
        self.server = desk.make_server(self.controls, 0, ["http://127.0.0.1:3301/", "http://127.0.0.1:3302/", "http://127.0.0.1:3303/"])
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.port = self.server.server_port

    def tearDown(self):
        self.server.shutdown(); self.thread.join(); self.server.server_close()
        self.controls.close(); self.sock.close(); self.temp.cleanup()

    def write(self, name, data):
        file = self.path / name
        file.write_text(json.dumps(data)); file.chmod(0o600)

    def request(self, method, path, body=None, headers=None):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=2)
        conn.request(method, path, body, headers or {})
        response = conn.getresponse()
        result = response.status, dict(response.getheaders()), response.read()
        conn.close()
        return result

    def test_page_status_and_dynamic_frame_policy(self):
        code, headers, body = self.request("GET", "/")
        self.assertEqual(code, 200)
        self.assertIn(b'http://127.0.0.1:3303/', body)
        self.assertIn("http://127.0.0.1:3301", headers["Content-Security-Policy"])
        status = json.loads(self.request("GET", "/status")[2])
        self.assertTrue(status["connected"] and status["audio_connected"])
        self.assertEqual(status["tiles"], self.saved["tiles"])

    def test_status_identity_is_fresh_and_symlinks_are_not_followed(self):
        _, generation = self.controls.status_snapshot()
        self.assertFalse(self.controls.status(generation)["ready"], "An old file is not fresh merely because the socket is connected")
        status = self.path / "status.json"
        status.unlink()
        target = self.path / "other.json"
        target.write_text(json.dumps(self.saved)); target.chmod(0o600)
        status.symlink_to(target)
        self.assertFalse(self.controls.status()["ready"])
        with self.assertRaises(OSError):
            self.controls.status_snapshot()
        self.assertEqual(json.loads(target.read_text()), self.saved)

    def test_protected_commands_and_fixed_download(self):
        body = json.dumps({"action": "example", "tile": "B"})
        origin = f"http://127.0.0.1:{self.port}"
        headers = {"Origin": origin, "Content-Type": "application/json"}
        self.assertEqual(self.request("POST", "/command", body)[0], 403)
        self.assertEqual(self.request("POST", "/command", body, {**headers, "Origin": "null"})[0], 403)
        self.assertEqual(self.request("GET", "/status", headers={"Host": "evil.example"})[0], 403)
        self.assertEqual(self.request("POST", "/command", body, headers)[0], 202)
        self.assertEqual(self.sock.recv(256), b"example B")
        self.assertEqual(self.request("POST", "/command", "x" * 1025, headers)[0], 413)
        self.assertEqual(self.request("GET", "/../status.json")[0], 404)
        self.assertEqual(self.request("GET", "/session.json")[0], 404)
        self.saved["export_available"] = True; self.write("status.json", self.saved)
        self.write("session.json", {"simulated": True, "tiles": {"A": {}}})
        self.assertEqual(self.request("GET", "/session.json")[0], 200)
        (self.path / "session.json").unlink(); (self.path / "session.json").symlink_to("/etc/passwd")
        self.assertEqual(self.request("GET", "/session.json")[0], 404)


class Geometry(unittest.TestCase):
    def test_contact_and_boot_timing_and_stale_cancellation(self):
        subprocess.run(["node", str(ROOT / "tests/desk_preview/timing_check.mjs")],
                       input=json.dumps({"script": desk.spatial_script() + desk.SCRIPT}),
                       text=True, check=True)

    def test_frozen_spatial_rules(self):
        script = desk.spatial_script()
        js = script + """
const s=new DuijuSpatial(),p={A:{x:32,y:22},B:{x:548,y:22},C:{x:1064,y:22}};
if(s.edges(p).length!==2)throw Error('chain');
const snap=s.snap('B',{x:561,y:35},{A:p.A});
if(!snap.snapped||snap.x!==548||snap.y!==22)throw Error('snap');
if(!s.collision('B',{x:100,y:22},{A:p.A}))throw Error('collision');
if(s.edges({A:p.A,B:{x:548,y:50}}).length)throw Error('vertical');
"""
        subprocess.run(["node", "-e", js], check=True)


class Launcher(unittest.TestCase):
    def test_old_ready_is_ignored_until_new_backend_publishes_ready(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp); control = temp / "control"; control.mkdir(mode=0o700)
            old_status = control / "status.json"
            old_status.write_text(json.dumps({"ready": True, "tiles": {}, "audio_connected": True}))
            old_status.chmod(0o600)
            exported = control / "session.json"
            exported.write_text('{"simulated":true,"tiles":{"A":{"preserve":true}}}')
            exported.chmod(0o600)
            preserved = exported.read_bytes()
            sim = temp / "sim"
            sim.write_text("""#!/usr/bin/env python3
import sys,json,os,time
from pathlib import Path
a=sys.argv;p=Path(a[a.index('--ready-file')+1])
p.write_text(json.dumps({'pid':os.getpid(),'backend':'tcp://127.0.0.1:'+str(20000+os.getpid()%30000)}))
time.sleep(30)
""")
            sim.chmod(0o700)
            backend = temp / "backend"
            backend.write_text("""#!/usr/bin/env python3
import sys,json,socket,time
from pathlib import Path
a=sys.argv;p=Path(a[a.index('--control-dir')+1])
s=socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM);s.bind(str(p/'command.sock'));(p/'command.sock').chmod(0o600)
# The old status remains ready even after the new socket has been bound.
time.sleep(.35)
def publish(ready):
 q=p/'new-status.tmp';q.write_text(json.dumps({'ready':ready,'tiles':{}}));q.chmod(0o600);q.replace(p/'status.json')
publish(False);time.sleep(.35)
(p/'published.marker').write_text('new native UI initialized')
publish(True);time.sleep(.5)
""")
            backend.chmod(0o700)
            bundle = temp / "app.gspb"; bundle.write_bytes(b"fixture")
            process = subprocess.Popen([sys.executable, str(ROOT/'tools/desk_preview.py'), '--no-build', '--headless', '--silent',
                                        '--port', '0', '--sim', str(sim), '--backend', str(backend), '--bundle', str(bundle),
                                        '--control-dir', str(control)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                ready = process.stdout.readline()
                self.assertIn('ECHOY_DESK_READY', ready)
                self.assertTrue((control/'published.marker').exists(), 'Launcher used a stale ready status before the new backend initialized')
                process.wait(timeout=10)
                self.assertEqual(process.returncode, 0, process.stderr.read())
                self.assertEqual(exported.read_bytes(), preserved, 'Startup must preserve the exported session')
            finally:
                if process.poll() is None:
                    process.terminate();process.wait(timeout=8)
                process.stdout.close();process.stderr.close()

    def test_defaults_keep_current_build_separate(self):
        args = desk.arguments([])
        self.assertEqual(args.build_dir.name, "build-sim-desk")
        self.assertEqual(args.visual_ports, [3222, 3223, 3224])
        self.assertEqual(args.backend.name, "echoy_desk")

    def test_owned_cleanup_does_not_stop_unrelated_process(self):
        with tempfile.TemporaryDirectory() as temp:
            external = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(30)"])
            owned = desk.OwnedProcesses()
            try:
                child = owned.start([sys.executable, "-c", "import time; time.sleep(30)"], Path(temp) / "child.log")
                owned.close()
                self.assertIsNotNone(child.poll())
                self.assertIsNone(external.poll())
            finally:
                external.terminate(); external.wait()

    def test_stale_readiness_does_not_attach_another_process(self):
        with tempfile.TemporaryDirectory() as temp:
            owned = desk.OwnedProcesses()
            try:
                child = owned.start([sys.executable, "-c", "import time; time.sleep(30)"], Path(temp) / "child.log")
                ready = Path(temp) / "ready.json"
                ready.write_text(json.dumps({"pid": child.pid + 1, "backend": "tcp://127.0.0.1:3300"}))
                with self.assertRaisesRegex(RuntimeError, "another process"):
                    desk.wait_ready(child, ready, .1)
            finally:
                owned.close()

    def test_headless_launch_passes_three_endpoints_and_silent(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp); control = temp / "control"; control.mkdir(mode=0o700)
            sim = temp / "sim"
            sim.write_text("""#!/usr/bin/env python3
import sys, json, os, time
from pathlib import Path
a=sys.argv
p=Path(a[a.index('--ready-file')+1])
p.write_text(json.dumps({'pid':os.getpid(),'backend':'tcp://127.0.0.1:'+str(20000+os.getpid()%30000)}))
time.sleep(30)
""")
            sim.chmod(0o700)
            backend = temp / "backend"
            backend.write_text("""#!/usr/bin/env python3
import sys, json, socket, time
from pathlib import Path
a=sys.argv; p=Path(a[a.index('--control-dir')+1])
(p/'arguments.json').write_text(json.dumps(a))
s=socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM);s.bind(str(p/'command.sock'));(p/'command.sock').chmod(0o600)
(p/'status.json').write_text(json.dumps({'ready':True,'tiles':{}}));(p/'status.json').chmod(0o600)
time.sleep(.5)
""")
            backend.chmod(0o700)
            bundle = temp / "app.gspb"; bundle.write_bytes(b"fixture")
            result = subprocess.run([sys.executable, str(ROOT/'tools/desk_preview.py'), '--no-build', '--headless', '--silent',
                                     '--port', '0', '--sim', str(sim), '--backend', str(backend), '--bundle', str(bundle),
                                     '--control-dir', str(control), '--duration-ms', '500',
                                     '--capture', str(temp/'played samples.wav')], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('ECHOY_DESK_READY', result.stdout)
            argv = json.loads((control/'arguments.json').read_text())
            self.assertIn('--silent', argv)
            for tile in 'ABC':
                self.assertTrue(argv[argv.index('--endpoint-'+tile)+1].startswith('tcp://127.0.0.1:'))
            self.assertEqual(argv[argv.index('--duration-ms')+1], '500')
            self.assertEqual(argv[argv.index('--capture')+1], str(temp/'played samples.wav'))


if __name__ == "__main__":
    unittest.main()
