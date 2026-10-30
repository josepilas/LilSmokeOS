"""Private VM QMP controls; never sends input to Windows or another desktop."""
import argparse
from pathlib import Path
import sys
import time

P = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(P / 'tools/portsmoke/sm64'))
from vm_scene_smoke import Qmp
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('directory', type=Path)
ap.add_argument('keys', nargs='*', choices=('home', 'up', 'down', 'left', 'right', 'ret', 'ctrl', 'alt', 'f12'))
ap.add_argument('--shot', default='current')
ap.add_argument('--stop', action='store_true')
a = ap.parse_args()
Q = a.directory.resolve()
if not str(Q).startswith('/root/rgui-state-menu-') or not a.shot.replace('-', '').replace('_','').isalnum():
    ap.error('private RGUI fixture path/shot required')
qmp = Qmp(Q / 'qmp.sock')
try:
    for key in a.keys:
        qmp.key(key)
        time.sleep(.4)
    time.sleep(.8)
    qmp.call('screendump', dict(filename=str(Q / (a.shot + '.ppm'))))
finally:
    qmp.close()
if a.stop:
    (Q / 'stop').touch()
