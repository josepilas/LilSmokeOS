#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Generate the bounded, exact short-span division adapter (no float changes)."""
import argparse
import difflib
from pathlib import Path

BEFORE = '''/* Full spans use constant division, preserving C truncation for negatives.
 * MIPS compilers can lower this branch to shifts instead of repeated DIV. */
static inline int opt_span_div(int value, int count) {
    return count == OPT_STEP ? value / OPT_STEP : value / count;
}
'''

AFTER = '''/* Exact signed division for the bounded raster block size, not an
 * approximate perspective reciprocal. For magnitude a and divisor d >= 2,
 * floor(a * floor(2^32/d) / 2^32) is at most one below a/d. Correct that
 * one-unit error with the remainder, then restore C truncation toward zero.
 * Unsigned magnitude also handles INT_MIN without signed overflow. */
static inline int opt_span_div(int value, int count) {
    static const uint32_t reciprocal[16] = {
        0, 0, 2147483648u, 1431655765u, 1073741824u, 858993459u,
        715827882u, 613566756u, 536870912u, 477218588u, 429496729u,
        390451572u, 357913941u, 330382099u, 306783378u, 286331153u
    };
    if (count == OPT_STEP) return value / OPT_STEP;
    if (count == 1) return value;
    const uint32_t sign = -(uint32_t)(value < 0);
    const uint32_t magnitude = ((uint32_t)value ^ sign) - sign;
    uint32_t quotient = (uint32_t)(((uint64_t)magnitude * reciprocal[count]) >> 32);
    quotient += magnitude - quotient * (uint32_t)count >= (uint32_t)count;
    return value < 0 ? -(int)quotient : (int)quotient;
}
'''


def transform(original):
    if original.count(BEFORE) != 1 or original.count('#define OPT_STEP 16 ') != 1:
        raise ValueError('Expected exact endpoint-reuse renderer with 16-pixel blocks')
    return original.replace(BEFORE, AFTER, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, required=True)
    args = parser.parse_args()
    original = args.reference.read_text()
    print(''.join(difflib.unified_diff(original.splitlines(True),
        transform(original).splitlines(True), 'a/src/pc/gfx/gfx_soft.c',
        'b/src/pc/gfx/gfx_soft.c')), end='')


if __name__ == '__main__':
    main()
