"""Build a self-contained native test application; no proprietary data."""
import argparse,json,shutil,subprocess,zipfile
from pathlib import Path
P=Path(__file__).resolve().parents[2]
B=Path('/root/lilsmoke-os/build/buildroot/output')
Q=Path('/root/lilsmoke-applications-20261009')
demo=P/'tools/apps/demo';output=P/'build/lilsmoke-applications'
parser=argparse.ArgumentParser();parser.add_argument('--versions',nargs='+',default=['1.2.0','1.3.0']);args=parser.parse_args()
subprocess.run(['python3',str(P/'tools/apps/render_demo_icon.py')],check=True)
output.mkdir(exist_ok=True);Q.mkdir(exist_ok=True)
binary=Q/'button-tester'
subprocess.run([str(B/'host/bin/mipsel-buildroot-linux-musl-gcc'),'-O2','-Wall','-Wextra','-Werror',
               '-I'+str(B/'staging/usr/include/SDL2'),str(demo/'button-tester.c'),'-lSDL2_ttf','-lSDL2','-o',str(binary)],check=True)
subprocess.run([str(B/'host/bin/mipsel-buildroot-linux-musl-strip'),str(binary)],check=True)
libs={}
for name in ['libSDL2-2.0.so.0','libSDL2_ttf-2.0.so.0']:
    libs['runtime/lib/'+name]=(B/'target/usr/lib'/name).resolve()
licenses={
    'runtime/licenses/SDL2.txt':next((B/'build').glob('sdl2-*/LICENSE.txt')),
    'runtime/licenses/SDL2_ttf.txt':next((B/'build').glob('sdl2_ttf-*/LICENSE.txt')),
    'runtime/licenses/DejaVu.txt':B/'build/dejavu-2.37/LICENSE'
}
font=B/'target/usr/share/fonts/DejaVuSans.ttf'
if not font.is_file():
    choices=list((B/'target/usr/share/fonts').rglob('DejaVuSans.ttf'))
    assert choices,'missing target font';font=choices[0]
for index,version in enumerate(args.versions):
    meta=json.loads((demo/'meta.json').read_text());meta['version']=version
    meta['icon']='icon.png'
    path=output/('Button-Tester-'+version+'.lilpck')
    with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        archive.writestr('meta.json',json.dumps(meta,indent=2)+'\n')
        archive.write(demo/'run.sh','run.sh' if index==0 else 'launch.sh')
        archive.write(demo/'icon.png','icon.png');archive.write(demo/'LICENSE.lucide','runtime/licenses/Lucide.txt')
        archive.write(binary,'runtime/bin/button-tester');archive.write(font,'runtime/assets/font.ttf')
        archive.write(P/'package/lilsmoke-apps/src/LICENSE','LICENSE')
        for name,file in libs.items():archive.write(file,name)
        for name,file in licenses.items():archive.write(file,name)
    print(path)
