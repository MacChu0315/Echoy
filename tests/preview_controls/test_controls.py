"""Run the actual PC adapter and HTTP server against private temporary files."""
import http.client
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PreviewControls(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="echoy-controls-build-")
        cls.binary = Path(cls.build.name) / "platform_runner"
        sanitizer = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if os.environ.get("ECHOY_PC_SANITIZE") == "1" else []
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g", *sanitizer,
                        "-I", str(ROOT / "tests/preview_controls"),
                        "-I", str(ROOT / "projects/echoy/main"),
                        str(ROOT / "tests/preview_controls/runner.c"),
                        str(ROOT / "projects/echoy/pc/platform_pc.c"),
                        str(ROOT / "projects/echoy/main/echoy_music.c"), "-lm", "-o", str(cls.binary)], check=True)
        cls.music_binary = Path(cls.build.name) / "music_runner"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", *sanitizer,
                        "-I", str(ROOT / "projects/echoy/main"),
                        str(ROOT / "tests/music/runner.c"),
                        str(ROOT / "projects/echoy/main/echoy_music.c"),
                        "-lm", "-o", str(cls.music_binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="echoy-controls-")
        self.directory = Path(self.temp.name)
        self.processes = []

    def tearDown(self):
        for process in reversed(self.processes):
            if process.poll() is None:
                process.terminate()
            process.communicate(timeout=5)
        self.temp.cleanup()

    def backend(self, **values):
        env = dict(os.environ)
        for key in ["ECHOY_SIM_CONTROL_DIR", "ECHOY_SIM_SEED", "ECHOY_SIM_CONDITION"]:
            env.pop(key, None)
        env.update(values)
        process = subprocess.Popen([str(self.binary)], env=env, stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.processes.append(process)
        initial = json.loads(process.stdout.readline())
        return process, initial

    def command(self, process, command):
        process.stdin.write(command + "\n")
        process.stdin.flush()
        return json.loads(process.stdout.readline())

    def server(self):
        process = subprocess.Popen(["python3", "-u", str(ROOT / "tools/preview_controls.py"),
                                    "--control-dir", str(self.directory), "--port", "0"],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.processes.append(process)
        ready = process.stdout.readline().strip()
        self.assertTrue(ready.startswith("ECHOY_CONTROLS_READY http://127.0.0.1:"), ready)
        return int(ready.rsplit(":", 1)[1])

    def request(self, port, method, path, body=None, headers=None):
        connection = http.client.HTTPConnection("127.0.0.1", port, timeout=3)
        connection.request(method, path, body=body, headers=headers or {})
        response = connection.getresponse()
        data = response.read()
        result = response.status, dict(response.getheaders()), data
        connection.close()
        return result

    def test_default_platform_and_environment(self):
        process, initial = self.backend()
        self.assertEqual(initial, {"simulator": True, "battery": 72, "seed": 0, "condition": 0, "time": 125000})
        self.assertEqual(self.command(process, "poll"), 0)
        self.assertFalse(self.command(process, "export"))
        for condition, value in [("normal", 0), ("fault", 1), ("loading", 2), ("empty", 3)]:
            _, initial = self.backend(ECHOY_SIM_SEED="0x1085", ECHOY_SIM_CONDITION=condition)
            self.assertEqual(initial["seed"], 0x1085)
            self.assertEqual(initial["condition"], value)
        for seed in ["10000", "-1", "12oops", ""]:
            _, initial = self.backend(ECHOY_SIM_SEED=seed, ECHOY_SIM_CONDITION="unknown")
            self.assertEqual(initial["seed"], 0)
            self.assertEqual(initial["condition"], 0)

    def test_socket_commands_status_export_and_cleanup_retry(self):
        process, _ = self.backend(ECHOY_SIM_CONTROL_DIR=str(self.directory))
        status_path = self.directory / "status.json"
        status = json.loads(status_path.read_text())
        self.assertEqual(status, {"removed": False, "export_counter": 0, "export_available": False})
        before = status_path.stat().st_mtime_ns
        self.command(process, "restore")
        self.assertEqual(status_path.stat().st_mtime_ns, before, "Unchanged removal state must not rewrite disk")
        with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as sender:
            for name, expected in [("restore", 1), ("fault", 2), ("loading", 3), ("recover", 4), ("empty", 5), ("nonsense", 0), ("fault\x00restore", 0), ("fault" * 100, 0)]:
                sender.sendto(name.encode(), str(self.directory / "command.sock"))
                self.assertEqual(self.command(process, "poll"), expected)
                self.assertEqual(self.command(process, "poll"), 0, "Poll must not block on an empty socket")
        self.command(process, "remove")
        self.assertTrue(json.loads(status_path.read_text())["removed"])
        self.assertTrue(self.command(process, "export"))
        exported = json.loads((self.directory / "session.json").read_text())
        self.assertTrue(exported["simulated"])
        self.assertEqual(list(exported["tiles"]), ["A"])
        tile = exported["tiles"]["A"]
        self.assertFalse(tile["loop"])
        self.assertEqual(tile["current"]["role"], "harmony")
        self.assertEqual(tile["current"]["phrase"], 3)
        self.assertEqual(tile["current"]["density"], .9)
        self.assertEqual([n["step"] for n in tile["ownSeed"]], [0, 2, 7, 12])
        self.assertGreater(len(tile["current"]["full"]), len(tile["ownSeed"]))
        self.assertEqual(len(tile["current"]["foreground"]), 4)
        self.assertTrue(any(n.get("notes") == [50, 57, 65] for n in tile["current"]["full"]))
        for n in tile["current"]["full"]:
            self.assertTrue({"step", "kind", "vel", "dur", "sourceStep", "layer", "expression", "origin"} <= n.keys())
        self.assertIn("audio synthesis not implemented", exported["limitations"])
        self.assertIn("multi-device transport not implemented", exported["limitations"])
        self.assertEqual(json.loads(status_path.read_text())["export_counter"], 1)
        preserved = (self.directory / "session.json").read_bytes()
        for invalid in ["bad-kind", "bad-pitches", "bad-nan"]:
            self.assertFalse(self.command(process, "export-" + invalid))
            self.assertEqual((self.directory / "session.json").read_bytes(), preserved)
            self.assertEqual(json.loads(status_path.read_text())["export_counter"], 1)
        self.command(process, "fail-delete")
        self.assertTrue((self.directory / "command.sock").exists(), "Failed UI flush must retain the platform context")
        self.command(process, "delete")
        self.assertFalse((self.directory / "command.sock").exists())
        process.wait(timeout=3)
        logs = process.stderr.read()
        self.assertIn("ECHOY_READY", logs)
        self.assertIn("last_error=0", logs)
        self.assertIn("cleanup_error=-1", logs)
        self.assertIn("cleanup_error=0", logs)

    def test_every_exported_event_matches_actual_plan_fields(self):
        process, _ = self.backend(ECHOY_SIM_CONTROL_DIR=str(self.directory))
        role_names = ["pulse", "voice", "dust", "harmony"]
        kinds = ["kick", "rim", "hat", "tone", "grain", "chord"]
        layers = ["anchor", "echo", "ending", "support", "detail"]
        articulations = [None, "short", "broken", "answer"]
        for role in range(4):
            for phrase in range(1, 4):
                with self.subTest(role=role, phrase=phrase):
                    self.assertTrue(self.command(process, f"export {role} {phrase} 0.9 1085"))
                    plan = json.loads((self.directory / "session.json").read_text())["tiles"]["A"]["current"]
                    # The music runner serializes the C plan directly; it is
                    # independently checked against the frozen JS by music/oracle.mjs.
                    direct = subprocess.run([str(self.music_binary)], input=f"{role} {phrase} 0.9 1 4229\n",
                                            text=True, capture_output=True, check=True)
                    expected = json.loads(direct.stdout)
                    self.assertEqual(plan["role"], role_names[role])
                    self.assertEqual(plan["duration"], expected["duration"])
                    self.assertEqual(len(plan["full"]), expected["count"])
                    self.assertEqual(len(plan["source"]), expected["source_count"])
                    for n, e in zip(plan["source"], expected["source"]):
                        self.assertEqual(n["step"], e["step"])
                        self.assertEqual(n["kind"], kinds[e["kind"]])
                        self.assertEqual(n["vel"], e["velocity"])
                    for n, e in zip(plan["full"], expected["events"]):
                        self.assertEqual(n["step"], e["step"])
                        self.assertEqual(n["vel"], e["velocity"])
                        self.assertEqual(n["dur"], e["duration"])
                        self.assertEqual(n["kind"], kinds[e["kind"]])
                        self.assertEqual(n["layer"], layers[e["layer"]])
                        self.assertEqual(n["origin"], "you" if e["layer"] == 0 else "partner")
                        self.assertEqual(n["expression"], e["expression"])
                        self.assertEqual(n["sourceStep"], e["source_step"])
                        self.assertEqual(n.get("note", 0), e["note"])
                        self.assertEqual(n.get("notes", []), e["notes"])
                        self.assertEqual(n.get("harmony", []), e["harmony"])
                        self.assertEqual(n.get("maxDuration", 0), e["max_duration"])
                        self.assertEqual(n.get("articulation"), articulations[e["articulation"]])
                    self.assertEqual(plan["foreground"], [n for n in plan["full"] if n["layer"] == "anchor"])

    def test_control_directory_is_private_and_existing_socket_is_not_replaced(self):
        self.directory.chmod(0o755)
        process, _ = self.backend(ECHOY_SIM_CONTROL_DIR=str(self.directory))
        self.assertEqual(self.command(process, "poll"), 0)
        self.assertFalse((self.directory / "command.sock").exists())
        self.directory.chmod(0o700)
        with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as existing:
            existing.bind(str(self.directory / "command.sock"))
            inode = (self.directory / "command.sock").stat().st_ino
            process, _ = self.backend(ECHOY_SIM_CONTROL_DIR=str(self.directory))
            self.command(process, "delete")
            self.assertEqual((self.directory / "command.sock").stat().st_ino, inode)

    def test_directory_symlink_is_not_followed(self):
        actual = self.directory / "actual"
        actual.mkdir(mode=0o700)
        alias = self.directory / "alias"
        alias.symlink_to(actual, target_is_directory=True)
        process, _ = self.backend(ECHOY_SIM_CONTROL_DIR=str(alias))
        self.assertEqual(self.command(process, "poll"), 0)
        self.assertEqual(list(actual.iterdir()), [])
        rejected = subprocess.run(["python3", str(ROOT / "tools/preview_controls.py"),
                                   "--control-dir", str(alias), "--port", "0"], capture_output=True, timeout=3)
        self.assertNotEqual(rejected.returncode, 0)

    def test_http_fixed_paths_same_origin_and_real_export(self):
        backend, _ = self.backend(ECHOY_SIM_CONTROL_DIR=str(self.directory))
        port = self.server()
        origin = f"http://127.0.0.1:{port}"
        code, headers, page = self.request(port, "GET", "/")
        self.assertEqual(code, 200)
        self.assertIn(b'http://127.0.0.1:3222/', page)
        self.assertNotIn("Access-Control-Allow-Origin", headers)
        self.assertEqual(self.request(port, "GET", "/status")[0], 200)
        for path in ["/../status.json", "/%2e%2e/session.json", "/etc/passwd", "/status?file=/etc/passwd"]:
            self.assertEqual(self.request(port, "GET", path)[0], 404)
        body = json.dumps({"action": "fault"})
        for bad_origin in ["https://example.com", "null", "http://localhost:18895"]:
            self.assertEqual(self.request(port, "POST", "/command", body, {"Origin": bad_origin, "Content-Type": "application/json"})[0], 403)
        self.assertEqual(self.request(port, "POST", "/command", body)[0], 403)
        self.assertEqual(self.request(port, "GET", "/status", headers={"Host": "evil.example"})[0], 403)
        allowed = {"Origin": origin, "Content-Type": "application/json"}
        self.assertEqual(self.request(port, "POST", "/command", body, allowed)[0], 202)
        self.assertEqual(self.command(backend, "poll"), 2)
        for bad in [{"action": "remove"}, {"action": "fault", "path": "/tmp/anything"}, [], "fault"]:
            self.assertEqual(self.request(port, "POST", "/command", json.dumps(bad), allowed)[0], 400)
        self.assertEqual(self.request(port, "POST", "/command", "x" * 129, allowed)[0], 413)
        self.assertEqual(self.request(port, "GET", "/session.json")[0], 404)
        self.command(backend, "export")
        code, headers, data = self.request(port, "GET", "/session.json")
        self.assertEqual(code, 200)
        self.assertIn("attachment", headers["Content-Disposition"])
        self.assertTrue(json.loads(data)["simulated"])
        self.command(backend, "remove")
        status = json.loads(self.request(port, "GET", "/status")[2])
        self.assertTrue(status["removed"])
        self.assertTrue(status["connected"])
        self.assertEqual(status["export_counter"], 1)
        session = self.directory / "session.json"
        session.unlink()
        session.symlink_to("/etc/passwd")
        self.assertEqual(self.request(port, "GET", "/session.json")[0], 404)
        session.unlink()
        session.write_bytes(b"x" * (256 * 1024 + 1))
        session.chmod(0o600)
        self.assertEqual(self.request(port, "GET", "/session.json")[0], 404)


if __name__ == "__main__":
    unittest.main(verbosity=2)
