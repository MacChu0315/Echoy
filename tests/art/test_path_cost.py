"""Count actual path-bound visits with GCC coverage, without timing thresholds.

Run separately from the portable art suite; this probe requires matching GCC
and gcov. Source text locates the coverage counter, not the pass condition.
"""
import argparse
import gzip
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PROBE = r'''
#include "echoy_art.h"
#include <assert.h>
int main(void)
{
    uint16_t pixels[300*204];
    struct echoy_art_workspace *work=echoy_art_workspace_create();
    assert(work);
    echoy_art_frame_t f={.role=ECHOY_VOICE,.width=300,.height=204,
        .active=true,.energy=.7f,.progress=.27,.workspace=work};
    esp_gsp_canvas_surface_t s={.pixels=pixels,.stride_bytes=600,.width=300,.height=204,
        .pixel_format=ESP_GSP_CANVAS_PIXEL_RGB565};
    echoy_art_draw(&s,&f);
    echoy_art_workspace_delete(work);
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path,
                        default=ROOT / 'projects/echoy/main/echoy_art.c')
    args = parser.parse_args()
    source = args.source.read_text()
    counter_lines = [i + 1 for i, line in enumerate(source.splitlines())
                     if line.strip() == 'bounds_t segment = segments[i];']
    assert len(counter_lines) == 1, 'Locate the path bounds load in the coverage report'
    with tempfile.TemporaryDirectory(prefix='echoy-path-cost-') as temporary:
        work = Path(temporary)
        (work / 'art.c').write_text(source)
        (work / 'probe.c').write_text(PROBE)
        flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-O0', '--coverage',
                 '-I' + str(ROOT / 'projects/echoy/main'),
                 '-I' + str(ROOT / 'projects/echoy/managed_components/espressif__esp-gsp/include')]
        cc = shlex.split(os.environ.get('CC', 'cc'))
        subprocess.run([*cc, *flags, '-c', 'art.c', '-o', 'art.o'], cwd=work, check=True)
        subprocess.run([*cc, *flags, 'probe.c', 'art.o', '-lm', '-o', 'probe'], cwd=work, check=True)
        subprocess.run([str(work / 'probe')], cwd=work, check=True)
        subprocess.run([os.environ.get('GCOV', 'gcov'), '--json-format', '-b', '-c', 'art.c'],
                       cwd=work, check=True, capture_output=True)
        report = json.loads(gzip.decompress((work / 'art.gcov.json.gz').read_bytes()))
        counter = next(line for file in report['files'] if file['file'].endswith('art.c')
                       for line in file['lines'] if line['line_number'] == counter_lines[0])
        visits = counter['count']
        print(json.dumps({'voice_segment_visits': visits, 'maximum': 20000}))
        assert visits < 20000, 'Voice must skip path segments outside the current coverage block'


if __name__ == '__main__':
    main()
