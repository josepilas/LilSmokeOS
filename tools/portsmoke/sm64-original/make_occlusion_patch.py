#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Exact whole-block depth rejection for the public endpoint-reuse renderer."""
import argparse
import difflib
from pathlib import Path
import re

HELPER = '''/* Reject only when every sample fails the existing <= depth test. The
 * depth increment, integer truncation and equality rule are unchanged. A
 * visible block exits on its first passing sample; no auxiliary Z hierarchy,
 * allocation, draw reordering or approximate depth bound is needed. */
static inline bool opt_span_visible(const uint16_t *zb, int count, int zz, int dz) {
    if (!z_test) return true;
    for (int k = 0; k < count; ++k, zz += dz)
        if ((uint16_t)zz <= zb[k]) return true;
    return false;
}

'''


def transform(original):
    marker = '/* --- opaque textured (tex_rgb): v3 path, unchanged --- */'
    if original.count(marker) != 1 or 'opt_span_visible(' in original:
        raise ValueError('Expected unmodified public endpoint-reuse renderer')
    starts = list(re.finditer(r'static void opt_scan_\w+\(', original))
    if len(starts) != 12:
        raise ValueError('Expected exactly twelve fused scanners')
    text = original
    for index in range(len(starts) - 1, -1, -1):
        start = starts[index].start()
        end = starts[index + 1].start() if index + 1 < len(starts) else original.index('/* --- parameterized triangle walker')
        body = original[start:end]
        pixel = '        for (int k = cnt; k; --k) {\n'
        tail = re.search(r'        for \(int i = 2; i < \d+; \+\+i\) p\[i\] = q\[i\];', body)
        if body.count(pixel) != 1 or tail is None:
            raise ValueError('Unexpected scanner loop')
        begin = body.index(pixel)
        loop = body[begin:tail.start()]
        body = body[:begin] + '        if (opt_span_visible(zb, cnt, zz, dz)) {\n' + ''.join(
            '    ' + line if line.strip() else line for line in loop.splitlines(True)) + '''        } else {
            dst += cnt;
            zb += cnt;
        }
''' + body[tail.start():]
        text = text[:start] + body + text[end:]
    return text.replace(marker, HELPER + marker, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, required=True)
    args = parser.parse_args()
    original = args.reference.read_text()
    print(''.join(difflib.unified_diff(original.splitlines(True), transform(original).splitlines(True),
        'a/src/pc/gfx/gfx_soft.c', 'b/src/pc/gfx/gfx_soft.c')), end='')


if __name__ == '__main__':
    main()
