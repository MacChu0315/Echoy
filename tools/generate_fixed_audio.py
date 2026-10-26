#!/usr/bin/env python3
"""Compile the original C synth on the build host and package fixed float32 audio."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / "projects/echoy/main"
SOURCES = [ROOT / "tools/generate_fixed_audio.c"] + [
    MAIN / f"{name}.c" for name in ("echoy_audio", "echoy_music", "echoy_device_pcm")]


def generate(profile, output, compiler, c_source=None):
    output = Path(output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="echoy-fixed-") as tmp:
        executable = Path(tmp) / "generate"
        subprocess.run([compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-I", str(MAIN), *map(str, SOURCES), "-lm", "-o", str(executable)], check=True)
        temporary = Path(tmp) / "audio.bin"
        subprocess.run([str(executable), profile, str(temporary)], check=True)
        data = temporary.read_bytes()
    entries = []
    for i in range(7):
        kind, identity, frames, offset, *windows = struct.unpack_from("<20I", data, 48 + i * 80)
        entries.append(dict(kind=kind, id=identity, frames=frames, offset=offset,
                            starts=windows[:8], lengths=windows[8:],
                            sha256=hashlib.sha256(data[offset:offset+frames*4]).hexdigest()))
    inputs = SOURCES + sorted(MAIN.glob("*.h")) + [Path(__file__).resolve()]
    manifest = dict(schema=1, profile=profile, sample_rate=24000, seed=0x2555,
                    gains=struct.unpack_from("<4f", data, 32), bytes=len(data),
                    sha256=hashlib.sha256(data).hexdigest(), entries=entries,
                    inputs={str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs})
    output.write_bytes(data)
    output.with_suffix(".json").write_text(json.dumps(manifest, indent=2) + "\n")
    if c_source:
        # Portable native-simulator embedding; ESP-IDF embeds the binary directly.
        lines = [", ".join(f"0x{byte:02x}" for byte in data[i:i+16]) for i in range(0, len(data), 16)]
        Path(c_source).write_text("#include <stddef.h>\nconst unsigned char echoy_fixed_audio_bytes[] = {\n" +
                                  ",\n".join(lines) + "\n};\nconst size_t echoy_fixed_audio_size = sizeof(echoy_fixed_audio_bytes);\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=("office", "device", "reference"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cc", default="cc", help="Native build-host C compiler (not the ESP cross compiler)")
    parser.add_argument("--c-source", type=Path, help="Optional portable C embedding for the native simulator")
    args = parser.parse_args()
    generate(args.profile, args.output, args.cc, args.c_source)
