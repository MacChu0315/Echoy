"""Exercise the official CLI boundary without any microphone or hardware I/O."""
import base64
import fcntl
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("measurement_host", ROOT / "tools/measure_device_output.py")
host = importlib.util.module_from_spec(spec)
spec.loader.exec_module(host)


class HostTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.base = Path(self.directory.name)
        self.ledger = self.base / "consent-ledger.json"
        self.output = self.base / "run"
        self.calls = []
        self.status = dict(schema=1, phase=1, generation=0, requested_generation=0,
                           runs_started=0, runs_remaining=2, frames_written=0,
                           frames_captured=0, elapsed_us=0, input_gain_db=0,
                           error_code=0, result_ready=False)
        self.boot = 123
        self.identity_reads = 0
        self.changed_boot = False
        self.firmware_sha = "ab" * 32
        self.stale = False
        self.identity_id = host.RIGHT_DEVICE_ID
        self.identity_extra = dict(firmware_mode="normal", board_id="esp-mosaico",
                                   layout_id="mosaico-retained-recovery-2m-v1",
                                   queue=dict(device_id=host.RIGHT_DEVICE_ID, running=[], queued=[]))
        self.fail_start = False
        self.expected_start_gain = 0
        self.fail_collect = False
        self.fail_operation = False
        self.truncated = False
        self.metric_reads = 0
        self.total_frames = 252000
        self.first_window = 49800
        self.noise_frames = 14400
        self.wrong_run = False
        self.generation_changed = False
        self.patch_ledger = patch.object(host, "CONSENT_LEDGER", self.ledger)
        self.patch_ledger.start()
        self.patch_cli = patch.object(host.subprocess, "run", side_effect=self.cli) if hasattr(host, "subprocess") else None
        if self.patch_cli:
            self.patch_cli.start()

    def tearDown(self):
        if self.patch_cli:
            self.patch_cli.stop()
        self.patch_ledger.stop()
        self.directory.cleanup()

    def cli(self, command, **kwargs):
        self.calls.append(command)
        self.assertEqual(command[2:4], ["--json", "iris"])
        self.assertIn(host.RIGHT_DEVICE_ID, command)
        action = command[4]
        if action == "device-status":
            self.identity_reads += 1
            data = dict(ok=True, device_id=host.RIGHT_DEVICE_ID,
                        device=dict(device_id=self.identity_id, stale=self.stale,
                                    boot_id=self.boot + int(self.changed_boot and self.identity_reads > 1),
                                    firmware_sha256=self.firmware_sha, **self.identity_extra))
        else:
            self.assertEqual(action, "rpc")
            self.assertEqual(command[5], "0xeca2")
            method = int(command[6])
            if method == 1:
                payload_value = dict(self.status)
                if self.generation_changed and self.metric_reads:
                    payload_value["generation"] += 1
            elif method == 2:
                # A start must be durably consumed BEFORE its possibly-uncertain RPC.
                ledger = json.loads(self.ledger.read_text())
                self.assertGreaterEqual(len(ledger["attempts"]), 1)
                self.assertEqual(bytes.fromhex(command[command.index("--payload-hex") + 1]),
                                 bytes([self.expected_start_gain]))
                if self.fail_start:
                    raise subprocess.TimeoutExpired(command, 30, stderr="uncertain start")
                self.status.update(phase=2, requested_generation=1, runs_started=1,
                                   runs_remaining=1, input_gain_db=self.expected_start_gain)
                payload_value = dict(self.status)
            elif method == 3:
                self.status.update(phase=4)
                payload_value = dict(self.status)
            else:
                self.assertEqual(method, 4)
                self.metric_reads += 1
                if self.fail_collect and self.metric_reads == 2:
                    raise subprocess.CalledProcessError(1, command, stderr="stale generation")
                generation, index = struct.unpack("<IB", bytes.fromhex(command[command.index("--payload-hex") + 1]))
                self.assertEqual(generation, 7)
                payload_value = dict(run_id=8 if self.wrong_run else 7, flags=265)
                if index == 255:
                    payload_value.update(frames_captured=self.total_frames, tones_analyzed=15,
                                         capture_adc_peak=.2, capture_clipped=0, capture_near_clip=0,
                                         noise=dict(flags=9, sample_count=self.noise_frames, clipped=0, near_clip=0,
                                                    adc_peak=.001, rms=.0007, dc=0, ac_rms=.0007))
                else:
                    self.assertIn(index, range(15))
                    payload_value.update(index=index, frequency_hz=[220, 330, 440, 660, 1000][index // 3],
                                         pcm_peak=.12 * (index % 3 + 1), window_start=self.first_window + index * 13200,
                                         sample_count=4800, adc_peak=.2, rms=.14, dc=0,
                                         clipped=0, near_clip=0, harmonic_amplitude=[.2, .02, 0, 0, 0],
                                         harmonic_ratio=[.1, 0, 0, 0], harmonic_rss_ratio=.1,
                                         fundamental_snr_db=46)
            raw = json.dumps(payload_value).encode()
            data = dict(ok=True, device_id=host.RIGHT_DEVICE_ID, response=dict(
                operation=dict(status="failed" if self.fail_operation else "succeeded",
                               device_id=host.RIGHT_DEVICE_ID, operation_id=f"op-{len(self.calls)}"),
                payload_base64=base64.b64encode(raw).decode(),
                response_bytes=len(raw) + int(self.truncated)))
        return subprocess.CompletedProcess(command, 0, json.dumps(data), "")

    def perform(self, action="status", **kwargs):
        return host.measure(Path("mosaico.py"), ROOT / "projects/echoy", action,
                            "ab" * 32, self.output, **kwargs)

    def ready(self):
        self.status.update(phase=6, generation=7, requested_generation=7,
                           runs_started=1, runs_remaining=1, frames_written=self.total_frames,
                           frames_captured=self.total_frames, elapsed_us=self.total_frames * 1000000 // 24000, result_ready=True)

    def methods(self):
        return [int(c[6]) for c in self.calls if c[4] == "rpc"]

    def test_status_is_read_only_and_preserves_live_identity(self):
        result = self.perform()
        self.assertEqual(result["boot_id"], 123)
        self.assertEqual(self.methods(), [1])
        self.assertFalse(self.ledger.exists())
        self.assertTrue((self.output / "intent.json").exists())
        self.assertTrue((self.output / "operations.jsonl").exists())

    def test_start_requires_notice_without_device_calls(self):
        with self.assertRaisesRegex(ValueError, "notice"):
            self.perform("start")
        self.assertEqual(self.calls, [])
        self.assertFalse(self.ledger.exists())

    def test_start_reserves_once_before_rpc_and_never_collects_automatically(self):
        result = self.perform("start", start_notice_given=True)
        self.assertEqual(self.methods(), [1, 2])
        self.assertEqual(result["status"]["requested_generation"], 1)
        self.assertEqual(len(json.loads(self.ledger.read_text())["attempts"]), 1)
        self.assertEqual(self.metric_reads, 0)

    def test_explicit_twelve_db_start_preserves_gain_in_intent_and_ledger(self):
        self.expected_start_gain = 12
        result = self.perform("start", start_notice_given=True, input_gain_db=12)
        self.assertEqual(result["status"]["input_gain_db"], 12)
        self.assertEqual(json.loads((self.output / "intent.json").read_text())["input_gain_db"], 12)
        self.assertEqual(json.loads(self.ledger.read_text())["attempts"][0]["input_gain_db"], 12)
        self.assertEqual(self.methods(), [1, 2])

    def test_uncertain_start_consumes_budget_without_retry_even_across_reboot(self):
        self.fail_start = True
        for attempt in range(2):
            self.output = self.base / f"run-{attempt}"
            self.boot += 1
            with self.assertRaises(subprocess.TimeoutExpired):
                self.perform("start", start_notice_given=True)
            self.assertTrue((self.output / "error.json").exists())
        self.output = self.base / "third"
        with self.assertRaisesRegex(ValueError, "budget"):
            self.perform("start", start_notice_given=True)
        self.assertEqual(self.methods().count(2), 2)
        self.assertEqual(len(json.loads(self.ledger.read_text())["attempts"]), 2)

    def test_unaccounted_device_run_cannot_expand_consent_budget(self):
        self.status.update(runs_started=1, runs_remaining=1)
        with self.assertRaisesRegex(ValueError, "ledger"):
            self.perform("start", start_notice_given=True)
        self.assertNotIn(2, self.methods())

    def test_active_or_queued_or_device_budget_prevents_start(self):
        for change in [dict(phase=3), dict(requested_generation=1), dict(runs_remaining=0, runs_started=2), dict(phase=0)]:
            with self.subTest(change=change):
                self.status.update(phase=1, requested_generation=0, runs_remaining=2, runs_started=0)
                self.status.update(change)
                self.output = self.base / str(len(self.calls))
                with self.assertRaises(ValueError):
                    self.perform("start", start_notice_given=True)
        self.assertNotIn(2, self.methods())

    def test_wrong_identity_stale_boot_or_firmware_rejected_before_start(self):
        for field, value in [("identity_id", "4553502d49524953010030eda0f452a0"),
                             ("stale", True), ("firmware_sha", "cd" * 32), ("boot", 0)]:
            with self.subTest(field=field):
                old = getattr(self, field)
                setattr(self, field, value)
                self.output = self.base / field
                with self.assertRaises(ValueError):
                    self.perform("start", start_notice_given=True)
                setattr(self, field, old)
        self.assertNotIn(2, self.methods())

    def test_wrong_mode_board_layout_or_busy_gateway_does_not_consume_attempt(self):
        mutations = [dict(firmware_mode="recovery"), dict(board_id="other-board"),
                     dict(layout_id="other-layout"),
                     dict(queue=dict(device_id=host.RIGHT_DEVICE_ID, running=["upload"], queued=[])),
                     dict(queue=dict(device_id=host.RIGHT_DEVICE_ID, running=[], queued=["restart"])),
                     dict(queue=dict(device_id=host.RIGHT_DEVICE_ID, running=[])),
                     dict(queue=dict(device_id="wrong-device", running=[], queued=[]))]
        for number, mutation in enumerate(mutations):
            with self.subTest(mutation=mutation):
                original = dict(self.identity_extra)
                self.identity_extra.update(mutation)
                self.output = self.base / f"unsafe-{number}"
                with self.assertRaises(ValueError):
                    self.perform("start", start_notice_given=True)
                self.assertEqual(json.loads(self.ledger.read_text())["attempts"], [])
                self.identity_extra = original
        self.assertNotIn(2, self.methods())

    def test_collect_reads_sixteen_metrics_with_no_waveform_or_ledger(self):
        self.ready()
        result = self.perform("collect", run_id=7)
        self.assertEqual(self.metric_reads, 16)
        self.assertEqual(len(result["tones"]), 15)
        self.assertEqual(set(self.methods()), {1, 4})
        self.assertTrue((self.output / "result.json").exists())
        self.assertFalse(self.ledger.exists())
        self.assertNotIn("excerpt", json.dumps(result))
        self.assertEqual(result["sequence_profile"]["duration_ms"], 10500)

    def test_collect_preserves_original_nine_second_profile(self):
        self.total_frames = 216000
        self.first_window = 13800
        self.noise_frames = 7200
        self.ready()
        result = self.perform("collect", run_id=7)
        self.assertEqual(result["sequence_profile"]["duration_ms"], 9000)
        self.assertEqual(result["summary"]["frames_captured"], 216000)
        self.assertEqual(result["tones"][0]["window_start"], 13800)

    def test_collect_rejects_mixed_profile_noise_or_tone_window(self):
        for attribute, bad_value in [("noise_frames", 7200), ("first_window", 13800)]:
            self.ready()
            self.output = self.base / attribute
            previous = getattr(self, attribute)
            setattr(self, attribute, bad_value)
            with self.assertRaises(ValueError):
                self.perform("collect", run_id=7)
            self.assertFalse((self.output / "result.json").exists())
            setattr(self, attribute, previous)

    def test_collect_requires_complete_ready_requested_generation(self):
        for phase, ready, generation in [(3, False, 7), (6, False, 7), (6, True, 8)]:
            self.ready()
            self.status.update(phase=phase, result_ready=ready, generation=generation)
            self.output = self.base / f"invalid-{phase}-{ready}-{generation}"
            with self.assertRaises(ValueError):
                self.perform("collect", run_id=7)
        self.assertEqual(self.metric_reads, 0)

    def test_collect_failure_keeps_partial_evidence_and_never_retries(self):
        self.ready()
        self.fail_collect = True
        with self.assertRaises(subprocess.CalledProcessError):
            self.perform("collect", run_id=7)
        self.assertEqual(self.metric_reads, 2)
        self.assertTrue((self.output / "metric-255.json").exists())
        self.assertTrue((self.output / "error.json").exists())
        self.assertFalse((self.output / "result.json").exists())

    def test_changed_boot_or_generation_or_wrong_metric_run_not_published(self):
        for field in ["changed_boot", "generation_changed", "wrong_run"]:
            with self.subTest(field=field):
                self.ready()
                self.metric_reads = 0
                self.identity_reads = 0
                setattr(self, field, True)
                self.output = self.base / field
                with self.assertRaises(ValueError):
                    self.perform("collect", run_id=7)
                self.assertFalse((self.output / "result.json").exists())
                setattr(self, field, False)

    def test_cancel_is_only_one_cancel_rpc_and_does_not_reserve_consent(self):
        self.status.update(phase=3)
        self.perform("cancel")
        self.assertEqual(self.methods(), [1, 3])
        self.assertFalse(self.ledger.exists())

    def test_failed_operation_and_truncated_response_are_preserved(self):
        for field in ["fail_operation", "truncated"]:
            setattr(self, field, True)
            self.output = self.base / field
            with self.assertRaises(ValueError):
                self.perform()
            self.assertTrue((self.output / "error.json").exists())
            setattr(self, field, False)

    def test_existing_output_is_not_overwritten(self):
        self.output.mkdir()
        (self.output / "intent.json").write_text("existing")
        with self.assertRaises(FileExistsError):
            self.perform()
        self.assertEqual((self.output / "intent.json").read_text(), "existing")
        self.assertEqual(self.calls, [])

    def test_corrupt_or_deleted_ledger_cannot_replenish_budget(self):
        self.perform("start", start_notice_given=True)
        self.ledger.write_text("corrupt")
        self.output = self.base / "corrupt"
        with self.assertRaises(ValueError):
            self.perform("start", start_notice_given=True)
        self.ledger.unlink()
        self.output = self.base / "missing"
        with self.assertRaisesRegex(ValueError, "ledger"):
            self.perform("start", start_notice_given=True)
        self.assertEqual(self.methods().count(2), 1)

    def test_concurrent_start_cannot_enter_locked_ledger(self):
        lock = self.ledger.with_suffix(".lock")
        with lock.open("w") as stream:
            fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
            with self.assertRaisesRegex(ValueError, "progress"):
                self.perform("start", start_notice_given=True)
        self.assertEqual(self.calls, [])


if __name__ == "__main__":
    unittest.main()
