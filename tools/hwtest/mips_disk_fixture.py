"""Small diagnostic boot fixture over the real built OS, not a substitute UI."""
from dataclasses import replace
from pathlib import Path
import re
import subprocess
from mips_boot_smoke import Newc


def runtime_libraries(build: Path, fixture: Newc, binaries) -> None:
    """Overlay the current SDK dependency closure onto an older QA rootfs."""
    readelf = build / 'host/bin/mipsel-buildroot-linux-musl-readelf'
    pending = list(binaries)
    installed = set()
    while pending:
        binary = pending.pop()
        dynamic = subprocess.run([str(readelf), '-d', str(binary)], check=True,
                                 text=True, capture_output=True).stdout
        for name in re.findall(r'\(NEEDED\).*\[([^]]+)\]', dynamic):
            if name == 'libc.so' or name in installed:
                continue
            candidates = [build / 'target/usr/lib' / name, build / 'target/lib' / name]
            library = next((p for p in candidates if p.is_file()), None)
            if library is None:
                raise RuntimeError('Missing QA runtime dependency: ' + name)
            installed.add(name)
            fixture.file('usr/lib/' + name, library.read_bytes(), 0o755)
            pending.append(library)


def overlay_initramfs(build: Path, fixture: Newc, entrypoint='/sbin/init', sdcard=False,
                     overlay_mib=8, trace=False, rootfs_in_ram=False,
                     root_device='/dev/vda', root_fstype='squashfs') -> bytes:
    if overlay_mib not in (8, 16, 32):
        raise ValueError('invalid diagnostic overlay budget')
    if root_device not in ('/dev/vda', '/dev/sda'):
        raise ValueError('unsupported diagnostic root device')
    if root_fstype not in ('squashfs', 'ext2'):
        raise ValueError('unsupported diagnostic root filesystem')
    if root_fstype != 'squashfs' and (rootfs_in_ram or sdcard):
        raise ValueError('alternate diagnostic root requires a standalone block image')
    boot = Newc()
    for name in ('bin', 'lib', 'dev', 'proc', 'sys', 'os', 'sysroot', 'overlay', 'qa', 'lilos'):
        boot.dir(name)
    boot.file('bin/busybox', (build/'target/bin/busybox').read_bytes(), 0o755)
    boot.symlink('bin/sh', 'busybox')
    for name in ('lib/libc.so', 'lib/ld-musl-mipsel.so.1'):
        boot.file(name, (build/'target'/name).read_bytes(), 0o755)
    parents = {str(p) for e in fixture.entries for p in Path(e.name).parents if str(p) != '.'}
    for name in sorted(parents, key=lambda p: (p.count('/'), p)):
        boot.dir('qa/'+name)
    boot.entries += [replace(e, name='qa/'+e.name) for e in fixture.entries]
    if entrypoint not in ('/sbin/init', '/usr/bin/bench-init'):
        raise ValueError('unsupported diagnostic entrypoint')
    if rootfs_in_ram and sdcard:
        raise ValueError('RAM root cannot stand in for an SD layout test')
    if rootfs_in_ram:
        boot.file('rootfs.squashfs', (build/'images/rootfs.squashfs').read_bytes())
    root_mount = '''$bb mount -t vfat -o rw,nosuid,nodev,noexec,umask=0077 "${rootdev}1" /lilos
$bb mount -t squashfs -o loop,ro /lilos/lilsmoke/slots/A/rootfs.squashfs /os
''' if sdcard else ('$bb mount -t squashfs -o loop,ro /rootfs.squashfs /os\n' if rootfs_in_ram
                   else '$bb mount -t ' + root_fstype + ' -o ro "$rootdev" /os\n')
    move_os = '''$bb mkdir -p /sysroot/lilos
$bb mount --move /lilos /sysroot/lilos
''' if sdcard else ''
    source = '''#!/bin/sh
set -eu
bb=/bin/busybox
rootdev=ROOT_DEVICE
$bb mount -t proc proc /proc
$bb mount -t sysfs sysfs /sys
$bb mount -t devtmpfs dev /dev
'''
    if not rootfs_in_ram:
        source += '''count=0
while [ ! -b "$rootdev" ]; do
    count=$((count+1))
    [ "$count" -lt 30 ] || { echo DISK_FIXTURE_NO_ROOT >&2; exit 1; }
    $bb sleep 1
done
'''
    source += root_mount+'''
$bb mount -t tmpfs -o size=8M,mode=0755 tmpfs /overlay
$bb mkdir /overlay/upper /overlay/work
$bb mount -t overlay overlay -o lowerdir=/os,upperdir=/overlay/upper,workdir=/overlay/work /sysroot
$bb cp -a /qa/. /sysroot/
'''+move_os+'''
$bb mount --move /dev /sysroot/dev
$bb mount --move /proc /sysroot/proc
$bb mount --move /sys /sysroot/sys
exec $bb switch_root /sysroot '''+entrypoint+'\n'
    source = source.replace('size=8M,mode=0755', 'size='+str(overlay_mib)+'M,mode=0755')
    source = source.replace('ROOT_DEVICE', root_device)
    if trace:
        source = source.replace('bb=/bin/busybox\n', 'bb=/bin/busybox\nset -x\n')
    boot.file('init', source, 0o755)
    return boot.build()


def disk_devices(build: Path, shares) -> list[str]:
    if len(shares) > 7:
        raise ValueError('too many PCI functions')
    args = ['-drive', f'file={build / "images/rootfs.squashfs"},if=none,id=osroot,format=raw,readonly=on',
            '-device', 'virtio-blk-pci,drive=osroot,addr=0x14.0,multifunction=on']
    for index, (tag, path, readonly) in enumerate(shares, 1):
        args += ['-fsdev', f'local,id=share{index},path={path},security_model=none'+(',readonly=on' if readonly else ''),
                 '-device', f'virtio-9p-pci,fsdev=share{index},mount_tag={tag},addr=0x14.{index}']
    return args
