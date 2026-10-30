# SPDX-License-Identifier: MPL-2.0
from pathlib import Path
import tempfile
import unittest

from audit_completed_progress import reconcile

RULE = 'cc -E -x c -o build/original-mips/level_rules.mk levels/level_rules.mk\n'
TEXTURE = 'tools/n64graphics -i build/original-mips/example.rgba16 -g example.png\n'


class ProgressAudit(unittest.TestCase):
    def run_audit(self, plan, build):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            (root / 'plan').write_text(plan)
            (root / 'build').write_text(build)
            return reconcile(root / 'plan', root / 'build')

    def test_exact(self):
        self.assertTrue(self.run_audit(TEXTURE, TEXTURE)['plan_matches'])

    def test_only_known_parse_time_include_is_normalized(self):
        value = self.run_audit(RULE + TEXTURE, TEXTURE)
        self.assertTrue(value['plan_matches'])
        self.assertEqual(value['included_makefile_tasks_executed_during_planning'], 1)
        self.assertIsNone(value['displayed_plan_reconciliation']['percent'])
        self.assertEqual(value['completed'], 1)

    def test_unrelated_missing_generation_is_not_forgiven(self):
        with self.assertRaises(ValueError):
            self.run_audit('cc -E -o build/original-mips/other.h other.c\n' + TEXTURE, TEXTURE)

    def test_missing_real_work_still_fails_reconciliation(self):
        value = self.run_audit(RULE + TEXTURE + 'cc -c -o work.o work.c\n', TEXTURE)
        self.assertFalse(value['plan_matches'])
        self.assertIsNone(value['percent'])


if __name__ == '__main__':
    unittest.main()
