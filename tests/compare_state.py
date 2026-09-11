#!/usr/bin/env python3
"""Compare C replay with the frozen, hash-verified JS session oracle.

Only adapter identities (numeric UID/session/boot) and integer clock quantization
are normalized. Motifs retain all twelve JS event fields. Demo readiness timings
are injected by the C host replay; the firmware core never invents readiness.
"""
import json
import math
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
fixture = json.loads((root / 'tests/cases/session-trajectories.json').read_text())
actual = [json.loads(line) for line in subprocess.check_output([sys.argv[1]], text=True).splitlines()]
expected = []
for trajectory in fixture['trajectories']:
    for step in trajectory['steps']:
        snap = step['snapshot']
        devices = []
        for name, d in snap['devices'].items():
            session = snap['sessions'].get(d['sessionId'])
            devices.append(dict(
                role=d['role'], present=d['present'], lifecycle=d['lifecycle'],
                transport=d['transport'], activity=d['activity'],
                committedActivity=d['committedActivity'], phrase=d['phrase'],
                volume=d['volume'], motifAvailable=d['motifAvailable'],
                members=''.join(sorted(session['members'])) if session else '',
                leader=session['leader'] if session else '',
                discovered=''.join(sorted(d['discovered'])),
                pendingRole=snap['pending']['roles'].get(name),
                pendingPhrase=snap['pending']['phrases'].get(name, {}).get('value', 0),
            ))
        expected.append(dict(label=step['label'], at=step['at'], devices=devices,
                             edges=sorted(snap['committedEdges']),
                             sync=next(iter(snap['merges'].values()))['stage'] if snap['merges'] else 'none',
                             plan=step['plan']))

def compare(a, b, path=''):
    if isinstance(b, float):
        assert isinstance(a, (int, float)) and math.isclose(a, b, rel_tol=1e-9, abs_tol=2e-6 if path.endswith('.at') else 1e-9), (path, a, b)
    elif isinstance(b, dict):
        assert a.keys() == b.keys(), (path, a.keys(), b.keys())
        for key in b:
            compare(a[key], b[key], f'{path}.{key}')
    elif isinstance(b, list):
        assert len(a) == len(b), (path, len(a), len(b))
        for i, (av, bv) in enumerate(zip(a, b)):
            compare(av, bv, f'{path}[{i}]')
    else:
        assert a == b, (path, a, b)
compare(actual, expected)
print(f'state oracle: {fixture["trajectoryCount"]} trajectories / {len(expected)} checkpoints; all state fields and complete motifs match')
