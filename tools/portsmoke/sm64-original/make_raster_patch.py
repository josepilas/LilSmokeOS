"""Print a reviewed mechanical diff over the pinned private renderer; no file edits."""
import difflib
import json
from pathlib import Path
import re
import argparse

HERE=Path(__file__).resolve().parent
source=Path(json.loads((HERE/'_build/private-workspace.json').read_text())['source'])
assert source.parent==Path('/root') and source.name.startswith('lilsmoke-sm64-original-private-')
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reference',type=Path,required=True,help='private renderer before patch 0007')
file=parser.parse_args().reference
original=file.read_text()
assert '#define OPT_STEP 16' in original
assert 'opt_span_div' not in original
text=original
functions=('tex_rgb','tex_rgba','tex_fog','tex_fog_rgb','tex','tex_rgba_texa','tex_fog_rgba','tex_rgb_rgb')
for name in functions:
    start=text.index('static void opt_scan_'+name+'(')
    next_function=text.index('\nstatic void ',start+1)
    body=text[start:next_function]
    loop='        for (int k = cnt; k; --k) {\n'
    guard='            if (!z_test || (uint16_t)zz <= *zb) {\n'
    assert body.count(loop)==1 and body.count(guard)==1,name
    at=body.index(loop)+len(loop)
    check=body.index(guard)
    assert check>at,name
    sampler=body[at:check]
    assert 'tpix[' in sampler and '++dst' not in sampler,name
    # Relocate the unchanged depth predicate ahead of pure texture reads.
    body=body[:at]+guard+''.join('    '+line if line.strip() else line for line in sampler.splitlines(True))+body[check+len(guard):]
    text=text[:start]+body+text[next_function:]
pattern=r'\(([a-z][a-z0-9]*) - ([a-z][a-z0-9]*)\) / cnt'
text,count=re.subn(pattern,r'opt_span_div(\1 - \2, cnt)',text)
assert count==73,count
marker='/* --- opaque textured (tex_rgb): v3 path, unchanged --- */'
helper='''/* Full spans use constant division, preserving C truncation for negatives.
 * MIPS compilers can lower this branch to shifts instead of repeated DIV. */
static inline int opt_span_div(int value, int count) {
    return count == OPT_STEP ? value / OPT_STEP : value / count;
}

'''
assert marker in text
text=text.replace(marker,helper+marker,1)
print(''.join(difflib.unified_diff(original.splitlines(True),text.splitlines(True),
    'a/src/pc/gfx/gfx_soft.c','b/src/pc/gfx/gfx_soft.c')),end='')
