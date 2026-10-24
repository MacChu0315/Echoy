"""Compare preparation against the retained pre-optimization C implementation."""
import argparse
from pathlib import Path
import re
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MAIN = ROOT / "projects/echoy/main"

def run(baseline, triangle_dir=None):
    with tempfile.TemporaryDirectory(prefix="echoy-preparation-") as temporary:
        build=Path(temporary)
        source=subprocess.check_output(["git","show",f"{baseline}:projects/echoy/main/echoy_audio.c"],cwd=ROOT)
        (build/"reference.c").write_bytes(source)
        public=sorted(set(re.findall(r"\b(echoy_audio_\w+)\s*\(", (MAIN/"echoy_audio.h").read_text())))
        common=[os.environ.get("CC","cc"),"-std=c11","-O2","-g","-UNDEBUG","-Wall","-Wextra","-Werror",
                *shlex.split(os.environ.get("AUDIO_TEST_CFLAGS","")),"-I",str(MAIN)]
        subprocess.run(common+[f"-D{name}=reference_{name}" for name in public]+[
            "-c",str(build/"reference.c"),"-o",str(build/"reference.o")],check=True)
        flags=["-DECHOY_USE_PREBUILT_TRIANGLES=1","-I",str(triangle_dir)] if triangle_dir else []
        subprocess.run(common+flags+[str(ROOT/"tests/audio/test_preparation_equivalence.c"),
            str(MAIN/"echoy_audio.c"),str(MAIN/"echoy_music.c"),str(build/"reference.o"),
            "-lm","-o",str(build/"compare")],check=True)
        subprocess.run([str(build/"compare")],check=True)

if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline",default="eff8303")
    parser.add_argument("--triangle-dir",type=Path)
    args=parser.parse_args();run(args.baseline,args.triangle_dir)
