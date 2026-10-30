#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Development-only incremental build of a previously extracted private fixture."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import sys
from native_build import NativeBuild
from cook import emit,US_SHA1

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--stage',type=Path,required=True)
args=parser.parse_args()
stage=args.stage.resolve()
app=stage.parent.parent
assert stage.parent.name=='.sm64-cache' and stage.name.startswith('working-')
work=stage/'source'
assert hashlib.sha1((work/'baserom.us.z64').read_bytes()).hexdigest()==US_SHA1
builder=NativeBuild(app)
os.environ.update(PATH=str(app/'runtime/compiler/wrappers')+':'+str(app/'runtime/bin')+':/usr/bin:/bin',
    PYTHONHOME=str(app/'runtime/python/usr'),LILSMOKE_APP_DIR=str(app),LC_ALL='C',MAKEFLAGS='-j1')
for name in ('level_rules.mk','include/level_headers.h'):
    (work/'build/original-mips'/name).unlink(missing_ok=True)
game,extracted=builder.compile(stage,work,10679837,emit,lambda:False)
print('DEV_COOK_GAME='+str(game),flush=True)
