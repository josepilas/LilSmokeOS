# SPDX-License-Identifier: MPL-2.0
import unittest
from reuse_vm_compiles import ASM, FIXED_ASM, BUILDER_CALL, RECOVERY, validate_delta, validate_recovery_delta


class BoundedDelta(unittest.TestCase):
    def test_only_exact_header_flag_is_accepted(self):
        validate_delta(b'unchanged\n' + ASM + b'\n', b'unchanged\n' + FIXED_ASM + b'\n')

    def test_c_flags_or_other_code_changes_are_rejected(self):
        for before, after in ((b'-O2\n', b'-O0\n'), (b'code\n', b'changed\n')):
            with self.assertRaises(ValueError):
                validate_delta(before + ASM, after + FIXED_ASM)

    def test_duplicate_rule_or_missing_flag_is_rejected(self):
        for old, new in ((ASM, ASM), (ASM * 2, FIXED_ASM * 2), (b'', FIXED_ASM)):
            with self.assertRaises(ValueError): validate_delta(old, new)

    def test_only_exact_uncommitted_game_cleanup_is_accepted(self):
        old = b'unchanged\n' + BUILDER_CALL
        validate_recovery_delta(old, b'unchanged\n' + RECOVERY + BUILDER_CALL)
        for new in (old, b'changed\n' + RECOVERY + BUILDER_CALL,
                    b'unchanged\n' + RECOVERY.replace(b'regular(', b'unchecked(') + BUILDER_CALL):
            with self.assertRaises(ValueError): validate_recovery_delta(old, new)

    def test_real_cook_delta_is_bounded(self):
        from pathlib import Path
        current = Path(__file__).with_name('cook.py').read_bytes()
        self.assertEqual(current.count(RECOVERY), 1)
        validate_recovery_delta(current.replace(RECOVERY, b'', 1), current)
