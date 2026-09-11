#!/usr/bin/env python3
"""Reject malformed/unbounded JSON without emitting a plausible motif."""
import json
import subprocess
import sys

runner = sys.argv[1]
base = dict(role='voice', phrase=1, activity=.56, bar=0, duplicate=0, bpm=96, motif=None)
valid = json.dumps(base, separators=(',', ':'))
invalid = [valid.replace('"bpm":96', '"bpm":+96'), valid.replace('"bpm":96', '"bpm":0x60'),
           valid.replace('"bpm":96', '"bpm":096'), valid.replace('"bpm":96', '"bpm":NaN'),
           valid.replace('"bpm":96', '"bpm":1e999'), valid[:-1], '{', '[', '"', '',
           valid+' garbage', valid+'{}', '['*20+']'*20, '"'+'x'*16380,
           json.dumps(dict(base, motif=[{}]*17)), json.dumps(dict(base, phrase=4)),
           json.dumps(dict(base, bpm=0)), json.dumps(dict(base, duplicate=3))]
invalid += [valid[:-1] + ',"unused":' + primitive + '}'
            for primitive in ['wat', 'NaN', '+1', '01', '1.', '1e', 'nul', 'TRUE', '[false,wat]', '{"x":NaN}']]
for value in invalid:
    result = subprocess.run([runner], input=value+'\n', text=True, capture_output=True)
    assert result.returncode == 2, f'expected rejection: {value[:120]!r}; code={result.returncode}; stderr={result.stderr}'
    assert not result.stdout
result = subprocess.run([runner], input=valid+'\n'+valid+'\n', text=True, capture_output=True)
assert result.returncode == 0 and len(result.stdout.splitlines()) == 2
print(f'PASS {len(invalid)} invalid runner inputs and JSON-line streaming')
