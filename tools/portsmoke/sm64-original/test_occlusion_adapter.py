#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Fail-closed transformation tests; actual C equivalence uses test_span_div.py."""
from pathlib import Path
import unittest

from make_occlusion_patch import transform, HELPER


def fixture():
    header = '/* --- opaque textured (tex_rgb): v3 path, unchanged --- */\n'
    scanners = ''.join('''static void opt_scan_case%d(void) {
    while (n > 0) {
        const int dz = opt_span_div(zz1 - zz, cnt);
        for (int k = cnt; k; --k) {
            if (!z_test || (uint16_t)zz <= *zb) *dst = 1;
            ++dst; ++zb;
            zz += dz;
        }
        for (int i = 2; i < 9; ++i) p[i] = q[i];
        zz = zz1;
        n -= cnt;
    }
}
''' % i for i in range(12))
    return header + scanners + '/* --- parameterized triangle walker */\n'


class TransformTests(unittest.TestCase):
    def test_all_twelve_paths_and_endpoint_progress_retained(self):
        result = transform(fixture())
        self.assertEqual(result.count('if (opt_span_visible(zb, cnt, zz, dz))'), 12)
        self.assertEqual(result.count('dst += cnt;'), 12)
        self.assertEqual(result.count('zb += cnt;'), 12)
        self.assertEqual(result.count('p[i] = q[i];'), 12)
        self.assertEqual(result.count('zz = zz1;'), 12)
        self.assertEqual(result.count('n -= cnt;'), 12)
        self.assertEqual(result.count(HELPER), 1)

    def test_unknown_or_already_patched_source_rejected(self):
        for source in ('', fixture().replace('v3 path', 'unknown'), transform(fixture()),
                       fixture().replace('static void opt_scan_case11', 'static void other')):
            with self.subTest(source=source[:40]), self.assertRaises(ValueError):
                transform(source)

    def test_changed_pixel_loop_rejected(self):
        with self.assertRaises(ValueError):
            transform(fixture().replace('k = cnt', 'k = other', 1))

    def test_changed_endpoint_update_rejected(self):
        with self.assertRaises(ValueError):
            transform(fixture().replace('p[i] = q[i];', 'p[i] += q[i];', 1))


if __name__ == '__main__':
    unittest.main()
