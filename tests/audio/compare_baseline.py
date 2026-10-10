"""Check all legal pitches against a retained pre-optimization C source.

Usage: python3 tests/audio/compare_baseline.py OLD_AUDIO_C [REPORT_JSON]
Uses the same predeclared budgets as compare.py. This extends the HTML corpus
with 384 extreme/full-length plans; it does not replace the independent oracle.
"""
import ctypes
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

import numpy as np

root = Path(__file__).resolve().parents[2]
baseline = Path(sys.argv[1]).resolve()
report = Path(sys.argv[2] if len(sys.argv) > 2 else '/tmp/echoy-baseline-comparison.json')
assert baseline.is_file(), baseline
rows = []
with tempfile.TemporaryDirectory(prefix='echoy-audio-baseline.') as directory:
    probes = []
    for index, source in enumerate((baseline, root / 'projects/echoy/main/echoy_audio.c')):
        library = Path(directory) / f'probe-{index}.so'
        command = [os.environ.get('CC', 'cc'), '-std=c11', '-O2', '-shared', '-fPIC']
        command += shlex.split(os.environ.get('AUDIO_TEST_CFLAGS', ''))
        command += ['-I' + str(root / 'projects/echoy/main'), str(root / 'tests/audio/baseline_probe.c'),
                    str(source), '-lm', '-o', str(library)]
        subprocess.run(command, check=True)
        function = ctypes.CDLL(str(library)).echoy_audio_baseline_probe
        function.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(ctypes.c_float)]
        function.restype = ctypes.c_int
        probes.append(function)
    for kind in (3, 4, 5):
        for note in range(128):
            signals = []
            for function in probes:
                pcm = np.empty(60000, dtype=np.float32)
                assert function(kind, note, pcm.ctypes.data_as(ctypes.POINTER(ctypes.c_float))) == 0
                assert np.all(np.isfinite(pcm))
                signals.append(pcm.astype(float))
            expected, actual = signals
            norm = np.linalg.norm(expected)
            if norm < 1e-12:
                row = dict(kind=kind, note=note, silent=True, passed=bool(np.linalg.norm(actual) < 1e-12))
            else:
                error = np.linalg.norm(actual - expected) / norm
                rms = np.linalg.norm(actual) / norm - 1
                peak = np.max(np.abs(actual)) / np.max(np.abs(expected)) - 1
                spectrum = np.abs(np.fft.rfft(expected))
                spectral = np.linalg.norm(np.abs(np.fft.rfft(actual)) - spectrum) / np.linalg.norm(spectrum)
                threshold = np.max(np.abs(expected)) * 1e-4
                onset = int(np.argmax(np.abs(actual) > threshold)) - int(np.argmax(np.abs(expected) > threshold))
                passed = abs(rms) <= .005 and abs(peak) <= .02 and error <= .03 and spectral <= .015 and abs(onset) <= 1
                row = dict(kind=kind, note=note, rms_ratio=float(rms), peak_ratio=float(peak),
                           relative_error=float(error), spectral_error=float(spectral), onset_samples=onset, passed=bool(passed))
            rows.append(row)
            if not row['passed']:
                print('FAIL', json.dumps(row))
report.write_text(json.dumps(dict(baseline=str(baseline), results=rows), indent=2) + '\n')
failed = sum(not row['passed'] for row in rows)
print(f'{len(rows)-failed}/{len(rows)} full-length MIDI/kind cases within the original sound budgets; report: {report}')
sys.exit(bool(failed))
