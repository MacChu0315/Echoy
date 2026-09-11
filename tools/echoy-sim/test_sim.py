#!/usr/bin/env python3
"""Host acceptance contracts; run with Python's standard unittest runner."""
import importlib.util
import json
from pathlib import Path
import unittest

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('sim', HERE / 'run.py')
sim = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sim)

class Contracts(unittest.TestCase):
    def test_reference_is_pinned_and_isolated(self):
        app = sim.Assets('reference')
        self.assertEqual(app.get('/')[0], sim.REFERENCE.read_bytes())
        for path in ['/../Sync/private', '/reference/../../HANDOFF.md', '/.git/config', '/etc/passwd', '/%2e%2e/Sync/a', '/reference/', '/api', '/run.py']:
            self.assertIsNone(app.get(path), path)
        page = sim.Assets('firmware').get('/')[0].decode()
        self.assertIn('firmware.js', page)
        self.assertNotIn('class DuijuEngine', page)
        self.assertNotIn('class DuijuSynth', page)
        self.assertNotIn('class DuijuView', page)
        self.assertIn('class DuijuGuide', page)
        self.assertIn('class DuijuDesk', page)

    def test_bridge_validation(self):
        for body in [b'{', b'[]', b'{"op":"tick","ms":1e100}', b'{"op":"tick","ms":true}', b'{"op":"touch","id":"D","phase":0,"x":1,"y":1}', b'{"op":"fault","id":"A","name":"shell","value":";rm"}', b'{"op":"tick","ms":1,"x":0}', b'{"op":"tick","ms":1,"ms":2}', b'x'*8193]:
            with self.assertRaises(ValueError, msg=body[:100]):
                sim.parse_request(body)

    def test_three_real_instances_and_revision(self):
        with sim.Bridge() as bridge:
            self.assertEqual(len({p.process.pid for p in bridge.runners}), 3)
            answer = bridge.request({'op':'snapshot'})
            self.assertEqual(len(answer['devices']), 3)
            for d in answer['devices']:
                self.assertEqual(d['revision'], d['frame_revision'])
                self.assertEqual(d['revision'], d['audio_revision'])
                self.assertTrue(d['frame'].startswith('data:image/png;base64,'))
            self.assertEqual(answer['devices'][0]['transport'], 'paused')
            self.assertFalse(answer['devices'][2]['present'])
        self.assertTrue(all(p.process.poll() is not None for p in bridge.runners))

    def test_all_scenarios(self):
        result = sim.scenarios()
        self.assertTrue(result['passed'])
        self.assertGreaterEqual(len(result['checks']), 20)

if __name__ == '__main__':
    unittest.main()
