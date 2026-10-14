"""Native unknown-battery UI regression; never opens a physical device."""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import time
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from check_home import snapshot_backend


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--measurement", choices=["1", "2"], help="test-only capture/analysis view")
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    manifest = snapshot_backend(args.build_dir.resolve(), out)
    sys.path.insert(0, str(Path(manifest["component_root"]) / "tools/sim_bridge"))
    from rpc_client import connect
    with socket.socket() as sock, socket.socket() as backend_socket:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
        backend_socket.bind(("127.0.0.1", 0))
        backend_port = backend_socket.getsockname()[1]
    ready = out / "ready.json"
    processes = []
    with (out / "host.log").open("w") as host_log, (out / "backend.log").open("w") as backend_log:
        def until(predicate, label):
            deadline = time.monotonic() + 6
            while time.monotonic() < deadline:
                if predicate():
                    return
                if any(p.poll() is not None for p in processes):
                    raise AssertionError(label + ": process exited; " + (out / "backend.log").read_text())
                time.sleep(.02)
            raise AssertionError(label)
        try:
            host = subprocess.Popen([
                str(ROOT.parent / ".toolchains/gsp-sim-1.5.1/sim"),
                "--bundle", manifest["bundle"], "--api-json", manifest["api"],
                "--backend-listen", f"tcp://127.0.0.1:{backend_port}", "--backend-required", "--api-listen", f"tcp://127.0.0.1:{port}",
                "--headless", "--fps", "1000", "--frames", "0", "--ready-file", str(ready),
            ], stdout=host_log, stderr=subprocess.STDOUT)
            processes.append(host)
            until(ready.exists, "host readiness")
            state = json.loads(ready.read_text())
            assert state["pid"] == host.pid
            env = dict(os.environ)
            if args.measurement:
                env["ECHOY_TEST_MEASUREMENT"] = args.measurement
            backend = subprocess.Popen([manifest["executable"], "--endpoint", state["backend"],
                                        "--duration-ms", "14000"], env=env, stdout=backend_log, stderr=subprocess.STDOUT)
            processes.append(backend)
            until(lambda: "HEALTH_FIXTURE init=0" in (out / "backend.log").read_text(),
                  "unknown-battery UI must initialize successfully")
            objects = {o["name"]: o for o in json.loads(Path(manifest["api"]).read_text())["scenes"][0]["objects"]}
            for name in ["battery_fill", *[x + "_battery_fill" for x in ["role", "settings", "display", "guide", "info"]]]:
                assert any(p["key"] == "visible" for p in objects[name]["properties"]), name
            with connect(port=port) as api:
                def call(method, params=None):
                    response = api.call(method, params)
                    assert "error" not in response, response
                    return response.get("result")
                def settle():
                    time.sleep(.12)
                    call("wait", {"frames": 5})
                def tap(name):
                    call("tap_object", {"name": name})
                    settle()
                def pad():
                    call("feed_pointer", {"x": 240, "y": 190, "pressed": True})
                    call("wait", {"frames": 2})
                    call("feed_pointer", {"x": 240, "y": 190, "pressed": False})
                    settle()
                def shot(name):
                    settle()
                    path = out / (name + ".png")
                    call("screenshot", {"path": str(path), "format": "png"})
                    image = Image.open(path).convert("RGB")
                    assert image.size == (480, 480)
                    return image
                if args.measurement:
                    until(lambda: "mode=pause record=0 play=0 single=1" in (out / "backend.log").read_text(),
                          "measurement must expose the existing full Pause button")
                    shot("measurement-active")
                    pad()
                    tap("role")
                    tap("settings")
                    call("feed_pointer", {"x": 48, "y": 190, "pressed": True})
                    call("feed_pointer", {"x": 48, "y": 250, "pressed": True})
                    call("feed_pointer", {"x": 48, "y": 250, "pressed": False})
                    settle()
                    log = (out / "backend.log").read_text()
                    assert "MEASUREMENT_FIXTURE action=" not in log, log
                    assert "ECHOY_ROUTE route=role_page" not in log
                    assert "ECHOY_ROUTE route=settings_page" not in log
                    shot("measurement-controls-blocked")
                    tap("strip_action")
                    until(lambda: "MEASUREMENT_FIXTURE cancelled" in (out / "backend.log").read_text(),
                          "native Pause must reach the controller")
                    until(lambda: "mode=record_only record=1 play=0 single=0" in (out / "backend.log").read_text(),
                          "cancelled measurement must restore the normal empty strip")
                    shot("measurement-cancelled")
                    assert backend.wait(timeout=16) == 0
                    call("quit")
                    assert host.wait(timeout=6) == 0
                    log = (out / "backend.log").read_text()
                    assert "ECHOY_UI_ERROR" not in log and "last_error=0" in log and "cleanup=0" in log
                    assert log.count("MEASUREMENT_FIXTURE action=") == 1
                    result = {"passed": ["visible measurement caption and full-size Pause",
                        "pad/volume/role/settings cannot disturb the fixed test", "Pause reaches controller and restores normal strip"],
                        "scope": "official GSP and production C UI; fixture state, no microphone or physical I/O"}
                    (out / "checks.json").write_text(json.dumps(result, indent=2) + "\n")
                    print(json.dumps(result, indent=2))
                    return
                unknown = shot("unknown-home")
                assert unknown.getpixel((405, 42)) == unknown.getpixel((395, 42))
                pad()
                until(lambda: "unknown=0 battery=37" in (out / "backend.log").read_text(), "known reading")
                known = shot("known-home")
                assert known.getpixel((405, 42)) != known.getpixel((395, 42))
                assert ImageChops.difference(known.crop((428, 31, 451, 53)), unknown.crop((428, 31, 451, 53))).getbbox()
                pad()
                until(lambda: "unknown=1 battery=0" in (out / "backend.log").read_text(), "lost reading")
                again = shot("unknown-again")
                assert ImageChops.difference(unknown.crop((399, 31, 451, 53)), again.crop((399, 31, 451, 53))).getbbox() is None
                tap("role")
                shot("unknown-role")
                tap("role_back")
                tap("settings")
                shot("unknown-settings")
                tap("settings_display")
                display = shot("unknown-display")
                call("feed_pointer", {"x": 240, "y": 193, "pressed": True})
                call("wait", {"frames": 2})
                call("feed_pointer", {"x": 240, "y": 193, "pressed": False})
                settle()
                until(lambda: "HEALTH_FIXTURE brightness=60" in (out / "backend.log").read_text(), "physical brightness adapter")
                # Hardware calls the panel adapter; it must not also shade its framebuffer.
                assert shot("display-panel-adapter").getpixel((240, 90)) == display.getpixel((240, 90))
                tap("display_back")
                tap("settings_guide")
                shot("unknown-guide")
                tap("guide_back")
                call("tap_object", {"name": "settings_info"})
                settle()
                shot("unknown-info")
                assert "ECHOY_ROUTE route=info_page" in (out / "backend.log").read_text()
                assert backend.wait(timeout=16) == 0
                call("quit")
                assert host.wait(timeout=6) == 0
            log = (out / "backend.log").read_text()
            assert "ECHOY_UI_ERROR" not in log
            assert "last_error=0" in log and "cleanup=0" in log
            result = {"passed": ["unknown battery initializes real C UI", "all six fill visibility bindings exist",
                                 "unknown to 37 percent to unknown updates text and fill", "all six pages remain usable; brightness reaches panel adapter without framebuffer shading"],
                      "scope": "official GSP, production C UI, fixture health readings; no physical audio or battery"}
            (out / "checks.json").write_text(json.dumps(result, indent=2) + "\n")
            print(json.dumps(result, indent=2))
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=6)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()


if __name__ == "__main__":
    main()
