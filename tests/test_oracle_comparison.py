#!/usr/bin/env python3
"""The numerical oracle must reject booleans and invalid numeric values."""
import unittest
from compare_oracle import compare_events

class ComparisonTests(unittest.TestCase):
    def test_numeric_fields_reject_boolean(self):
        for field in ('vel', 'dur', 'expression', 'maxDuration'):
            for value in (False, True):
                with self.subTest(field=field, value=value):
                    with self.assertRaises(AssertionError):
                        compare_events([[int(value)]], [[value]], [field], 'boolean')

    def test_numeric_tolerance_and_invalid_values(self):
        compare_events([[1.0]], [[1]], ['dur'], 'integer JSON number')
        compare_events([[1.0]], [[1.0000005]], ['dur'], 'within tolerance')
        for value in (1.000002, float('nan'), float('inf'), '1', None):
            with self.subTest(value=value):
                with self.assertRaises(AssertionError):
                    compare_events([[1.0]], [[value]], ['dur'], 'invalid number')

if __name__ == '__main__':
    unittest.main()
