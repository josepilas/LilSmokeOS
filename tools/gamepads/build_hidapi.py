"""Enable pinned SDL HIDAPI/libusb without changing the graphics/audio options."""
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import tarfile
import urllib.request
import shutil

B=Path('/root/lilsmoke-os/build/buildroot/output')
Q=Path('/root/lilsmoke-hidapi-20261011');Q.mkdir(exist_ok=True)
env=dict(os.environ,PATH=str(B/'host/bin')+':/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin',
         CC=str(B/'host/bin/mipsel-buildroot-linux-musl-gcc'),
         CXX=str(B/'host/bin/mipsel-buildroot-linux-musl-g++'),
         PKG_CONFIG=str(B/'host/bin/pkgconf'),PKG_CONFIG_SYSROOT_DIR=str(B/'staging'),
         PKG_CONFIG_LIBDIR=str(B/'staging/usr/lib/pkgconfig')+':'+str(B/'staging/usr/share/pkgconfig'),
         CFLAGS='-O3 -g0 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64',
         CXXFLAGS='-O3 -g0 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64')
archive=Q/'libusb-1.0.26.tar.bz2'
if not archive.exists():
    urllib.request.urlretrieve('https://github.com/libusb/libusb/releases/download/v1.0.26/libusb-1.0.26.tar.bz2',archive)
assert hashlib.sha256(archive.read_bytes()).hexdigest()=='12ce7a61fc9854d1d2a1ffe095f7b5fac19ddba095c259e6067a46500381b5a5'
source=Q/'libusb-1.0.26'
if not source.exists():
    with tarfile.open(archive) as files:files.extractall(Q,filter='data')
with (Q/'build.log').open('w') as log:
    def run(args,cwd):
        subprocess.run(list(map(str,args)),cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    run(['./configure','--host=mipsel-buildroot-linux-musl','--prefix=/usr','--disable-udev','--disable-static'],source)
    run(['make','-j2'],source)
    for root in [B/'staging',B/'target']:run(['make','install','DESTDIR='+str(root)],source)
    sdl=B/'build/sdl2-2.28.5'
    options=shlex.split(next(line.strip()[2:] for line in (sdl/'config.log').read_text().splitlines() if line.strip().startswith('$ ./configure ')))
    options=[option for option in options if option not in ['--disable-hidapi-joystick','--disable-hidapi-libusb','--disable-joystick-virtual']]
    options+=['--enable-hidapi-joystick','--enable-hidapi-libusb','--enable-joystick-virtual']
    run(options,sdl)
    run(['make','clean'],sdl)
    run(['make','-j2'],sdl)
    for root in [B/'staging',B/'target']:run(['make','install','DESTDIR='+str(root)],sdl)
    config=(sdl/'include/SDL_config.h').read_text()
    assert '#define SDL_JOYSTICK_HIDAPI 1' in config and '#define HAVE_LIBUSB 1' in config
    # SDK development files belong in staging, never in the low-RAM runtime.
    target=(B/'target').resolve()
    for relative in ['usr/include/SDL2','usr/include/libusb-1.0','usr/lib/cmake/SDL2']:
        path=target/relative
        assert path.resolve().is_relative_to(target)
        if path.exists():shutil.rmtree(path)
    for relative in ['usr/lib/libSDL2.a','usr/lib/libSDL2main.a','usr/lib/libSDL2_test.a',
                     'usr/lib/libSDL2.la','usr/lib/libusb-1.0.la','usr/bin/sdl2-config',
                     'usr/lib/pkgconfig/sdl2.pc','usr/lib/pkgconfig/libusb-1.0.pc']:
        (target/relative).unlink(missing_ok=True)
print('Built pinned SDL2 HIDAPI/libusb; DirectFB/no-GPU options retained. Log:',Q/'build.log')
