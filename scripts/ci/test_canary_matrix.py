import copy
import sys
from pathlib import Path
import unittest

import canary_matrix
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'parity'))
import canary_plan


class CanaryMatrixTests(unittest.TestCase):
    def test_groups_indivisible_and_escalation_preserved(self):
        plan = canary_plan.plan(canary_plan._SELF_TEST_TABLE, mode='touched',
                                changed_files=['scripts/parity/canary_plan.py'])
        manifest = canary_plan.shard_manifest(plan, 2)
        self.assertTrue(manifest['all_required'])
        self.assertEqual(manifest['rows'], ['row_one', 'row_two', 'row_three'])
        self.assertIn(['row_one', 'row_two'], [s['rows'] for s in manifest['shards']])
        self.assertEqual(sum(len(s['groups']) for s in manifest['shards']), 2)

    def test_build_and_transport_machinery_force_full_selection(self):
        for path in ('flake.nix', 'flake.lock', 'nix/native-ci.nix', 'CMakePresets.json',
                     'scripts/ci/nix_bundle.py', 'scripts/ci/canary_matrix.py',
                     'tests/fixtures/scenario99.py'):
            with self.subTest(path=path):
                plan = canary_plan.plan(canary_plan._SELF_TEST_TABLE, mode='touched',
                                        changed_files=[path])
                self.assertTrue(plan.all_required)
                self.assertEqual(plan.rows, ('row_one', 'row_two', 'row_three'))

    def test_aggregate_rejects_missing_duplicate_and_toothless(self):
        plan = canary_plan.plan(canary_plan._SELF_TEST_TABLE, mode='all')
        manifest = canary_plan.shard_manifest(plan, 2)
        reports = [(s['id'], {'rows': [{'id': n, 'pred_flips': 1} for n in s['rows']],
                              'tallies': {'rows': len(s['rows']), 'zero_flips': 0,
                                          'predicate_toothless': 0}})
                   for s in manifest['shards']]
        self.assertEqual(canary_matrix.verify(manifest, reports), {'rows': 3, 'groups': 2, 'shards': 2})
        for bad in (reports[:1], reports+reports[:1]):
            with self.assertRaises(ValueError):
                canary_matrix.verify(manifest, bad)
        for field in ('missing', 'toothless'):
            bad = copy.deepcopy(reports)
            if field == 'missing': bad[0][1]['rows'].pop()
            else: bad[0][1]['rows'][0]['pred_flips'] = 0
            with self.assertRaises(ValueError):
                canary_matrix.verify(manifest, bad)


if __name__ == '__main__':
    unittest.main()
