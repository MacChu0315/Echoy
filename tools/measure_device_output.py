"""Run or read the explicitly authorized, finite right-device output trial.

Only --start captures microphone audio. It issues one start RPC, without retries
or automatic collection. Every start attempt consumes the fixed two-run consent
budget before submission, even if the response is lost. Keep the room quiet and
give the user a start notice before passing --start-notice-given. Collected
metrics are relative output-chain evidence, not calibrated SPL or speaker THD.
"""

import argparse
import base64
from contextlib import contextmanager, nullcontext
from datetime import datetime, timezone
import fcntl
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import uuid

ROOT = Path(__file__).resolve().parents[1]
RIGHT_DEVICE_ID = "4553502d49524953010030eda0f40c6c"
ARTIFACT_ROOT = ROOT / "output/harmony-measurement-20261014"
CONSENT_LEDGER = ARTIFACT_ROOT / "consent-ledger.json"
CONSENT_ID = "right-output-measurement-2026-10-14"
MAX_ATTEMPTS = 2
# The first physical run remains a 9 s record. The settling revision extends
# only leading silence and moves the quiet noise window; tone levels/timing
# and the two-attempt / 12 s hard limits are unchanged.
SEQUENCE_PROFILES = {
    216000: dict(name="initial-9s", duration_ms=9000, lead_frames=12000,
                 noise_start=2400, noise_frames=7200, first_tone_window=13800),
    252000: dict(name="settled-10.5s", duration_ms=10500, lead_frames=48000,
                 noise_start=28800, noise_frames=14400, first_tone_window=49800),
}
CURRENT_TOTAL_FRAMES = 252000


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def write_json(path, value):
    with Path(path).open("x", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2, allow_nan=False)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())


def save_ledger(value):
    """Replace only our fixed ledger, durably, while holding its separate lock."""
    descriptor, temporary = tempfile.mkstemp(prefix=".consent-", dir=CONSENT_LEDGER.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(value, stream, indent=2, allow_nan=False)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, CONSENT_LEDGER)
        directory = os.open(CONSENT_LEDGER.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


@contextmanager
def consent_lock():
    CONSENT_LEDGER.parent.mkdir(parents=True, exist_ok=True)
    lock_path = CONSENT_LEDGER.with_suffix(".lock")
    try:
        descriptor = os.open(lock_path, os.O_RDWR | os.O_CREAT | os.O_EXCL, 0o600)
        new_lock = True
    except FileExistsError:
        descriptor = os.open(lock_path, os.O_RDWR)
        new_lock = False
    with os.fdopen(descriptor, "r+") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise ValueError("another measurement start is in progress") from error
        if not CONSENT_LEDGER.exists():
            if not new_lock:
                raise ValueError("consent ledger is missing; do not recreate or reset it")
            save_ledger(dict(schema=1, consent_id=CONSENT_ID, device_id=RIGHT_DEVICE_ID,
                             max_attempts=MAX_ATTEMPTS, attempts=[]))
        try:
            ledger = json.loads(CONSENT_LEDGER.read_text())
        except (OSError, ValueError) as error:
            raise ValueError("consent ledger is unreadable; preserve it for review") from error
        if (not isinstance(ledger, dict) or ledger.get("schema") != 1 or
                ledger.get("consent_id") != CONSENT_ID or ledger.get("device_id") != RIGHT_DEVICE_ID or
                ledger.get("max_attempts") != MAX_ATTEMPTS or not isinstance(ledger.get("attempts"), list) or
                len(ledger["attempts"]) > MAX_ATTEMPTS):
            raise ValueError("invalid consent ledger; do not reset it")
        for number, entry in enumerate(ledger["attempts"], 1):
            if not isinstance(entry, dict) or entry.get("attempt") != number or not entry.get("reserved_at"):
                raise ValueError("invalid consent ledger attempt")
        yield ledger


def validate_status(value):
    if not isinstance(value, dict) or value.get("schema") != 1:
        raise ValueError("invalid measurement status schema")
    limits = dict(phase=(0, 8), generation=(0, 0xffffffff),
                  requested_generation=(0, 0xffffffff), runs_started=(0, 2),
                  runs_remaining=(0, 2), frames_written=(0, CURRENT_TOTAL_FRAMES),
                  frames_captured=(0, CURRENT_TOTAL_FRAMES), elapsed_us=(0, 0xffffffffffffffff))
    for key, (minimum, maximum) in limits.items():
        if type(value.get(key)) is not int or not minimum <= value[key] <= maximum:
            raise ValueError(f"invalid measurement status {key}")
    if (type(value.get("result_ready")) is not bool or type(value.get("error_code")) is not int or
            not isinstance(value.get("input_gain_db"), (int, float)) or
            not math.isfinite(value["input_gain_db"]) or value["input_gain_db"] not in range(0, 31, 6)):
        raise ValueError("invalid measurement status flags/gain")
    return value


def require_complete(status, run_id):
    if (status["phase"] != 6 or status["result_ready"] is not True or
            status["generation"] != run_id or status["requested_generation"] != run_id):
        raise ValueError("requested run is not complete and ready, or generation changed")


def require_start_identity(device):
    if (device.get("firmware_mode") != "normal" or device.get("board_id") != "esp-mosaico" or
            device.get("layout_id") != "mosaico-retained-recovery-2m-v1"):
        raise ValueError("start requires the expected application mode, board, and retained Recovery layout")
    queue = device.get("queue")
    if (not isinstance(queue, dict) or queue.get("device_id") != RIGHT_DEVICE_ID or
            queue.get("running") != [] or queue.get("queued") != []):
        raise ValueError("start requires an empty, confirmed right-device operation queue")


def validate_metric(value, run_id, index, sequence_profile=None):
    if not isinstance(value, dict) or value.get("run_id") != run_id:
        raise ValueError("metric run generation changed")
    if type(value.get("flags")) is not int or value["flags"] < 0:
        raise ValueError("invalid metric flags")
    if index == 255:
        sequence_profile = SEQUENCE_PROFILES.get(value.get("frames_captured"))
        if (sequence_profile is None or value.get("tones_analyzed") != 15 or
                not isinstance(value.get("noise"), dict) or
                value["noise"].get("sample_count") != sequence_profile["noise_frames"]):
            raise ValueError("incomplete global measurement")
    else:
        if (sequence_profile is None or value.get("index") != index or
                value.get("frequency_hz") != [220, 330, 440, 660, 1000][index // 3] or
                value.get("sample_count") != 4800 or
                value.get("window_start") != sequence_profile["first_tone_window"] + index * 13200 or
                not isinstance(value.get("pcm_peak"), (int, float)) or
                abs(value["pcm_peak"] - .12 * (index % 3 + 1)) > .000001):
            raise ValueError("invalid tone identity/window")
        for name, size in [("harmonic_amplitude", 5), ("harmonic_ratio", 4)]:
            if not isinstance(value.get(name), list) or len(value[name]) != size:
                raise ValueError("invalid harmonic metrics")
    # Never accept NaN/Infinity or accidentally add raw microphone exports.
    encoded = json.dumps(value, allow_nan=False)
    if "excerpt" in encoded or "waveform" in encoded:
        raise ValueError("unexpected microphone waveform export")
    return value


def measure(mosaico, project, action, expected_elf_sha256, output,
            *, start_notice_given=False, input_gain_db=0, run_id=None):
    if action not in {"start", "status", "cancel", "collect"}:
        raise ValueError("unknown measurement action")
    if (len(expected_elf_sha256) != 64 or
            any(c not in "0123456789abcdef" for c in expected_elf_sha256)):
        raise ValueError("expected diagnostic ELF SHA-256 must be 64 lowercase hexadecimal characters")
    if action == "start" and not start_notice_given:
        raise ValueError("an explicit user start notice is required before --start")
    if type(input_gain_db) is not int or input_gain_db not in range(0, 31, 6):
        raise ValueError("input gain must be one of 0, 6, 12, 18, 24, 30 dB")
    if action == "collect" and (type(run_id) is not int or not 1 <= run_id <= 0xffffffff):
        raise ValueError("--collect requires an explicit positive --run-id")
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    intent = dict(schema=1, action=action, device_id=RIGHT_DEVICE_ID, created_at=utc_now(),
                  expected_elf_sha256=expected_elf_sha256, input_gain_db=input_gain_db,
                  start_notice_given=start_notice_given, requested_run_id=run_id,
                  note="No automatic retries, recording at boot, loopback, or waveform export")
    if action == "start":
        intent["sequence_profile"] = SEQUENCE_PROFILES[CURRENT_TOTAL_FRAMES]
    write_json(output / "intent.json", intent)
    operations = []
    common = ["--device-id", RIGHT_DEVICE_ID, "--project", str(Path(project).resolve())]

    def log_operation(value):
        with (output / "operations.jsonl").open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(value, allow_nan=False) + "\n")
            stream.flush()
            os.fsync(stream.fileno())

    def cli(*arguments):
        command = [sys.executable, str(mosaico), "--json", "iris", *arguments, *common]
        log_operation(dict(at=utc_now(), state="submitted", command=command))
        try:
            completed = subprocess.run(command, check=True, capture_output=True, text=True, timeout=30)
        except (subprocess.SubprocessError, OSError) as error:
            log_operation(dict(at=utc_now(), state="unconfirmed", error=str(error)))
            raise
        log_operation(dict(at=utc_now(), state="returned", stdout=completed.stdout, stderr=completed.stderr))
        document = json.loads(completed.stdout)
        if document.get("ok") is not True or document.get("device_id") != RIGHT_DEVICE_ID:
            raise ValueError("official CLI did not confirm the fixed right-device identity")
        return document

    def identity():
        value = cli("device-status")["device"]
        if (value.get("device_id") != RIGHT_DEVICE_ID or value.get("stale") is not False or
                not value.get("boot_id") or value.get("firmware_sha256") != expected_elf_sha256):
            raise ValueError("missing live right-device boot or diagnostic ELF identity mismatch")
        return value

    def rpc(method, payload=None):
        arguments = ["rpc", "0xeca2", str(method)]
        if payload is not None:
            arguments += ["--payload-hex", payload.hex()]
        response = cli(*arguments)["response"]
        operation = response.get("operation", {})
        if (operation.get("status") != "succeeded" or operation.get("device_id") != RIGHT_DEVICE_ID or
                not operation.get("operation_id")):
            raise ValueError("measurement RPC was not confirmed successful")
        operations.append(operation["operation_id"])
        raw = base64.b64decode(response["payload_base64"], validate=True)
        if len(raw) != response.get("response_bytes"):
            raise ValueError("truncated measurement RPC response")
        return json.loads(raw)

    try:
        with consent_lock() if action == "start" else nullcontext(None) as ledger:
            if ledger is not None and len(ledger["attempts"]) >= MAX_ATTEMPTS:
                raise ValueError("the fixed two-attempt consent budget is exhausted")
            device = identity()
            write_json(output / "identity-before.json", device)
            if action == "start":
                require_start_identity(device)
            status = validate_status(rpc(1))
            write_json(output / "status-before.json", status)
            result = dict(device_id=RIGHT_DEVICE_ID, boot_id=device["boot_id"],
                          expected_elf_sha256=expected_elf_sha256, action=action, status=status)
            if action == "start":
                if (status["phase"] not in {1, 6, 7, 8} or not status["runs_remaining"] or
                        status["requested_generation"] != status["generation"]):
                    raise ValueError("device is disabled, active, queued, or has no remaining run budget")
                if (status["runs_started"] > len(ledger["attempts"]) or
                        status["runs_started"] + status["runs_remaining"] != MAX_ATTEMPTS):
                    raise ValueError("device run budget is not accounted for by the fixed consent ledger")
                entry = dict(attempt=len(ledger["attempts"]) + 1, reserved_at=utc_now(),
                             boot_id=device["boot_id"], output=str(output), input_gain_db=input_gain_db,
                             expected_elf_sha256=expected_elf_sha256,
                             note="Consumed even if start fails or its outcome is uncertain; never reset")
                ledger["attempts"].append(entry)
                save_ledger(ledger)
                result["consent_attempt"] = entry["attempt"]
                result["status"] = validate_status(rpc(2, bytes([input_gain_db])))
                write_json(output / "status-after.json", result["status"])
            elif action == "cancel":
                result["status"] = validate_status(rpc(3))
                write_json(output / "status-after.json", result["status"])
            elif action == "collect":
                require_complete(status, run_id)
                metrics = []
                sequence_profile = None
                for index in [255, *range(15)]:
                    metric = validate_metric(rpc(4, struct.pack("<IB", run_id, index)),
                                             run_id, index, sequence_profile)
                    write_json(output / f"metric-{index:03}.json", metric)
                    metrics.append(metric)
                    if index == 255:
                        if metric["frames_captured"] != status["frames_captured"]:
                            raise ValueError("status and result sequence lengths do not match")
                        sequence_profile = SEQUENCE_PROFILES[metric["frames_captured"]]
                after = validate_status(rpc(1))
                write_json(output / "status-after.json", after)
                require_complete(after, run_id)
                result.update(run_id=run_id, summary=metrics[0], tones=metrics[1:], status=after,
                              sequence_profile=sequence_profile,
                              note="Relative output-chain measurements; ADC flags invalidate interpretation. "
                                   "Not calibrated SPL or speaker THD; microphone noise/nonlinearity remain.")
            after_device = identity()
            write_json(output / "identity-after.json", after_device)
            if after_device["boot_id"] != device["boot_id"]:
                raise ValueError("device boot changed during operation; do not retry a start")
            result["rpc_operation_ids"] = operations
            if action == "collect":
                write_json(output / "result.json", result)
            write_json(output / "outcome.json", result)
            return result
    except Exception as error:
        write_json(output / "error.json", dict(error_type=type(error).__name__, error=str(error),
                                               at=utc_now(), rpc_operation_ids=operations,
                                               note="Preserve evidence. Never retry a start automatically."))
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    for name in ("start", "status", "cancel", "collect"):
        action.add_argument(f"--{name}", dest="action", action="store_const", const=name)
    parser.add_argument("--expected-elf-sha256", required=True,
                        help="full SHA-256 of the approved diagnostic ELF")
    parser.add_argument("--start-notice-given", action="store_true",
                        help="confirm the user has been told this finite microphone trial will start")
    parser.add_argument("--input-gain-db", type=int, choices=range(0, 31, 6), default=0)
    parser.add_argument("--run-id", type=int, help="exact completed generation to collect")
    parser.add_argument("--output", type=Path, help="new local evidence directory; never overwritten")
    parser.add_argument("--mosaico", type=Path, default=ROOT.parent / "esp-mosaico-vibe/mosaico.py")
    parser.add_argument("--project", type=Path, default=ROOT / "projects/echoy")
    args = parser.parse_args()
    output = args.output or ARTIFACT_ROOT / f"{args.action}-{datetime.now(timezone.utc):%Y%m%dT%H%M%S}-{uuid.uuid4().hex[:8]}"
    try:
        result = measure(args.mosaico.resolve(), args.project, args.action, args.expected_elf_sha256,
                         output, start_notice_given=args.start_notice_given,
                         input_gain_db=args.input_gain_db, run_id=args.run_id)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        parser.exit(1, f"{error}\nLocal evidence: {output.resolve()}\n")
    print(json.dumps(dict(output=str(output.resolve()), device_id=result["device_id"],
                          boot_id=result["boot_id"], status=result["status"]), indent=2))


if __name__ == "__main__":
    main()
