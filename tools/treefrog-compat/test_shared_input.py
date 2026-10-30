# SPDX-License-Identifier: MPL-2.0
import argparse,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
ui=ROOT/'package/lilsmoke-ui/src/src'
cflags=subprocess.check_output(['pkg-config','--cflags','sdl2'],text=True).split()
libs=subprocess.check_output(['pkg-config','--libs','sdl2'],text=True).split()
binary=a.out/'shared-input-stability'
subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I'+str(ui),*cflags,str(ROOT/'tools/treefrog-compat/shared-input-stability-test.cpp'),*[str(ui/n)for n in('Input.cpp','Keymap.cpp','Log.cpp')],'-o',str(binary),*libs],check=True)
subprocess.run([str(binary)],check=True,timeout=10)
