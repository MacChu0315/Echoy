#!/usr/bin/env python3
"""Compare native output with committed expectations, never calculate an oracle."""
import argparse
import json
import math
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FLOAT_FIELDS = {'vel', 'dur', 'expression', 'maxDuration'}

def compare_events(expected, actual, fields, context):
    assert len(actual) == len(expected), f'{context}; event count expected={len(expected)} actual={len(actual)}'
    for event_index, (want, got) in enumerate(zip(expected, actual)):
        assert len(got) == len(fields), f'{context}; event={event_index}: field count'
        for field, a, b in zip(fields, want, got):
            equal = (type(b) in (int, float) and math.isfinite(b) and abs(a-b) <= 1e-6) if field in FLOAT_FIELDS and a is not None else type(a) is type(b) and a == b
            assert equal, f'{context}; event={event_index} field={field} expected={a!r} actual={b!r}'

def run_cases(runner, inputs):
    result = subprocess.run([runner], input=''.join(json.dumps(i, separators=(',', ':'))+'\n' for i in inputs), text=True, capture_output=True)
    assert result.returncode == 0, f'runner exit {result.returncode}: {result.stderr}'
    rows = [json.loads(line) for line in result.stdout.splitlines()]
    assert len(rows) == len(inputs)
    return rows

def compare_edges(runner):
    data = json.loads((ROOT / 'tests/cases/music-edges.json').read_text())
    fields = data['eventFields']
    actual = run_cases(runner, [c['input'] for c in data['cases']])
    for c, got in zip(data['cases'], actual):
        compare_events(c['events'], got, fields, f"boundary input={c['input']}")
    previous = [None] * len(data['chains'])
    for stage in range(3):
        inputs = [dict(c['steps'][stage]['input'], motif=motif) for c, motif in zip(data['chains'], previous)]
        actual = run_cases(runner, inputs)
        for i, (c, got) in enumerate(zip(data['chains'], actual)):
            compare_events(c['steps'][stage]['events'], got, fields, f"chain={c['roles']} phrase={c['phrase']} bar={c['bar']} event stage={stage}")
            previous[i] = [{key: value for key, value in zip(fields, event) if value is not None} for event in got]
    print(f"PASS {len(data['chains'])} native three-device chains / {len(data['cases'])} boundary cases")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--runner', required=True)
    parser.add_argument('--scope', choices=['defaults', 'solo', 'response', 'all'], default='all')
    args = parser.parse_args()
    data = json.loads((ROOT / 'tests/cases/music-fixtures.json').read_text())
    cases = data['cases']
    defaults = data['dimensions']['activitySamples']['defaults']
    if args.scope == 'defaults':
        cases = [c for c in cases if c[2] == defaults[c[0]] and c[3:6] == [0, 0, 0]]
    elif args.scope == 'solo':
        cases = [c for c in cases if c[0] != 'harmony' and c[4] < 2]
    elif args.scope == 'response':
        cases = [c for c in cases if c[0] != 'harmony']
    inputs = [dict(role=c[0], phrase=c[1], activity=c[2], bar=c[3], duplicate=c[5], bpm=data['bpm'],
                   motif=data['dimensions']['leftMotifs'][c[4]]['motif']) for c in cases]
    result = subprocess.run([args.runner], input=''.join(json.dumps(i, separators=(',', ':'))+'\n' for i in inputs), text=True, capture_output=True)
    if result.returncode:
        raise AssertionError(f'runner exit {result.returncode}: {result.stderr}')
    lines = result.stdout.splitlines()
    assert len(lines) == len(cases), f'expected {len(cases)} output lines, got {len(lines)}'
    events = 0
    for index, (c, line) in enumerate(zip(cases, lines)):
        actual = json.loads(line)
        expected = c[6]
        context = f'input={json.dumps(inputs[index])}; role={c[0]} bar={c[3]}'
        compare_events(expected, actual, data['eventFields'], context)
        events += len(expected)
    print(f'PASS {len(cases)} cases / {events} events ({args.scope}), absolute tolerance 1e-6')
    if args.scope == 'all':
        compare_edges(args.runner)

if __name__ == '__main__':
    main()
