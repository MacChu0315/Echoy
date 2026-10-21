"""Read an immutable Echoy plan through the official mosaico.py CLI.

Only device-status and ECA1 snapshot/chunk requests are issued. No playback,
pause, upload, restart, input injection or legacy ECA0 tone RPC is available.
"""
import argparse
import base64
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]


def capture_snapshot(mosaico, project, device_id, output):
    output = Path(output).resolve()
    if output.exists():
        raise FileExistsError(f"preserve existing capture: {output}")
    common = ["--device-id", device_id, "--project", str(Path(project).resolve())]
    operations = []

    def cli(*arguments):
        result = subprocess.run([sys.executable, str(mosaico), "--json", "iris", *arguments, *common],
                                check=True, capture_output=True, text=True, timeout=30)
        document = json.loads(result.stdout)
        if document.get("ok") is not True or document.get("device_id") != device_id:
            raise ValueError("official CLI did not confirm the requested device")
        return document

    def identity():
        value = cli("device-status")["device"]
        if value.get("device_id") != device_id or value.get("stale") is not False or not value.get("boot_id"):
            raise ValueError("missing live device identity/boot")
        return value["boot_id"]

    def rpc(method, payload=None):
        arguments = ["rpc", "0xeca1", str(method)]
        if payload is not None:
            arguments += ["--payload-hex", payload.hex()]
        response = cli(*arguments)["response"]
        operation = response.get("operation", {})
        if (operation.get("status") != "succeeded" or operation.get("device_id") != device_id or
                not operation.get("operation_id")):
            raise ValueError("snapshot RPC was not confirmed successful")
        data = base64.b64decode(response["payload_base64"], validate=True)
        if len(data) != response.get("response_bytes"):
            raise ValueError("truncated snapshot RPC response")
        operations.append(operation.get("operation_id"))
        return data

    boot_id = identity()
    metadata = json.loads(rpc(1))
    size, chunk = metadata["size"], metadata["chunk_max"]
    volume = metadata["volume"]
    if (metadata.get("schema") != 1 or not isinstance(size, int) or not 32 <= size <= 8192 or
            not isinstance(chunk, int) or not 1 <= chunk <= 512 or
            not isinstance(volume, (int, float)) or not math.isfinite(volume) or not 0 <= volume <= 1 or
            len(metadata["token"]) != 16 or len(metadata["crc32"]) != 8):
        raise ValueError("invalid snapshot metadata")
    token = int(metadata["token"], 16)
    if not 0 < token <= 0xffffffffffffffff:
        raise ValueError("invalid snapshot token")
    data = bytearray()
    for offset in range(0, size, chunk):
        length = min(chunk, size - offset)
        part = rpc(2, struct.pack("<QIH", token, offset, length))
        if len(part) != length:
            raise ValueError("incomplete snapshot chunk")
        data.extend(part)
    if zlib.crc32(data) != int(metadata["crc32"], 16):
        raise ValueError("snapshot CRC mismatch")
    if data[:4] != b"ECHP" or struct.unpack_from("<HHI", data, 4) != (1, 32, size):
        raise ValueError("invalid canonical ECHP header")
    if identity() != boot_id:
        raise ValueError("device boot changed during snapshot read")
    result = dict(device_id=device_id, boot_id=boot_id, metadata=metadata,
                  plan_sha256=hashlib.sha256(data).hexdigest(), rpc_operation_ids=operations,
                  note="Coherent cached current plan; volume is the exact UI target, not a smoothed PCM sample gain")
    output.mkdir(parents=True)
    (output / "plan.echp").write_bytes(data)
    (output / "snapshot.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="new local capture directory")
    parser.add_argument("--device-id", required=True, help="verified 32-character device identity")
    parser.add_argument("--mosaico", type=Path, default=ROOT.parent / "esp-mosaico-vibe-iris02/mosaico.py")
    parser.add_argument("--project", type=Path, default=ROOT / "projects/echoy")
    args = parser.parse_args()
    if len(args.device_id) != 32 or any(c not in "0123456789abcdef" for c in args.device_id):
        parser.error("--device-id must be 32 lowercase hexadecimal characters")
    result = capture_snapshot(args.mosaico.resolve(), args.project, args.device_id, args.output)
    print(json.dumps(dict(output=str(args.output.resolve()), device_id=result["device_id"],
                          boot_id=result["boot_id"], metadata=result["metadata"]), indent=2))


if __name__ == "__main__":
    main()
