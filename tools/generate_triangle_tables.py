#!/usr/bin/env python3
"""Generate shared immutable chord tables using the production C implementation."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
MAIN=ROOT/"projects/echoy/main"

def generate(output,compiler):
    output=Path(output).resolve();output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="echoy-triangle-") as temporary:
        build=Path(temporary)
        subprocess.run([compiler,"-std=c11","-O2","-Wall","-Wextra","-Werror","-I",str(MAIN),
            str(ROOT/"tools/generate_triangle_tables.c"),str(MAIN/"echoy_music.c"),"-lm","-o",str(build/"generate")],check=True)
        subprocess.run([str(build/"generate"),str(build/"tables.h"),str(build/"tables.bin")],check=True)
        header=(build/"tables.h").read_text(); data=(build/"tables.bin").read_bytes()
    inputs=[ROOT/"tools/generate_triangle_tables.c",Path(__file__).resolve(),
            MAIN/"echoy_audio.c",MAIN/"echoy_music.c"]+sorted(MAIN.glob("*.h"))
    harmonics=[int(n) for n in re.search(r"echoy_triangle_harmonics\[\d+\] = \{(.*?)\}",header)[1].replace("u","").split(",")]
    metadata=dict(schema=1,sample_rate=24000,table_frames=4096,harmonics=harmonics,bytes=len(data),
                  sha256=hashlib.sha256(data).hexdigest(),header_sha256=hashlib.sha256(header.encode()).hexdigest(),
                  inputs={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs})
    (output/"echoy_triangle_tables.h").write_text(header)
    (output/"echoy_triangle_tables.bin").write_bytes(data)
    (output/"echoy_triangle_tables.json").write_text(json.dumps(metadata,indent=2)+"\n")
    print(f"Generated {len(harmonics)} triangle tables, {len(data)} bytes")

if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir",type=Path,required=True)
    parser.add_argument("--cc",default="cc")
    args=parser.parse_args();generate(args.output_dir,args.cc)
