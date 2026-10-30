"""Private controller-only libusb: polling hotplug and pipe/poll signalling."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tarfile

P=Path(__file__).resolve().parents[2]
B=Path('/root/lilsmoke-os/build/buildroot/output')
Q=Path('/root/lilsmoke-portable-usb-20261011')
source=Q/'libusb-1.0.26'
archive=Path('/root/lilsmoke-hidapi-20261011/libusb-1.0.26.tar.bz2')
assert hashlib.sha256(archive.read_bytes()).hexdigest()=='12ce7a61fc9854d1d2a1ffe095f7b5fac19ddba095c259e6067a46500381b5a5'
Q.mkdir(exist_ok=True)
if not source.exists():
    with tarfile.open(archive) as files: files.extractall(Q,filter='data')
patches=P/'package/lilsmoke-usb-gamepads/portable-usb'
patch_hash=hashlib.sha256((patches/'0001-polling-hotplug.patch').read_bytes()).hexdigest()
marker=source/'.lilsmoke-poll-patched'
if not marker.exists() or marker.read_text().strip()!=patch_hash:
    with tarfile.open(archive) as files:
        files.extract('libusb-1.0.26/libusb/os/linux_usbfs.c',Q,filter='data')
    subprocess.run(['patch','-p1','--fuzz=0','--batch','-i',str(patches/'0001-polling-hotplug.patch')],cwd=source,check=True)
    marker.write_text(patch_hash+'\n')
for name in ('poll_hotplug.h','poll_hotplug.inc'):
    shutil.copyfile(patches/name,source/'libusb/os'/name)
env=dict(os.environ,CC=str(B/'host/bin/mipsel-buildroot-linux-musl-gcc'),
         CFLAGS='-O2 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64')
with (Q/'build.log').open('w') as log:
    for command in (
        ['./configure','--host=mipsel-buildroot-linux-musl','--prefix=/usr','--disable-static','--disable-udev','--disable-eventfd','--disable-timerfd'],
        ['make','-j2']):
        subprocess.run(command,cwd=source,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
target=B/'target/usr/lib/lilsmoke-usb'
target.mkdir(exist_ok=True)
library=source/'libusb/.libs/libusb-1.0.so.0.3.0'
shutil.copyfile(library,target/'libusb-1.0.so.0')
subprocess.run([str(B/'host/bin/mipsel-buildroot-linux-musl-strip'),str(target/'libusb-1.0.so.0')],check=True)
print('Built controller-private libusb: netlink fallback, no eventfd/timerfd dependency',Q)
