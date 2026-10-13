"""Read one touch trace with the official mosaico.py CLI.

Only device-status and empty ECA1 method 3 are issued, with no touch, audio,
input injection, reset, logging dump or automatic retry.
"""
import argparse
import base64
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SUMMARY = (
    "irq_count", "irq_overwritten", "reads", "read_errors", "raw_reports",
    "raw_presses", "raw_releases", "observers", "observer_presses", "submitted",
    "same_grid", "multiple_irq_windows", "irq_during_read", "event_count",
    "event_overwritten", "busy_drops", "events_copied", "events_omitted",
    "irqs_copied", "irqs_omitted", "max_read_gap_us", "max_read_duration_us",
    "max_latest_irq_age_us", "max_raw_to_observer_us",
)
EDGE = (
    "at_us", "irq_at_us", "raw_at_us", "capture_at_us", "seq", "read_seq",
    "raw_seq", "irq_seq", "phase", "grid", "pending_seed", "submitted",
    "raw_match", "same_grid",
)


def capture_trace(mosaico, project, device_id, output):
    output = Path(output).resolve()
    if output.exists():
        raise FileExistsError(f"preserve existing capture: {output}")
    common = ["--device-id", device_id, "--project", str(Path(project).resolve())]

    def cli(*arguments):
        result = subprocess.run([sys.executable, str(mosaico), "--json", "iris", *arguments, *common],
                                check=True, capture_output=True, text=True, timeout=30)
        document = json.loads(result.stdout)
        if document.get("ok") is not True or document.get("device_id") != device_id:
            raise ValueError("official CLI did not confirm the requested device")
        return document

    def identity():
        device = cli("device-status")["device"]
        if device.get("device_id") != device_id or device.get("stale") is not False or not device.get("boot_id"):
            raise ValueError("missing live device identity/boot")
        return device["boot_id"]

    boot = identity()
    response = cli("rpc", "0xeca1", "3")["response"]
    operation = response.get("operation", {})
    if (operation.get("status") != "succeeded" or operation.get("device_id") != device_id or
            not operation.get("operation_id")):
        raise ValueError("touch trace RPC was not confirmed successful")
    data = base64.b64decode(response["payload_base64"], validate=True)
    if len(data) != response.get("response_bytes") or len(data) > 3500:
        raise ValueError("invalid touch trace response size")
    trace = json.loads(data)
    if trace.get("schema") != "echoy-touch-v1":
        raise ValueError("unknown touch trace schema")
    if trace.get("available") is not True:
        raise ValueError(f"touch trace unavailable: {trace.get('reason', 'unspecified')}")
    state, summary, edges = trace["state"], trace["summary"], trace["edges"]
    if (type(state) is not int or state not in range(4) or
            len(summary) != len(SUMMARY) or any(type(x) is not int or x < 0 for x in summary) or
            len(edges) > 16 or any(len(row) != len(EDGE) or any(type(x) is not int for x in row) for row in edges)):
        raise ValueError("invalid touch trace summary/edge fields")
    summary = dict(zip(SUMMARY, summary))
    omissions = {key: trace[key] for key in (
        "pressed_retained", "pressed_omitted", "events_not_returned", "irqs_not_returned")}
    if (any(type(x) is not int or x < 0 for x in omissions.values()) or
            not len(edges) <= omissions["pressed_retained"] <= summary["events_copied"] <= 512 or
            omissions["pressed_omitted"] != summary["observer_presses"] - len(edges) or
            omissions["events_not_returned"] != summary["events_copied"] - len(edges) or
            omissions["irqs_not_returned"] != summary["irqs_copied"] or summary["irqs_copied"] > 64):
        raise ValueError("inconsistent touch trace omission counts")
    if identity() != boot:
        raise ValueError("device boot changed during touch trace capture")
    result = dict(schema=trace["schema"], device_id=device_id, boot_id=boot, state=state,
                  state_name=("inactive", "armed", "capturing", "frozen")[state],
                  operation_id=operation["operation_id"], summary=summary, omissions=omissions,
                  edges=[dict(zip(EDGE, row)) for row in edges],
                  note="IRQ windows are observations, not physical touch IDs or lost-note counts; grid uses observer callback time")
    output.mkdir(parents=True)
    (output / "touch.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="new local capture directory")
    parser.add_argument("--device-id", required=True)
    parser.add_argument("--mosaico", type=Path, default=ROOT.parent / "esp-mosaico-vibe/mosaico.py")
    parser.add_argument("--project", type=Path, default=ROOT / "projects/echoy")
    args = parser.parse_args()
    if len(args.device_id) != 32 or any(c not in "0123456789abcdef" for c in args.device_id):
        parser.error("--device-id must be 32 lowercase hexadecimal characters")
    result = capture_trace(args.mosaico.resolve(), args.project, args.device_id, args.output)
    print(json.dumps(dict(output=str(args.output.resolve()), device_id=result["device_id"],
                          boot_id=result["boot_id"], state=result["state_name"], summary=result["summary"],
                          omissions=result["omissions"], returned_edges=len(result["edges"])), indent=2))


if __name__ == "__main__":
    main()
