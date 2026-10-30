# SPDX-License-Identifier: MPL-2.0
import json
from pathlib import Path
import tempfile
import unittest

from export_preparation_update import export


class AcceptanceGuards(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in ('stage', 'native', 'vm', 'installer'):
            (self.root / name).mkdir()
        self.args = {name: self.root / name for name in
                     ('stage', 'native', 'vm', 'installer', 'legacy', 'out')}

    def report(self, name, value):
        (self.root / name / 'report.json').write_text(json.dumps(value))

    def test_failed_report_never_exports(self):
        self.report('native', dict(passed=False))
        with self.assertRaisesRegex(ValueError, 'Acceptance failed'):
            export(**self.args)
        self.assertFalse((self.root / 'out').exists())

    def test_boolean_success_without_real_pipeline_is_rejected(self):
        for name in ('native', 'vm', 'installer'):
            self.report(name, dict(passed=True))
        with self.assertRaisesRegex(ValueError, 'Incomplete acceptance'):
            export(**self.args)
        self.assertFalse((self.root / 'out').exists())


if __name__ == '__main__':
    unittest.main()
