"""Read-only diagnostics for the private QA VM, without host keyboard input."""
from pathlib import Path
import sys
import time
P = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(P/'tools/portsmoke/sm64'))
from vm_scene_smoke import Qmp
qmp = Qmp(Path(sys.argv[1]))
try:
    print(qmp.call('query-status'))
    if '--blocked-tasks' in sys.argv:
        print(qmp.call('send-key', {'keys': [{'type':'qcode','data':key} for key in ('alt','sysrq','w')], 'hold-time':100}))
        time.sleep(1)
    for _ in range(3):
        print(qmp.call('human-monitor-command', {'command-line':'info registers'}))
        time.sleep(.3)
finally:
    qmp.close()
