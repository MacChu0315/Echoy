"""Exercise the exporter's external CLI boundary without a real device."""
import base64
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zlib

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("capture", ROOT / "tools/capture_audio_snapshot.py")
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)


class CaptureTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.output = Path(self.directory.name) / "capture"
        self.device = "4553502d49524953010030eda0f40c6c"
        self.calls = []
        self.changed_boot = False
        self.corrupt = False
        self.fail_chunk = False
        self.chunk_reads = 0
        self.plan = b"ECHP" + struct.pack("<HHI", 1, 32, 8192) + bytes(8180)
        self.metadata = dict(schema=1, token="0011223344556677", size=8192,
                             crc32=f"{zlib.crc32(self.plan):08x}", volume=.34234234234234234,
                             chunk_max=512, phase=5, plan_pending=False)

    def tearDown(self):
        self.directory.cleanup()

    def run_cli(self, command, **kwargs):
        self.calls.append(command)
        self.assertIn("mosaico.py", str(command[1]))
        self.assertIn("--json", command)
        self.assertIn(self.device, command)
        action = command[command.index("iris") + 1]
        if action == "device-status":
            data = dict(ok=True, device_id=self.device,
                        device=dict(device_id=self.device, stale=False,
                                    boot_id=8 if self.changed_boot and self.chunk_reads else 7))
        else:
            self.assertEqual(action, "rpc")
            self.assertEqual(command[command.index("rpc") + 1], "0xeca1")
            method = command[command.index("rpc") + 2]
            if method == "1":
                self.assertNotIn("--payload-hex", command)
                payload = json.dumps(self.metadata).encode()
            else:
                self.assertEqual(method, "2")
                self.chunk_reads += 1
                if self.fail_chunk:
                    raise subprocess.CalledProcessError(1, command, stderr="stale snapshot token")
                token, offset, length = struct.unpack("<QIH", bytes.fromhex(command[command.index("--payload-hex") + 1]))
                self.assertEqual(token, int(self.metadata["token"], 16))
                self.assertGreater(length, 0)
                self.assertLessEqual(length, 512)
                payload = self.plan[offset:offset + length]
                if self.corrupt and offset == 0: payload = b"!" + payload[1:]
            data = dict(ok=True, device_id=self.device, response=dict(
                operation=dict(status="succeeded", device_id=self.device, operation_id="op"),
                payload_base64=base64.b64encode(payload).decode(), response_bytes=len(payload)))
        return subprocess.CompletedProcess(command, 0, json.dumps(data), "")

    def perform(self):
        with patch.object(capture.subprocess, "run", side_effect=self.run_cli):
            return capture.capture_snapshot(Path("mosaico.py"), ROOT / "projects/echoy",
                                            self.device, self.output)

    def test_8192_bytes_exact_volume_and_only_official_read_commands(self):
        result = self.perform()
        self.assertEqual((self.output / "plan.echp").read_bytes(), self.plan)
        self.assertEqual(result["metadata"]["volume"], .34234234234234234)
        self.assertEqual(result["boot_id"], 7)
        self.assertEqual(self.chunk_reads, 16)

    def test_corruption_does_not_publish_a_capture(self):
        self.corrupt = True
        with self.assertRaisesRegex(ValueError, "CRC"):
            self.perform()
        self.assertFalse(self.output.exists())

    def test_boot_change_does_not_publish_a_capture(self):
        self.changed_boot = True
        with self.assertRaisesRegex(ValueError, "boot"):
            self.perform()
        self.assertFalse(self.output.exists())

    def test_stale_rpc_does_not_retry_or_publish_partial_plan(self):
        self.fail_chunk = True
        with self.assertRaises(subprocess.CalledProcessError):
            self.perform()
        self.assertEqual(self.chunk_reads, 1)
        self.assertFalse(self.output.exists())

    def test_existing_output_is_preserved_without_device_calls(self):
        self.output.mkdir()
        (self.output / "plan.echp").write_bytes(b"previous")
        with self.assertRaises(FileExistsError):
            self.perform()
        self.assertEqual(self.calls, [])
        self.assertEqual((self.output / "plan.echp").read_bytes(), b"previous")


if __name__ == "__main__":
    unittest.main()
