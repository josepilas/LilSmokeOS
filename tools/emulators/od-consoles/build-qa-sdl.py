#!/usr/bin/env python3
"""Build a test-only headless SDL2 from the SDK's exact persisted source."""
import os
from pathlib import Path
import shutil
import subprocess
root = Path("/root/lilsmoke-emulator-expansion-20261008/od-console-qa-runtime")
sdk = Path("/root/lilsmoke-os/build/buildroot/output")
source = root / "SDL2-2.28.5"
root.mkdir(parents=True, exist_ok=True)
if not source.exists():
    shutil.copytree(sdk / "build/sdl2-2.28.5", source,
        ignore=shutil.ignore_patterns(".libs", "build", "*.o", "*.lo", "*.la",
                                     "config.status", "config.log", "Makefile", "libtool"))
work = root / "build"
work.mkdir(exist_ok=True)
env = os.environ.copy()
env.update(CC=str(sdk / "host/bin/mipsel-linux-gcc"),
           CXX=str(sdk / "host/bin/mipsel-linux-g++"),
           CFLAGS="-O1 -fPIC -mfp32 -mhard-float", CXXFLAGS="-O1 -fPIC -mfp32 -mhard-float",
           PKG_CONFIG="false", PATH="/usr/bin:/bin")
options = [
    "--host=mipsel-buildroot-linux-musl", "--prefix=" + str(root / "runtime"),
    "--disable-static", "--enable-shared", "--enable-video-dummy", "--enable-dummyaudio",
    "--disable-video-x11", "--disable-video-wayland", "--disable-video-opengl",
    "--disable-video-opengles", "--disable-video-directfb", "--disable-video-kmsdrm",
    "--disable-alsa", "--disable-pulseaudio", "--disable-esd", "--disable-oss",
    "--disable-sndio", "--disable-jack", "--disable-nas", "--disable-libudev",
    "--disable-haptic", "--disable-sensor"
]
with (root / "build.log").open("a") as log:
    if not (work / "Makefile").exists():
        subprocess.run([str(source / "configure"), *options], cwd=work, env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(["make", "-j1"], cwd=work, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(["make", "install"], cwd=work, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
print(root / "runtime/lib/libSDL2-2.0.so.0", flush=True)
