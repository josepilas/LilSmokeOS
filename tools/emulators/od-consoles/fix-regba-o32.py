"""Mechanical ABI repair of the pinned generated ReGBA assembly copy."""
from pathlib import Path
import re
import sys

root=Path(sys.argv[1]).resolve()
path=root/'source/mips/stub.S'
text=path.read_text()
marker='# LilSmoke: o32 C calls reserve argument homes and aligned frames.\n'
if marker not in text:
    # o32 callees may spill a0..a3 into the caller's 16-byte argument area.
    # Upstream kept ra there instead; current GCC overwrote it with the ARM PC.
    sizes={4:24,8:24,12:32}
    def frame(match):
        value=int(match[2]);sign=-1 if value<0 else 1
        return match[1]+str(sign*sizes[abs(value)])
    text,count=re.subn(r'(addiu\s+\$sp,\s*\$sp,\s*)(-?4|-?8|-?12)(?=\s|$)',frame,text)
    assert count>=30,('Unexpected pinned stack layout',count)
    def saved(match):
        offset=int(match[2] or '0')
        assert offset in (0,4,8),offset
        return match[1]+str(offset+16)+'($sp)'
    text,count=re.subn(r'((?:lw|sw)\s+\$(?:ra|4|5|6),\s*)(\d*)\(\$sp\)',saved,text)
    assert count>=35,('Unexpected pinned saved-register layout',count)
    path.write_text(marker+text)
else:
    assert not re.search(r'addiu\s+\$sp,\s*\$sp,\s*-?(?:4|8|12)(?=\s|$)',text)
print('ReGBA o32 assembly frames verified')
