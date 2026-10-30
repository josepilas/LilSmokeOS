#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Real converted game via production Apps install/run in 128 MiB.
HC display/audio boundary is a QA capture driver. UI tested separately.
"""
import argparse,gzip,hashlib,json,os,re,shutil,subprocess,sys,tempfile,time,zipfile
from pathlib import Path
P=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(P/'tools/hwtest'));sys.path.insert(0,str(P/'tools/portsmoke/sm64'))
from mips_boot_smoke import Newc
from mips_disk_fixture import overlay_initramfs,disk_devices,runtime_libraries
SDK=Path('/root/lilsmoke-os/build/buildroot/output')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def native_stock(package,source,out):
    """Private, minimal original MIPS dependency closure, never published."""
    root=out/'stock-runtime';(root/'lib').mkdir(parents=True);(root/'usr/lib').mkdir(parents=True)
    probe=out/'elf-inputs';probe.mkdir();programs=[]
    with zipfile.ZipFile(package)as z:
        for index,entry in enumerate(z.infolist()):
            if entry.is_dir()or entry.file_size>32*1024*1024:continue
            with z.open(entry)as stream:header=stream.read(20)
            if header[:7]!=b'\x7fELF\x01\x01\x01':continue
            path=probe/(str(index)+'-'+Path(entry.filename).name);path.write_bytes(z.read(entry));programs.append(path)
    shutil.copyfile(source/'lib/ld.so.1',root/'lib/ld.so.1')
    pending=list(programs);seen=set();readelf=str(SDK/'host/bin/mipsel-buildroot-linux-musl-readelf')
    while pending:
        program=pending.pop();dynamic=subprocess.check_output([readelf,'-d',str(program)],text=True)
        for name in re.findall(r'\(NEEDED\).*\[([^]]+)\]',dynamic):
            if not re.fullmatch(r'[A-Za-z0-9_.+-]{1,127}',name):raise ValueError('Nonportable dependency name in QA input')
            if name in seen:continue
            seen.add(name)
            bundled=next((p for p in programs if p.name.split('-',1)[1]==name),None)
            if bundled is not None:pending.append(bundled);continue
            relative=None
            for folder in ('lib','usr/lib'):
                candidate=source/folder/name
                if candidate.is_file():
                    with candidate.open('rb')as f:header=f.read(20)
                    if header[:7]==b'\x7fELF\x01\x01\x01'and header[18:20]==b'\x08\x00':relative=Path(folder)/name;break
            if relative is None:raise RuntimeError('Stock MIPS dependency missing: '+name)
            shutil.copyfile(source/relative,root/relative);pending.append(source/relative)
    cc=str(SDK/'host/bin/mipsel-buildroot-linux-musl-gcc')
    subprocess.run([cc,'-O2','-shared','-fPIC','-nostdlib','-mips32r2','-mabi=32','-mfp32','-mhard-float','-Wl,--hash-style=sysv',str(P/'tools/treefrog-compat/qa_driver.c'),'-o',str(root/'driver.so')],check=True)
    return root
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--package',type=Path,required=True);ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--helpers', type=Path, default=P/'tools/treefrog-compat/_build/mips-20261029-compat02')
    ap.add_argument('--native',action='store_true');ap.add_argument('--direct',action='store_true')
    ap.add_argument('--exit-hotkey',action='store_true');ap.add_argument('--stock-source',type=Path)
    a=ap.parse_args()
    a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    helpers=a.helpers.resolve(strict=True)
    snapshot=P/'tools/daedalus-lilsmoke/_build/sdk-gui-snapshot-20261028'
    stock=P/'tools/treefrog-compat/_build/runtime-root-app-20261029/stock-runtime'
    with zipfile.ZipFile(a.package)as z:
        if a.direct:
            configs=[n for n in z.namelist()if n.endswith('/config.json')]
            if len(configs)!=1:raise ValueError('Direct QA needs exactly one metadata-backed port')
            config=json.loads(z.read(configs[0]));meta={'name':config['name']};port_id=config['id']
        else:meta=json.loads(z.read('meta.json'));port_id=json.loads(z.read('adapter.json'))['port_id']
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]{0,63}',port_id):raise ValueError('Unsafe QA port ID')
    if a.stock_source:stock=native_stock(a.package,a.stock_source,a.out)
    cc=str(SDK/'host/bin/mipsel-buildroot-linux-musl-gcc')
    combo=a.native or a.exit_hotkey
    pad_source=P/('tools/treefrog-compat/qa_pad.c'if combo else'tools/treefrog-converter/tests/vm_pad.c')
    subprocess.run([cc,'-std=c11','-O2','-I'+str(P/'package/lilsmoke-ui/src/src'),str(pad_source),'-o',str(a.out/'vm-pad')],check=True)
    report=dict(passed=False,cpu='74Kf',cpus=1,ram_mib=128,gpu=False,physical_device=False,actual_core=not a.native,actual_native=a.native,actual_ui_navigation=False,
        launch='production Apps install/run, root launcher and compatibility helpers',device_boundary='QA HC frame/PCM capture',package_sha256=sha(a.package))
    report['helper_sha256']={n:sha(helpers/n) for n in ('lilsmoke-treefrog-check','lilsmoke-treefrog-input')}
    report['launcher_sha256']=sha(P/'package/lilsmoke-scripts/src/lilsmoke-treefrog-launch')
    with tempfile.TemporaryDirectory(prefix='converted-port-vm-')as t:
        work=Path(t);bench=work/'bench';bench.mkdir();shutil.copyfile(a.package,bench/'game.lilpck')
        disk=work/'storage.fat'
        with disk.open('xb')as f:f.truncate(384*1024*1024)
        subprocess.run(['mkfs.fat','-F','32',str(disk)],check=True,capture_output=True)
        if a.direct:
            stage=work/'stage/treefrog-ports';stage.mkdir(parents=True)
            with zipfile.ZipFile(a.package)as z:
                for item in z.infolist():
                    parts=Path(item.filename).parts
                    if item.filename.startswith('/')or'..'in parts or'\\'in item.filename or item.file_size>32*1024*1024 or(item.external_attr>>16)&0o170000==0o120000:raise ValueError('Unsafe direct QA archive')
                    if not item.is_dir():
                        target=stage/item.filename;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(z.read(item))
            subprocess.run(['mcopy','-s','-i',str(disk),str(stage),'::/'],check=True)
        ov=Newc();bins=[]
        for target,source in [('usr/lib/lilsmoke/lilsmoke-apps',snapshot/'usr/lib/lilsmoke/lilsmoke-apps'),
            ('usr/lib/lilsmoke/lilsmoke-treefrog-check',helpers/'lilsmoke-treefrog-check'),
            ('usr/lib/lilsmoke/lilsmoke-treefrog-input',helpers/'lilsmoke-treefrog-input'),('usr/bin/vm-pad',a.out/'vm-pad')]:
            ov.file(target,source.read_bytes(),0o755);bins.append(source)
        for name in ('lilsmoke-treefrog-launch','lilsmoke-treefrog-port-check','treefrog-keymap.txt'):
            ov.file('usr/lib/lilsmoke/'+name,(P/'package/lilsmoke-scripts/src'/name).read_bytes(),0o755)
        runtime_libraries(SDK,ov,bins)
        init='''#!/bin/sh
exec </dev/console >/dev/console 2>&1
mkdir -p /bench /storage /lilos/cubegm /persist/logs /dev/pts /dev/shm /run
mount -t devpts devpts /dev/pts
mount -t tmpfs -o size=4m tmpfs /dev/shm
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || poweroff -f
mount -t 9p -o trans=virtio,version=9p2000.L,ro stock /lilos/cubegm || poweroff -f
mount -t vfat -o rw,umask=0077 /dev/vdb /storage || poweroff -f
mkdir -p /storage/independent-apps /storage/treefrog-ports
export LILSMOKE_HCFB=0
fbset -g 640 480 640 480 16
/usr/lib/lilsmoke/lilsmoke-apps install /bench/game.lilpck HASH > /bench/install.log 2>&1
echo INSTALL_EXIT=$?
/usr/bin/vm-pad > /bench/pad.log 2>&1 &
pad=$!
sleep 0.1
/usr/lib/lilsmoke/lilsmoke-apps run 'APP_NAME' > /bench/game.log 2>&1 &
app=$!
(
  while kill -0 "$app" 2>/dev/null; do
    if [ -s '/storage/independent-apps/APP_NAME/runtime/runtime-capabilities.json' ]; then sleep 1; kill -TERM "$app"; break; fi
    for status in /proc/[0-9]*/status; do
      if grep -q '^Name:.*lilretro' "$status" 2>/dev/null; then cat "$status" >> /bench/memory.log; fi
    done
    sleep 0.2
  done
) &
watcher=$!
wait "$app"; echo APP_EXIT=$?
wait "$watcher"
kill -TERM "$pad"; wait "$pad"
cp '/storage/independent-apps/APP_NAME/runtime/runtime-capabilities.json' /bench/runtime.json
cp /tmp/treefrog-driver-report.txt /bench/driver.txt
for frame in /tmp/treefrog-frame-*.ppm; do [ ! -f "$frame" ] || cp "$frame" /bench/; done
cat /proc/self/mountinfo > /bench/final-mounts.txt
sync
if umount /storage; then echo STORAGE_UNMOUNTED; fi
echo CONVERTED_PORT_VM_DONE
poweroff -f
'''
        if a.direct:
            init=init.replace('/usr/lib/lilsmoke/lilsmoke-apps install /bench/game.lilpck HASH > /bench/install.log 2>&1',': > /bench/install.log')
            init=init.replace("/usr/lib/lilsmoke/lilsmoke-apps run 'APP_NAME'","/usr/lib/lilsmoke/lilsmoke-treefrog-launch "+port_id)
        if combo:
            init=init.replace('/usr/bin/vm-pad > /bench/pad.log','/usr/bin/vm-pad 12 > /bench/pad.log')
            start=init.index('(\n  while kill -0 "$app"')
            end=init.index('watcher=$!',start)
            init=init[:start]+'''(
  sleep 90
  if kill -0 "$app" 2>/dev/null; then echo watchdog > /bench/watchdog.txt; kill -TERM "$app"; fi
) &
'''+init[end:]
            init=init.replace('wait "$app"; echo APP_EXIT=$?','wait "$app"; echo APP_EXIT=$?\nkill -TERM "$watcher" 2>/dev/null')
            init=init.replace("cp '/storage/independent-apps/APP_NAME/runtime/runtime-capabilities.json' /bench/runtime.json",'''cp /tmp/treefrog-combo-sent /bench/combo-sent.txt
cp /persist/logs/treefrog-PORT_ID.log /bench/bridge.log 2>/dev/null
if [ -f '/storage/independent-apps/APP_NAME/runtime/runtime-capabilities.json' ]; then cp '/storage/independent-apps/APP_NAME/runtime/runtime-capabilities.json' /bench/runtime.json; fi
cp '/storage/independent-apps/APP_NAME/runtime/port/log.txt' /bench/engine.log 2>/dev/null
cp /storage/treefrog-ports/PORT_ID/log.txt /bench/direct-engine.log 2>/dev/null''')
        init=init.replace('HASH',report['package_sha256']).replace('APP_NAME',meta['name']).replace('PORT_ID',port_id)
        if any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_' for c in meta['name']):raise ValueError('QA fixture name is not literal-safe')
        ov.file('usr/bin/bench-init',init,0o755)
        initrd=work/'boot.cpio.gz';initrd.write_bytes(gzip.compress(overlay_initramfs(SDK,ov,'/usr/bin/bench-init',overlay_mib=16)))
        devices=disk_devices(SDK,[('bench',bench,False),('stock',stock,True)])
        devices=[s.replace(str(SDK/'images/rootfs.squashfs'),'/root/lilsmoke-media-c45-final-video/rootfs.squashfs')for s in devices]
        devices+=['-drive',f'file={disk},if=none,id=storage,format=raw','-device','virtio-blk-pci,drive=storage,addr=0x15']
        command=['taskset','-c',str(min(os.sched_getaffinity(0))),'nice','-n','15','/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel','-M','malta','-cpu','74Kf','-smp','1','-m','128','-kernel',str(SDK/'images/vmlinux'),'-initrd',str(initrd),'-append','console=ttyS0 panic=-1','-vga','cirrus','-display','none','-serial','stdio','-monitor','none','-no-reboot',*devices]
        vm=None
        try:
            with (bench/'serial.log').open('wb')as log:
                vm=subprocess.Popen(command,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT)
                deadline=time.monotonic()+480
                while vm.poll()is None and time.monotonic()<deadline:
                    if 'CONVERTED_PORT_VM_DONE'in(bench/'serial.log').read_text(errors='replace'):break
                    time.sleep(.5)
                else:
                    if vm.poll()is None:raise RuntimeError('480-second VM timeout')
        finally:
            if vm and vm.poll()is None:vm.terminate()
            if vm:
                try:vm.wait(timeout=10)
                except subprocess.TimeoutExpired:vm.kill();vm.wait(timeout=5)
            for f in bench.iterdir():
                if f.is_file()and f.name!='game.lilpck':shutil.copyfile(f,a.out/f.name)
            check=subprocess.run(['fsck.fat','-n',str(disk)],capture_output=True,text=True);(a.out/'fat32-check.txt').write_text(check.stdout+check.stderr)
            report['fat32_check_exit']=check.returncode
            serial=(a.out/'serial.log').read_text(errors='replace')
            if(a.out/'runtime.json').exists():report['runtime']=json.loads((a.out/'runtime.json').read_text())
            report['clean_storage']='STORAGE_UNMOUNTED'in serial
            runtime = report.get('runtime', {})
            observed = runtime.get('observed', {})
            counters = dict(line.split('=', 1) for line in (a.out/'driver.txt').read_text().splitlines()) if (a.out/'driver.txt').is_file() else {}
            report['rendered_frames'] = int(counters.get('frames', 0))
            report['nonzero_pcm_energy'] = int(counters.get('energy', 0))
            report['gameplay_callbacks_verified'] = (a.native or observed.get('video_frames', 0) >= 180 and observed.get('audio_frames', 0) > 0 and observed.get('nonzero_audio_samples', 0) > 0) and report['rendered_frames'] >= (30 if combo else 180) and report['nonzero_pcm_energy'] > 0
            report['hotkey_exit_verified']=combo and(a.out/'combo-sent.txt').is_file()and not(a.out/'watchdog.txt').exists()and'APP_EXIT=0'in serial
            report['binds_released']='/run/lilsmoke-treefrog-root/' not in (a.out/'final-mounts.txt').read_text()
            report['passed']=report['clean_storage']and report['binds_released']and check.returncode==0 and(a.native or combo or bool(runtime.get('ok')))and'INSTALL_EXIT=0'in serial and report['gameplay_callbacks_verified']and(not combo or report['hotkey_exit_verified'])
            (a.out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items()if k!='runtime'},indent=2))
    if not report['passed']:raise SystemExit(1)
if __name__=='__main__':main()
