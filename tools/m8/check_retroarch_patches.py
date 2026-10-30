"""Verify the complete patch series against the pinned RetroArch source."""
import argparse
import difflib
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--archive', type=Path, required=True)
p.add_argument('--normalize', action='store_true', help='mechanically normalize patches 6/7 context')
p.add_argument('--emit-sdl', type=Path, help='refresh the generated build copy of SDL2 driver source')
p.add_argument('--emit-menu', type=Path, help='refresh the generated menu driver source')
a = p.parse_args()
package = Path(__file__).resolve().parents[2] / 'package/retroarch-mips'
with tempfile.TemporaryDirectory(prefix='retroarch-patches-') as folder:
    root = Path(folder)
    with tarfile.open(a.archive) as source:
        source.extractall(root, filter='data')
    tree = next(root.iterdir())
    for patch in sorted(package.glob('[0-9][0-9][0-9][0-9]-*.patch')):
        content = patch.read_text()
        if a.normalize and patch.name.startswith('0008'):
            lines = content.splitlines(True)
            for i, line in enumerate(lines):
                match = re.match(r'@@ -(\d+)(?:,\d+)? \+(\d+)(?:,\d+)? @@(.*)', line)
                if not match: continue
                old = new = 0
                for body in lines[i + 1:]:
                    if body.startswith(('@@ ', '--- a/', '+++ b/')): break
                    if body.startswith(' '): old += 1; new += 1
                    elif body.startswith('-'): old += 1
                    elif body.startswith('+'): new += 1
                    else: break
                lines[i] = f'@@ -{match[1]},{old} +{match[2]},{new} @@{match[3]}\n'
            content = ''.join(lines)
        paths = re.findall(r'^--- a/(.+)$', content, re.M)
        before = {path: (tree / path).read_text() for path in paths}
        normalize = a.normalize and patch.name.startswith(('0006', '0007', '0008'))
        subprocess.run(['patch', '-p1', '--batch', '--fuzz=' + ('2' if normalize else '0')],
                       cwd=tree, input=content, text=True, check=True)
        if normalize:
            header = content.split('--- a/', 1)[0]
            for path in paths:
                header += ''.join(difflib.unified_diff(before[path].splitlines(True),
                    (tree / path).read_text().splitlines(True), 'a/' + path, 'b/' + path))
            patch.write_text(header)
    if a.emit_sdl:
        shutil.copyfile(tree / 'gfx/drivers/sdl2_gfx.c', a.emit_sdl)
    if a.emit_menu:
        shutil.copyfile(tree / 'menu/menu_driver.c', a.emit_menu)
print('PASS: complete pinned RetroArch patch series, ' + ('normalized context' if a.normalize else 'zero fuzz'))
