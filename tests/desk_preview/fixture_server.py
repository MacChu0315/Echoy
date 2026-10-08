#!/usr/bin/env python3
"""Protocol fixture for browser shell tests, never an audio/device implementation.

Pass --visual-url for an already-running isolated official simulator. Only the
outer desk receives browser input in browser_check.mjs; the iframe is inspected.
"""
import argparse
import json
from pathlib import Path
import socket
import sys
import tempfile
import threading

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from desk_preview import DeskControls, make_server

parser = argparse.ArgumentParser()
parser.add_argument("--visual-url", required=True)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix="echoy-desk-browser-fixture-") as temp:
    path = Path(temp)
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    sock.bind(str(path / "command.sock")); (path / "command.sock").chmod(0o600)
    state = {"ready": True, "audio_connected": True, "export_counter": 0, "export_available": False,
             "tiles": {tile: {"present": tile == "A", "role": role} for tile, role in zip("ABC", ["pulse", "voice", "harmony"])}, "edges": []}

    def save():
        file = path / "status.tmp"
        file.write_text(json.dumps(state)); file.chmod(0o600); file.replace(path / "status.json")

    def receive():
        while True:
            words = sock.recv(1024).decode().split()
            if words[0] == "present":
                state["tiles"][words[1]]["present"] = words[2] == "1"
            if words[0] == "edges":
                state["edges"] = [{"left": a, "right": b} for a, b in zip(words[1::2], words[2::2])]
            if words[0] == "condition":
                state["tiles"][words[1]]["condition"] = words[2]
            save()

    save()
    threading.Thread(target=receive, daemon=True).start()
    controls = DeskControls(path)
    server = make_server(controls, 0, [args.visual_url] * 3)
    print(f"http://127.0.0.1:{server.server_port}", flush=True)
    try:
        server.serve_forever()
    finally:
        server.server_close(); controls.close(); sock.close()
