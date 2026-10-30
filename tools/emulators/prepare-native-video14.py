"""Generate Candidate14 runtime/payload without opening physical disks."""
from pathlib import Path
import sys
import shutil
import subprocess
import argparse
P=Path(__file__).resolve().parents[2]
B=Path('/root/lilsmoke-os/build/buildroot/output')
R=Path('/root/lilsmoke-emulator-expansion-20261008')
sys.path.insert(0,str(P/'tools/m8'))
from build_stock_candidate import prepare_runtime,build_display_adapter
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--generation',choices=['candidate14','candidate15'],default='candidate14')
generation=parser.parse_args().generation
payload=P/('build/m8-payload-'+generation)
payload.mkdir(parents=True,exist_ok=True)
assert not (payload/'files').exists(),'Refusing to replace an existing generated payload'
shutil.copyfile(R/'adapter/libod-sdl.so',R/'built-native/libod-sdl.so')
subprocess.run([str(B/'host/bin/mipsel-buildroot-linux-musl-strip'),str(R/'built-native/libod-sdl.so')],check=True)
shutil.copyfile(R/'built-native/libod-sdl.so',B/'target/usr/lib/lilsmoke/libod-sdl.so')
shutil.copyfile(P/'package/lilsmoke-scripts/src/lilsmoke-od-launch',B/'target/usr/lib/lilsmoke/lilsmoke-od-launch')
print(prepare_runtime(B/'target',payload))
shutil.copyfile(P/'board/segam-m8-v6-stock/m8-launch.sh',payload/'m8-launch.sh')
build_display_adapter(B,payload/'libm8-display.so')
print('Generated '+generation+' payload; no card written.')
