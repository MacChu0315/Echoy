"""Compare actual C output to frozen HTML OfflineAudioContext recordings.

Budgets are set from the intended numerical fidelity before comparison:
0.5% RMS, 2% peak, 3% time-domain relative error (>30dB SNR), 1.5% spectral
magnitude error, and one sample at the first audible onset. Report every
residual; none of these permit changing a pitch, envelope or onset.
"""
import json
import math
import os
from pathlib import Path
import shlex
import subprocess
import sys

import numpy as np

root = Path(__file__).resolve().parents[2]
reference = Path(sys.argv[1]).resolve()
output = reference / 'actual'
output.mkdir(exist_ok=True)
manifest = json.loads((reference / 'manifest.json').read_text())

def number(value):
    return repr(value if value is not None else 0)

def note(value, seed=False):
    fields = [f'.step={number(value.get("step", 0))}',
              f'.velocity={number(value.get("vel", .74))}',
              '.kind=ECHOY_' + value.get('kind', 'kick').upper()]
    if not seed:
        fields += [f'.duration={number(value.get("dur", 0))}',
                   f'.max_duration={number(value.get("maxDuration", 0))}',
                   f'.note={value.get("note", 0)}',
                   '.layer=ECHOY_' + value.get('layer', value.get('role', 'anchor')).upper(),
                   '.articulation=ECHOY_' + value.get('articulation', 'ARTICULATION_NONE').upper()]
        for key in ('notes', 'harmony'):
            values = value.get(key, [])
            fields += ['.' + key + '={' + ','.join(map(str, values)) + '}',
                       '.' + ('note_count' if key == 'notes' else 'harmony_count') + '=' + str(len(values))]
    return '{' + ','.join(fields) + '}'

rows = []
for case in manifest['cases']:
    plan = case['plan']
    fields = ['.role=ECHOY_' + plan['role'].upper(), '.phrase=' + str(plan['phrase']),
              '.density=' + number(plan['density']), '.duration=2.5',
              '.source_count=' + str(len(plan['source'])), '.count=' + str(len(plan['full'])),
              '.source={' + ','.join(note(n, True) for n in plan['source']) + '}',
              '.events={' + ','.join(note(n) for n in plan['full']) + '}']
    rows.append('{' + json.dumps(case['name']) + ',' + ('true' if case.get('original') else 'false') + ',{' + ','.join(fields) + '}}')
(output / 'oracle_cases.h').write_text('static const struct { const char *name; bool original; echoy_plan_t plan; } cases[]={\n' + ',\n'.join(rows) + '\n};\n')
command = [os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2', '-g']
command += shlex.split(os.environ.get('AUDIO_TEST_CFLAGS', ''))
command += ['-I' + str(root / 'projects/echoy/main'), '-I' + str(output), str(root / 'tests/audio/oracle_probe.c'),
            str(root / 'projects/echoy/main/echoy_audio.c'), '-lm', '-o', str(output / 'oracle_probe')]
subprocess.run(command, check=True)
subprocess.run([str(output / 'oracle_probe'), str(output)], check=True)

failed = []
report = []
for expected_file in sorted(reference.glob('*.f32')):
    expected = np.fromfile(expected_file, dtype='<f4').astype(float)
    actual = np.fromfile(output / expected_file.name, dtype='<f4').astype(float)
    assert expected.shape == actual.shape
    assert np.all(np.isfinite(actual))
    norm = np.linalg.norm(expected)
    rms_ratio = np.linalg.norm(actual) / norm - 1
    peak_ratio = np.max(np.abs(actual)) / np.max(np.abs(expected)) - 1
    error = np.linalg.norm(actual - expected) / norm
    spectrum = np.abs(np.fft.rfft(expected))
    spectral = np.linalg.norm(np.abs(np.fft.rfft(actual)) - spectrum) / np.linalg.norm(spectrum)
    threshold = np.max(np.abs(expected)) * 1e-4
    onset = int(np.argmax(np.abs(actual) > threshold)) - int(np.argmax(np.abs(expected) > threshold))
    correlation = np.dot(actual, expected) / (np.linalg.norm(actual) * norm)
    passed = abs(rms_ratio) <= .005 and abs(peak_ratio) <= .02 and error <= .03 and spectral <= .015 and abs(onset) <= 1
    row = dict(name=expected_file.stem, rms_percent=float(rms_ratio * 100), peak_percent=float(peak_ratio * 100),
               relative_error=float(error), spectral_error=float(spectral), onset_samples=onset,
               correlation=float(correlation), snr_db=float(-20 * math.log10(max(error, 1e-20))), passed=bool(passed))
    report.append(row)
    if not passed: failed.append(row)
    print(f'{"PASS" if passed else "FAIL"} {row["name"]:37s} RMS {rms_ratio*100:+7.3f}% peak {peak_ratio*100:+7.3f}% err {error:.5f} FFT {spectral:.5f} onset {onset:+d}')
(output / 'comparison.json').write_text(json.dumps(dict(browser=manifest['browser'],results=report), indent=2) + '\n')
print(f'{len(report)-len(failed)}/{len(report)} audio references within predeclared budgets')
sys.exit(bool(failed))
