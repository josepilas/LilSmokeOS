#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Original native and libretro ports in 128-MiB MIPS Linux, not host games.

The real port, glibc, SDL and PicoArch run unchanged. Only the HC display/audio
device boundary is replaced by a frame/PCM capture driver; no physical FPS claim.
"""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
SDK=Path(os.environ.get('SDK','/root/lilsmoke-os/build/buildroot/output'))
sys.path.insert(0,str(ROOT/'tools/hwtest'))
from mips_boot_smoke import Newc
from mips_disk_fixture import overlay_initramfs,disk_devices,runtime_libraries


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def mips_elf(path):
    if not path.is_file():return False
    with path.open('rb') as f:header=f.read(20)
    return len(header)==20 and header[:7]==b'\x7fELF\x01\x01\x01' and header[18:20]==b'\x08\x00'


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stock',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--helpers',type=Path,default=HERE/'_build/mips',help='Qualified helper build directory')
    p.add_argument('--applications',action='store_true',help='Also test renamed native app and a typed-argv recording frontend (not a real independent core)')
    p.add_argument('--expect-missing-os',action='store_true',help='Negative fixture: omit OS backing bind and require original ENOENT')
    a=p.parse_args();a.stock=a.stock.resolve(strict=True);a.out=a.out.resolve()
    a.out.mkdir(parents=True,exist_ok=False)
    cc=str(SDK/'host/bin/mipsel-buildroot-linux-musl-gcc')
    subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC','-nostdlib',
        '-mips32r2','-mabi=32','-mfp32','-mhard-float','-Wl,--hash-style=sysv',
        str(HERE/'qa_driver.c'),'-o',str(a.out/'driver.so')],check=True)
    subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror',
        '-I'+str(ROOT/'package/lilsmoke-ui/src/src'),str(HERE/'qa_pad.c'),'-o',str(a.out/'qa-pad')],check=True)
    base=HERE/'_build'
    build=json.loads((a.helpers/'build.json').read_text())
    for path,digest in build['source_sha256'].items():
        if sha(ROOT/path)!=digest:raise RuntimeError('Build input changed: '+path)
    scripts=[ROOT/'package/lilsmoke-scripts/src'/n for n in ('lilsmoke-treefrog-launch','lilsmoke-treefrog-port-check','treefrog-keymap.txt')]
    source_snapshot={str(p.relative_to(ROOT)):sha(p) for p in scripts}
    # A tiny dependency closure, not the stock card or game collection. The
    # original stock directory is read-only and never changed by this fixture.
    stock=a.out/'stock-runtime';(stock/'lib').mkdir(parents=True);(stock/'usr/lib').mkdir(parents=True)
    shutil.copyfile(a.stock/'lib/ld.so.1',stock/'lib/ld.so.1')
    shutil.copyfile(a.out/'driver.so',stock/'driver.so')
    pending=[base/'hexahop-original-20261027/roms/ports/hexahop/hex-a-hop.mipsel',
        base/'hexahop-original-20261027/roms/ports/hexahop/libSDL_ttf-2.0.so.0',
        base/'classicube-original-20261027/roms/ports/classicube/classicube_libretro.so',
        base/'runtime-original-20261027/cubegm/picoarch']
    seen={'libSDL_ttf-2.0.so.0'}
    readelf=str(SDK/'host/bin/mipsel-buildroot-linux-musl-readelf')
    while pending:
        binary=pending.pop()
        dynamic=subprocess.check_output([readelf,'-d',str(binary)],text=True)
        for name in re.findall(r'\(NEEDED\).*\[([^]]+)\]',dynamic):
            if name in seen:continue
            seen.add(name)
            # Some stock cards contain ARM libraries under lib/ alongside valid
            # MIPS versions in usr/lib/. Match the dynamic loader's architecture
            # filtering, not just the first filename that exists.
            relative=next((Path(d)/name for d in ('lib','usr/lib') if mips_elf(a.stock/d/name)),None)
            if relative is None:raise RuntimeError('Missing stock dependency: '+name)
            shutil.copyfile(a.stock/relative,stock/relative);pending.append(a.stock/relative)
    scratch=tempfile.TemporaryDirectory(prefix='lilsmoke-treefrog-fat32-')
    scratch_root=Path(scratch.name)
    disk=scratch_root/'ports.fat'
    with disk.open('xb') as f:f.truncate(256*1024*1024)
    subprocess.run(['mkfs.fat','-F','32','-n','LILROMS_QA',str(disk)],check=True,capture_output=True)
    stage=scratch_root/'stage';stage.mkdir()
    inventories=[]
    for port in ('hexahop','classicube'):
        fixture=base/(port+'-original-20261027')
        inventory=json.loads((fixture/'fixture.json').read_text());inventories.append((fixture,inventory))
        for item in inventory['files']:
            original=fixture/'roms'/item['path']
            if sha(original)!=item['sha256']:raise RuntimeError('Original fixture changed')
            target=stage/'treefrog-ports'/Path(item['path']).relative_to('ports');target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(original,target)
    if a.applications:
        if a.expect_missing_os:raise RuntimeError('Application cases are positive-only')
        native=stage/'independent-apps/Renamed Hex App'
        shutil.copytree(stage/'treefrog-ports/hexahop',native/'runtime/port')
        probe=stage/'independent-apps/Renamed Libretro App'
        (probe/'runtime/port').mkdir(parents=True)
        (probe/'runtime/frontend').mkdir()
        flags=['-mips32r2','-mabi=32','-mfp32','-mhard-float']
        subprocess.run([cc,'-std=c11','-O2',*flags,str(HERE/'application-recorder.c'),'-o',str(probe/'runtime/frontend/lilretro')],check=True)
        subprocess.run([cc,'-std=c11','-O2',*flags,'-shared','-fPIC',str(HERE/'application-recorder.c'),'-Wl,--no-as-needed','-lc','-o',str(probe/'runtime/port/core.so')],check=True)
        config=dict(schema_version=1,id='probe',name='Typed argv fixture',launch=dict(type='libretro',binary='core.so',content='content ;literal.dat'),required_file_sets=[])
        (probe/'runtime/port/config.json').write_text(json.dumps(config))
        (probe/'runtime/port/run.sh').write_text('#!/bin/sh\ntouch /tmp/UPSTREAM_SCRIPT_EXECUTED\nexit 99\n')
        (probe/'runtime/port/content ;literal.dat').write_text('literal filename, not shell text\n')
        (probe/'runtime/launch.json').write_text(json.dumps(dict(schema=1,save_directory='userdata')))
        for app,id,profile in ((native,'hexahop','native'),(probe,'probe','libretro')):
            config=json.loads((app/'runtime/port/config.json').read_text())
            (app/'meta.json').write_text(json.dumps(dict(id='org.lilsmoke.treefrog.'+id,name='Renamed '+id,version='0.0.1')))
            (app/(id+'.sh')).write_text('#!/bin/sh\nexit 98\n')
            adapter=dict(schema=1,kind='treefrog-converted',port_id=id,profile=profile,binary=config['launch']['binary'],content=config['launch'].get('content',''),port_root='runtime/port',requires_runtime='treefrog-compat-0.2',binary_recompiled=False)
            if profile=='libretro':adapter.update(independent_libretro=True,frontend='runtime/frontend/lilretro',runtime_profile='musl')
            (app/'adapter.json').write_text(json.dumps(adapter))
    shutil.copytree(base/'runtime-original-20261027/cubegm',stage/'.lilsmoke/treefrog-runtime/cubegm')
    (stage/'.qa-log').mkdir()
    for child in stage.iterdir():subprocess.run(['mcopy','-s','-i',str(disk),str(child),'::/'],check=True)
    exports=[('stock',stock,True)]
    overlay=Newc()
    binaries=[]
    for name in ('lilsmoke-treefrog-check','lilsmoke-treefrog-input','input-test','loading-test','port-lease-test'):
        binary=a.helpers/name;binaries.append(binary)
        overlay.file('usr/lib/lilsmoke/'+name,binary.read_bytes(),0o755)
    for name in ('lilsmoke-treefrog-launch','lilsmoke-treefrog-port-check','treefrog-keymap.txt'):
        data=(ROOT/'package/lilsmoke-scripts/src'/name).read_bytes()
        if name=='lilsmoke-treefrog-launch' and a.expect_missing_os:
            text=data.decode()
            block='''if [ -d /os/files ]; then
    bind /os os
    mount -o remount,bind,ro "$view/os" || fail 'Cannot protect native runtime objects'
fi'''
            if text.count(block)!=1:raise RuntimeError('OS bind fault injection no longer matches')
            data=text.replace(block,': # QA intentionally omits native backing namespace').encode()
        overlay.file('usr/lib/lilsmoke/'+name,data,0o755)
    runtime_libraries(SDK,overlay,binaries)
    overlay.file('usr/lib/lilsmoke/qa-pad',(a.out/'qa-pad').read_bytes(),0o755)
    init='''#!/bin/sh
exec </dev/console >/dev/console 2>&1
mkdir -p /dev/pts /dev/shm /lilos/cubegm /storage
mount -t devpts devpts /dev/pts
mount -t tmpfs -o size=2m tmpfs /dev/shm
mount -t 9p -o trans=virtio,version=9p2000.L,ro stock /lilos/cubegm || poweroff -f
mount -t vfat -o rw,umask=0077 /dev/vdb /storage || poweroff -f
ln -s /storage/.qa-log /bench
# Reproduce the real card's file-backed metadata root, not just a conventional
# SquashFS /bin. An unexposed /os makes execve("/bin/sh") return ENOENT.
mkdir -p /os/files/treefrog-qa
cp -L /bin/busybox /os/files/treefrog-qa/busybox || poweroff -f
cp -L /lib/ld-musl-mipsel.so.1 /os/files/treefrog-qa/ld-musl-mipsel.so.1 || poweroff -f
cp -L /lib/libc.so /os/files/treefrog-qa/libc.so || poweroff -f
ln -sf /os/files/treefrog-qa/busybox /bin/busybox
ln -sf busybox /bin/sh
ln -sf /os/files/treefrog-qa/ld-musl-mipsel.so.1 /lib/ld-musl-mipsel.so.1
ln -sf /os/files/treefrog-qa/libc.so /lib/libc.so
readlink /bin/busybox > /bench/native-busybox-link.txt
readlink /lib/ld-musl-mipsel.so.1 > /bench/native-loader-link.txt
export SDL_FBDEV=/dev/fb0 SDL_NOMOUSE=1 PICOARCH_QEMU=1
cat /proc/meminfo > /bench/memory.txt
cat /proc/fb > /bench/fb.txt
/usr/lib/lilsmoke/input-test > /bench/input-test.log 2>&1 || poweroff -f
/usr/lib/lilsmoke/loading-test > /bench/loading-test.log 2>&1 || poweroff -f
/usr/lib/lilsmoke/port-lease-test unit /run/treefrog-lease-qa > /bench/port-lease-test.log 2>&1 || poweroff -f
run_case() {
  name=$1
  # The actual UI restores display mode between launches. Mirror that boundary
  # on Malta's virtual Cirrus device instead of inheriting another game's mode.
  fbset -g 640 480 640 480 16 >"/bench/$name-fb-reset.txt" 2>&1 || return 91
  rm -f /tmp/treefrog-driver-report.txt
  mkdir -p /run/m8-loading
  rm -f /run/m8-loading/stopped
  touch /run/m8-loading/active /run/m8-boot-logo.state
  ( while [ -f /run/m8-loading/active ]; do sleep 0.01; done; touch /run/m8-loading/stopped ) &
  logo=$!
  export LILSMOKE_M8_BOOT_STATE=/run/m8-boot-logo.state LILSMOKE_M8_LOADING_GAME=1
  /usr/lib/lilsmoke/qa-pad 25 >"/bench/$name-input.log" 2>&1 &
  pad=$!
  case "$name" in
    converted-hexahop) /usr/lib/lilsmoke/lilsmoke-treefrog-launch --application "/storage/independent-apps/Renamed Hex App" >"/bench/$name.log" 2>&1 & ;;
    independent-probe) /usr/lib/lilsmoke/lilsmoke-treefrog-launch --application "/storage/independent-apps/Renamed Libretro App" >"/bench/$name.log" 2>&1 & ;;
    *) /usr/lib/lilsmoke/lilsmoke-treefrog-launch "$name" >"/bench/$name.log" 2>&1 & ;;
  esac
  pid=$!
  (
    sleep 120
    if kill -0 "$pid" 2>/dev/null; then
      echo timeout >"/bench/$name-timeout.txt"
      ps >"/bench/$name-timeout-processes.txt"
      cat /proc/meminfo >"/bench/$name-timeout-memory.txt"
      for process in /proc/[0-9]*; do
        printf '%s ' "$process" >>"/bench/$name-timeout-wchan.txt"
        cat "$process/wchan" >>"/bench/$name-timeout-wchan.txt" 2>/dev/null
        echo >>"/bench/$name-timeout-wchan.txt"
      done
      kill -TERM "$pid"
    fi
  ) &
  watchdog=$!
  while kill -0 "$pid" 2>/dev/null; do
    cat "/proc/$pid/status" >>"/bench/$name-memory.log" 2>/dev/null
    if awk '$5 == "/run/lilsmoke-treefrog-root/os" && $6 ~ /(^|,)ro(,|$)/ { found=1 } END { exit !found }' /proc/self/mountinfo; then
      echo readonly >"/bench/$name-os-readonly.txt"
    fi
    sleep 1
  done
  wait "$pid"
  code=$?
  kill -TERM "$watchdog" 2>/dev/null
  wait "$watchdog" 2>/dev/null
  echo "$code" >"/bench/$name.exit"
  if [ ! -f /run/m8-loading/active ] && [ ! -f /run/m8-boot-logo.state ]; then
    echo released >"/bench/$name-loading.txt"
  fi
  kill -TERM "$pad" 2>/dev/null
  wait "$pad" 2>/dev/null
  rm -f /run/m8-loading/active
  wait "$logo"
  cp /tmp/treefrog-driver-report.txt "/bench/$name-driver.txt" 2>/dev/null
  for frame in /tmp/treefrog-frame-*.ppm; do
    [ -f "$frame" ] || continue
    cp "$frame" "/bench/$name-${frame##*/}"
    rm -f "$frame"
  done
  cp /storage/treefrog-ports/hexahop/log.txt /bench/hexahop-engine.log 2>/dev/null
  if [ "$name" = converted-hexahop ]; then
    cp "/storage/independent-apps/Renamed Hex App/runtime/port/log.txt" /bench/converted-hexahop-engine.log
    cp /persist/logs/treefrog-hexahop.log /bench/converted-hexahop-session.log
  fi
  cp "/persist/logs/treefrog-$name.log" "/bench/$name-session.log" 2>/dev/null
  cat /proc/mounts >"/bench/$name-mounts.txt"
  if touch /os/files/treefrog-qa/source-writable-check; then
    echo writable >"/bench/$name-source-writable.txt"
  fi
  echo "TREEFROG_VM_CASE $name exit=$code"
}
run_case hexahop
run_case classicube
APPLICATION_CASES
cd /
sync
if umount /storage; then echo TREEFROG_STORAGE_UNMOUNTED; fi
echo TREEFROG_VM_DONE
poweroff -f
'''
    app_cases='''run_case converted-hexahop
run_case independent-probe
cp "/storage/independent-apps/Renamed Libretro App/runtime/userdata/argv.txt" /bench/independent-argv.txt
if [ ! -e /tmp/UPSTREAM_SCRIPT_EXECUTED ]; then echo unchanged > /bench/independent-script-not-executed.txt; fi
'''
    init=init.replace('APPLICATION_CASES',app_cases if a.applications else ':')
    overlay.file('usr/bin/bench-init',init,0o755)
    initrd=a.out/'boot.cpio.gz'
    initrd.write_bytes(gzip.compress(overlay_initramfs(SDK,overlay,'/usr/bin/bench-init',overlay_mib=16)))
    devices=disk_devices(SDK,exports)
    source=str(SDK/'images/rootfs.squashfs')
    devices=[s.replace(source,'/root/lilsmoke-media-c45-final-video/rootfs.squashfs') for s in devices]
    devices+=['-drive',f'file={disk},if=none,id=portsdisk,format=raw',
        '-device','virtio-blk-pci,drive=portsdisk,addr=0x15']
    cmd=['taskset','-c',os.environ.get('TREEFROG_CPU','2'),'nice','-n','15',
        '/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel','-M','malta',
        '-cpu','74Kf','-smp','1','-m','128','-kernel',str(SDK/'images/vmlinux'),
        '-initrd',str(initrd),'-append','console=ttyS0 panic=-1','-vga','cirrus',
        '-display','none','-serial','stdio','-monitor','none','-no-reboot',*devices]
    report=dict(passed=False,cpu='74Kf',cpus=1,ram_mib=128,gpu_3d=False,
        physical_hardware=False,device_boundary='QA capture driver (not HC hardware emulation)',
        sdl_video_boundary='original stock SDL fbcon on virtual framebuffer',
        actual_original_ports=True,foreign_runtime='stock glibc; no musl SDL injected',cases=[])
    report.update(native_runtime_file_backed=True,qa_omits_os_bind=a.expect_missing_os,
                  production_launcher=not a.expect_missing_os)
    report['storage_filesystem']='FAT32 on a temporary sparse virtio disk'
    started=time.monotonic()
    with (a.out/'serial.log').open('w') as log:
        proc=subprocess.Popen(cmd,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        deadline=time.monotonic()+900
        done=False
        try:
            while proc.poll() is None and time.monotonic()<deadline:
                time.sleep(1)
                try:done='TREEFROG_VM_DONE' in (a.out/'serial.log').read_text(errors='replace')
                except OSError:continue  # DrvFS can transiently return ENODATA on a live log.
                if done:break
            if not done:report['incomplete']=True
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:proc.wait(timeout=10)
                except subprocess.TimeoutExpired:proc.kill();proc.wait()
    report['seconds']=time.monotonic()-started
    subprocess.run(['mcopy','-s','-i',str(disk),'::/.qa-log/*',str(a.out)],check=True)
    fsck=subprocess.run(['fsck.fat','-n',str(disk)],capture_output=True,text=True)
    (a.out/'fat32-check.txt').write_text(fsck.stdout+fsck.stderr)
    report['fat32_check_exit']=fsck.returncode
    report['storage_unmounted']='TREEFROG_STORAGE_UNMOUNTED' in (a.out/'serial.log').read_text(errors='replace')
    for port in ('hexahop','classicube'):
        exitfile=a.out/(port+'.exit');driver=a.out/(port+'-driver.txt')
        counters={}
        if driver.is_file():
            counters={k:int(v) for k,v in (line.split('=',1) for line in driver.read_text().splitlines())}
        frames=list(a.out.glob(port+'-treefrog-frame-*.ppm'))
        mounts=a.out/(port+'-mounts.txt')
        clean=mounts.is_file() and '/run/lilsmoke-treefrog-root/' not in mounts.read_text()
        report['cases'].append(dict(port=port,exit=int(exitfile.read_text()) if exitfile.is_file() else None,
            capture_files=[f.name for f in frames],counters=counters,binds_released=clean,
            loading_released=(a.out/(port+'-loading.txt')).is_file(),
            backing_mount_read_only=(a.out/(port+'-os-readonly.txt')).is_file(),
            source_namespace_still_writable=(a.out/(port+'-source-writable.txt')).is_file()))
    unchanged=[]
    for fixture,inventory in inventories:
        for item in inventory['files']:
            data=subprocess.check_output(['mtype','-i',str(disk),'::/treefrog-ports/'+str(Path(item['path']).relative_to('ports'))])
            unchanged.append(hashlib.sha256(data).hexdigest()==item['sha256'] and sha(fixture/'roms'/item['path'])==item['sha256'])
    report['original_files_unchanged']=bool(unchanged) and all(unchanged)
    report['helper_sha256']={p.name:sha(p) for p in binaries}
    report['compiled_source_sha256']=build['source_sha256']
    report['source_sha256']=source_snapshot
    report['sources_unchanged']=all(sha(ROOT/p)==h for p,h in {**source_snapshot,**build['source_sha256']}.items())
    report['timeouts']=[p.name for p in a.out.glob('*-timeout.txt')]
    report['passed']=not report['timeouts'] and report['storage_unmounted'] and fsck.returncode==0 and report['sources_unchanged'] and report['original_files_unchanged'] and all(x['exit']==0 and x['binds_released'] and x['loading_released'] and
        x['backing_mount_read_only'] and x['source_namespace_still_writable'] and
        x['counters'].get('frames',0)>10 and x['capture_files'] for x in report['cases'])
    if a.expect_missing_os:
        report['passed']=report['storage_unmounted'] and fsck.returncode==0 and report['original_files_unchanged'] and all(
            case['exit']==127 and case['binds_released'] and case['loading_released'] and
            'TreeFrog original launcher: No such file or directory' in (a.out/(case['port']+'-session.log')).read_text()
            for case in report['cases'])
        report['qualification']='Negative ENOENT reproduction only, not successful port execution'
    if a.applications:
        apps=[]
        for name in ('converted-hexahop','independent-probe'):
            mounts=a.out/(name+'-mounts.txt')
            driver=a.out/(name+'-driver.txt')
            counters={k:int(v) for k,v in (line.split('=',1) for line in driver.read_text().splitlines())} if driver.is_file() else {}
            apps.append(dict(name=name,exit=int((a.out/(name+'.exit')).read_text()),
                binds_released='/run/lilsmoke-treefrog-root/' not in mounts.read_text(),
                loading_released=(a.out/(name+'-loading.txt')).is_file(),counters=counters))
        report['application_cases']=apps
        report['independent_core_executed']=False
        report['independent_qualification']='Typed argv, ABI validation, writable app containment and no original run.sh; recording frontend only'
        report['independent_argv']=(a.out/'independent-argv.txt').read_text().splitlines() if (a.out/'independent-argv.txt').exists() else []
        report['independent_original_script_not_executed']=(a.out/'independent-script-not-executed.txt').is_file()
        report['native_application_gameplay_verified']=apps[0]['counters'].get('frames',0)>10 and apps[0]['counters'].get('energy',0)>0
        report['passed']=report['passed'] and report['native_application_gameplay_verified'] and all(x['exit']==0 and x['binds_released'] and x['loading_released'] for x in apps) and len(report['independent_argv'])==5 and report['independent_original_script_not_executed']
    (a.out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    scratch.cleanup()
    print(json.dumps(report,indent=2),flush=True)
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
