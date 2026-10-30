#!/usr/bin/env python3
"""Private one-CPU MIPS Linux: real DirectFB window/QMP input and bounded offscreen scene."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import zipfile

PROJECT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(PROJECT / "tools/hwtest"))
from mips_boot_smoke import Newc

INIT = r'''#!/bin/sh
exec </dev/console >/dev/console 2>&1
/bin/busybox --install -s /bin
export PATH=/bin:/usr/bin
mkdir -p /proc /sys /dev /tmp /run /storage
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
echo "SCENE_VM_MEM_KB=$(awk '/MemTotal:/ {print $2}' /proc/meminfo)"
disk=/dev/sda
[ -b "$disk" ] || disk=/dev/hda
mount -t vfat "$disk" /storage || { echo 'FAIL scene mount'; poweroff -f; }
APP='/storage/independent-apps/SM64 Bob Scene - NO GAMEPLAY'
export LD_LIBRARY_PATH="$APP/runtime/lib:/usr/lib:/lib"
export SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software
export DFBARGS='system=fbdev,fbdev=/dev/fb0,no-hardware,no-cursor,no-sighandler,quiet,no-vt,mode=640x480,depth=24,pixelformat=RGB24,disable-module=keyboard,linux-input-force'
cd "$APP"
mkdir -p .sm64
FAIL=0
(ulimit -v 32768; ./runtime/bin/sm64-scene-dev --original original --pack .sm64/bob-us-v1.pdat --headless --frames 4 --auto-orbit --snapshot /storage/headless.bmp)
[ "$?" -eq 0 ] || FAIL=1
echo 'SCENE_VM_WINDOW_START'
(ulimit -v 49152; /usr/bin/scene-qa --pack .sm64/bob-us-v1.pdat --frames 600 --vm-input --snapshot /storage/window.bmp)
[ "$?" -eq 0 ] || FAIL=1
echo 'SCENE_VM_WINDOW_END'
sha1sum original/*
sync
cd /
umount /storage || FAIL=1
echo "SCENE_VM_RESULT=$FAIL"
poweroff -f
'''


def run(command):
    result = subprocess.run(list(map(str, command)), text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout


def initramfs(sdk, qa, path, boot=INIT):
    target = sdk / "target"; archive = Newc()
    directories = {"bin", "lib", "usr", "usr/bin", "usr/lib", "dev", "proc", "sys", "tmp", "run", "storage"}
    files = {"bin/busybox": target / "bin/busybox", "lib/libc.so": target / "lib/libc.so", "usr/bin/scene-qa": qa}
    # Existing platform dependencies only, never rebuild/write SDK target.
    for name in ["libdirectfb-1.7.so.7", "libfusion-1.7.so.7", "libdirect-1.7.so.7"]:
        files["usr/lib/" + name] = (target / "usr/lib" / name).resolve()
    for folder in ("usr/lib/directfb-1.7-7", "usr/share/directfb-1.7.7"):
        for source in (target / folder).rglob("*"):
            if source.is_file(): files[source.relative_to(target).as_posix()] = source.resolve()
    # Copy the small transitive library closure from the existing SDK.
    readelf = sdk / "host/bin/mipsel-buildroot-linux-musl-readelf"
    pending = list(files.values()); checked = set()
    while pending:
        source = pending.pop()
        if source in checked: continue
        checked.add(source)
        result = subprocess.run([str(readelf), "-d", str(source)], text=True, capture_output=True)
        for needed in re.findall(r"\(NEEDED\).*\[([^]]+)\]", result.stdout):
            if needed == "libc.so": continue
            if "usr/lib/" + needed in files or "lib/" + needed in files: continue
            candidates = [target / "usr/lib" / needed, target / "lib" / needed]
            found = next((p for p in candidates if p.is_file()), None)
            if found is None: raise RuntimeError("missing platform VM dependency " + needed)
            destination = ("lib/" if found.parent.name == "lib" and found.parent.parent == target else "usr/lib/") + needed
            files[destination] = found.resolve(); pending.append(found.resolve())
    for name in files:
        parent = Path(name).parent
        while str(parent) != ".":
            directories.add(parent.as_posix()); parent = parent.parent
    for directory in sorted(directories, key=lambda value: (value.count("/"), value)):
        archive.dir(directory)
    archive.chrdev("dev/console", 5, 1)
    for name, source in files.items(): archive.file(name, source.read_bytes(), 0o755)
    archive.symlink("bin/sh", "busybox"); archive.symlink("lib/ld-musl-mipsel.so.1", "libc.so")
    archive.file("init", boot, 0o755)
    path.write_bytes(gzip.compress(archive.build(), compresslevel=1))


class Qmp:
    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(5); self.socket.connect(str(path))
        self.stream = self.socket.makefile("rwb", buffering=0)
        self.receive(); self.call("qmp_capabilities")
    def receive(self):
        while True:
            line = self.stream.readline()
            if not line: raise RuntimeError("QMP closed")
            message = json.loads(line)
            if "event" not in message: return message
    def call(self, command, arguments=None):
        self.stream.write((json.dumps(dict(execute=command, arguments=arguments or {})) + "\n").encode())
        reply = self.receive()
        if "error" in reply: raise RuntimeError(str(reply))
        return reply
    def key(self, key):
        self.call("send-key", dict(keys=[dict(type="qcode", data=key)], **{"hold-time": 180}))
    def close(self):
        self.stream.close(); self.socket.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--qa", type=Path, required=True)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--keep-private", type=Path)
    args = parser.parse_args(); args.out.mkdir(parents=True, exist_ok=True)
    original = hashlib.sha256(args.rom.read_bytes()).hexdigest()
    with tempfile.TemporaryDirectory(prefix="sm64-private-window-vm-") as temporary:
        work = Path(temporary); mount = work / "mount"; mount.mkdir()
        disk = work / "scene.fat"
        with disk.open("wb") as file: file.truncate(64 * 1024 * 1024)
        run(["mkfs.vfat", "-F", "32", disk])
        run(["mount", "-o", "loop", disk, mount])
        try:
            app = mount / "independent-apps/SM64 Bob Scene - NO GAMEPLAY"; app.mkdir(parents=True)
            with zipfile.ZipFile(args.package) as archive: archive.extractall(app)
            shutil.copyfile(args.rom, app / "original/arbitrary.private")
        finally: run(["umount", mount])
        ramdisk = work / "init.cpio.gz"; initramfs(args.sdk, args.qa, ramdisk)
        log = work / "serial.log"; qmp_path = work / "qmp.sock"
        command = [str(args.qemu), "-M", "malta", "-cpu", "74Kf-lilsmoke", "-m", "120", "-smp", "1",
                   "-kernel", str(args.sdk / "images/vmlinux"), "-initrd", str(ramdisk),
                   "-append", "console=ttyS0 rdinit=/init", "-drive", f"file={disk},format=raw,if=ide",
                   "-vga", "cirrus", "-device", "usb-kbd", "-display", "none", "-serial", "stdio", "-monitor", "none",
                   "-qmp", f"unix:{qmp_path},server=on,wait=off", "-no-reboot"]
        with log.open("w") as output:
            process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
            qmp = None
            try:
                deadline = time.monotonic() + 150; sent = False
                while time.monotonic() < deadline and process.poll() is None:
                    text = log.read_text()
                    if "SceneInputReady=1" in text and not sent:
                        qmp = Qmp(qmp_path)
                        for _ in range(3): qmp.key("right"); time.sleep(0.5)
                        qmp.key("esc"); sent = True
                    if "SCENE_VM_RESULT=" in text and "System halted" in text: break
                    time.sleep(0.1)
                text = log.read_text()
                assert "SCENE_VM_RESULT=0" in text and "SceneVMInput=PASS" in text, text[-9000:]
                assert "Driver=directfb" in text and "ScenePresentation=SDL-offscreen-test-surface" in text
            finally:
                if qmp: qmp.close()
                if process.poll() is None:
                    process.terminate()
                    try: process.wait(timeout=5)
                    except subprocess.TimeoutExpired: process.kill(); process.wait()
                shutil.copyfile(log, args.out / "native-scene-vm.log")
        fsck = run(["fsck.vfat", "-n", disk]); (args.out / "native-scene-vm-fsck.log").write_text(fsck)
        run(["mount", "-o", "loop,ro", disk, mount])
        try:
            assert hashlib.sha256((mount / "independent-apps/SM64 Bob Scene - NO GAMEPLAY/original/arbitrary.private").read_bytes()).hexdigest() == original
            if args.keep_private:
                args.keep_private.mkdir(parents=True, exist_ok=True)
                for name in ("headless.bmp", "window.bmp"): shutil.copyfile(mount / name, args.keep_private / name)
        finally: run(["umount", mount])
        report = dict(status="PASS", machine="QEMU Malta", cpus=1, ram_mib=120,
                      guest_usable_kib=int(re.search(r"SCENE_VM_MEM_KB=(\d+)", text)[1]),
                      scene_peak_rss_kib=list(map(int, re.findall(r"ScenePeakRSSKB=(\d+)", text))),
                      offscreen_address_space_cap_mib=32, directfb_address_space_cap_mib=48,
                      directfb_window=True, qmp_keyboard_camera_quit=True, original_rom_unchanged=True,
                      data_retained=False, physical_m8_tested=False, mario_gameplay=False)
        (args.out / "native-scene-vm.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report, indent=2))
    assert hashlib.sha256(args.rom.read_bytes()).hexdigest() == original


if __name__ == "__main__":
    main()
