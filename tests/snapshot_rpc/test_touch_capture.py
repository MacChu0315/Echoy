import base64
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("touch_capture", ROOT / "tools/capture_touch_trace.py")
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)


class CaptureTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.output = Path(self.directory.name) / "capture"
        self.device = "4553502d49524953010030eda0f40c6c"
        self.calls = []
        self.boot_changed = False
        self.reads = 0
        self.trace = dict(schema="echoy-touch-v1", available=True, state=3, summary=[0] * 24,
                          pressed_retained=0, pressed_omitted=0,
                          events_not_returned=0, irqs_not_returned=0, edges=[])

    def tearDown(self):
        self.directory.cleanup()

    def run_cli(self, command, **kwargs):
        self.calls.append(command)
        self.assertIn("mosaico.py", str(command[1]))
        self.assertIn("--json", command)
        self.assertIn(self.device, command)
        action = command[command.index("iris") + 1]
        if action == "device-status":
            document = dict(ok=True, device_id=self.device, device=dict(
                device_id=self.device, stale=False, boot_id=8 if self.boot_changed and self.reads else 7))
        else:
            self.assertEqual(command[command.index("iris") + 1:command.index("iris") + 4], ["rpc", "0xeca1", "3"])
            self.assertNotIn("--payload", command)
            self.assertNotIn("--payload-hex", command)
            self.reads += 1
            data = json.dumps(self.trace).encode()
            document = dict(ok=True, device_id=self.device, response=dict(
                operation=dict(status="succeeded", device_id=self.device, operation_id="trace-op"),
                response_bytes=len(data), payload_base64=base64.b64encode(data).decode()))
        return subprocess.CompletedProcess(command, 0, json.dumps(document), "")

    def perform(self):
        with patch.object(capture.subprocess, "run", side_effect=self.run_cli):
            return capture.capture_trace(Path("mosaico.py"), ROOT / "projects/echoy", self.device, self.output)

    def test_only_single_method3_and_two_identity_reads(self):
        result = self.perform()
        self.assertEqual(self.reads, 1)
        self.assertEqual(len(self.calls), 3)
        self.assertEqual(result["boot_id"], 7)
        self.assertEqual(result["state_name"], "frozen")
        self.assertEqual(result["summary"]["busy_drops"], 0)
        self.assertEqual(json.loads((self.output / "touch.json").read_text()), result)

    def test_rows_and_omissions_are_decoded_without_losing_integer_time(self):
        self.trace["summary"][8] = 20
        self.trace["summary"][14:16] = [7, 3]
        self.trace["summary"][16] = 10
        self.trace["summary"][18] = 64
        self.trace.update(pressed_retained=5, pressed_omitted=19, events_not_returned=9, irqs_not_returned=64,
                          edges=[[9007199254740999, 2, 3, 4, 5, 6, 7, 8, 2, 9, 0x4001, 1, 0, 1]])
        result = self.perform()
        self.assertEqual(result["edges"][0]["at_us"], 9007199254740999)
        self.assertEqual(result["edges"][0]["pending_seed"], 0x4001)
        self.assertEqual(result["summary"]["event_overwritten"], 7)
        self.assertEqual(result["omissions"]["pressed_omitted"], 19)

    def test_unavailable_is_explicit_and_does_not_publish(self):
        self.trace = dict(schema="echoy-touch-v1", available=False, reason="inactive_or_busy")
        with self.assertRaisesRegex(ValueError, "unavailable.*inactive_or_busy"):
            self.perform()
        self.assertFalse(self.output.exists())

    def test_boot_change_is_rejected(self):
        self.boot_changed = True
        with self.assertRaisesRegex(ValueError, "boot"):
            self.perform()
        self.assertFalse(self.output.exists())

    def test_missing_summary_fields_cannot_look_complete(self):
        self.trace["summary"] = [0] * 23
        with self.assertRaises(ValueError):
            self.perform()
        self.assertFalse(self.output.exists())

    def test_existing_output_is_preserved_without_device_calls(self):
        self.output.mkdir()
        with self.assertRaises(FileExistsError):
            self.perform()
        self.assertEqual(self.calls, [])


if __name__ == "__main__":
    unittest.main()
