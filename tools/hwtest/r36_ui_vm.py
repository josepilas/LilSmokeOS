#!/usr/bin/env python3
"""Actual UI in a 48 MiB MIPS guest; NOT an HC16xx boot/scanout emulator."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import socket
import subprocess
import time
from PIL import Image
from mips_boot_smoke import Newc

B = Path("/root/lilsmoke-os/build/buildroot/output")
P = Path(__file__).resolve().parents[2]
QEMU = Path("/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel")


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--ui", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("--sdk-squashfs", type=Path,
                   help="Optional read-only SDK snapshot, avoiding executable mmap over 9p")
    p.add_argument("--startup-timeout", type=int, default=600)
    a = p.parse_args()
    if not 30 <= a.startup_timeout <= 900:
        p.error("--startup-timeout must be between 30 and 900 seconds")
    if a.output.exists():
        raise ValueError("Evidence directory must be new")
    a.output.mkdir(parents=True)
    shutil.copyfile(a.ui, a.output / "ui")
    (a.output / "ui").chmod(0o755)
    fixed = a.output / "fixed"
    fixed.mkdir()
    for stem in ("libdirect", "libfusion"):
        source = Path("/root/lilsmoke-directfb-dispatch-20261021-qualified/payload") / (stem + "-1.7.so.7.0.0")
        shutil.copyfile(source, fixed / (stem + "-1.7.so.7"))
    arc = Newc()
    for name in ("bin", "dev", "lib", "proc", "sys", "sdk", "qa", "etc",
                 "tmp", "run", "persist", "storage", "root"):
        arc.dir(name)
    arc.chrdev("dev/console", 5, 1)
    arc.file("bin/busybox", (B / "target/bin/busybox").read_bytes(), 0o755)
    arc.file("lib/libc.so", (B / "target/lib/libc.so").read_bytes(), 0o755)
    arc.symlink("lib/ld-musl-mipsel.so.1", "libc.so")
    arc.symlink("usr", "/sdk/usr")
    rc = (P / "board/qemu-mipsel/directfbrc").read_text().replace("856x480", "640x480")
    arc.file("etc/directfbrc", rc.replace("8388608", "2097152"))
    init = """#!/bin/busybox sh
exec </dev/console >/dev/console 2>&1
set -x
export PATH=/bin:/sdk/bin:/sdk/sbin:/usr/bin:/usr/sbin
BB=/bin/busybox
$BB mount -t proc proc /proc
$BB mount -t sysfs sysfs /sys
$BB mount -t devtmpfs devtmpfs /dev
$BB mount -t 9p -o ro,trans=virtio,version=9p2000.L sdk /sdk || exit 70
$BB mount -t 9p -o trans=virtio,version=9p2000.L qa /qa || exit 70
$BB mkdir -p /dev/pts /dev/shm /persist/config /persist/logs /storage/roms /storage/independent-apps /storage/packages /storage/themes /etc/lilsmoke
$BB mount -t devpts devpts /dev/pts
$BB mount -t tmpfs -o size=4m tmpfs /dev/shm
$BB cp /sdk/etc/lilsmoke/lilsmoke.conf.default /persist/config/lilsmoke.conf
echo 'DISPLAY_WIDTH=640
DISPLAY_HEIGHT=480
DEVICE_MODE=handheld
DEFAULT_THEME=default
UI_RENDER_MODE=native
ASSET_CACHE_MB=4
SHOW_CLOCK=off
SHOW_BATTERY=off
SCAN_ON_BOOT=off' >> /persist/config/lilsmoke.conf
export HOME=/persist SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy
export LD_LIBRARY_PATH=/qa/fixed:/sdk/usr/lib:/sdk/lib
export DFBARGS=no-hardware,no-cursor,no-sighandler,quiet,vt,no-vt-switch,no-vt-switching,disable-module=keyboard,linux-input-force
export LILSMOKE_UI_NATIVE_DAMAGE=0 LILSMOKE_HCFB=0 LILSMOKE_DRAW_CLOCK=0 LILSMOKE_SHOW_BATTERY=off
unset DISPLAY LD_PRELOAD
ulimit -c 0
/qa/ui >/qa/ui.log 2>&1 &
pid=$!
echo QA_UI_PID=$pid
i=0
while $BB kill -0 "$pid" 2>/dev/null && [ "$i" -lt 900 ]; do
    $BB cat /proc/meminfo > /qa/memory-last.txt
    $BB cat /proc/"$pid"/status > /qa/ui-status-last.txt
    [ ! -f /qa/stop ] || break
    $BB sleep 1
    i=$((i+1))
done
$BB kill -TERM "$pid" 2>/dev/null
wait "$pid"
echo QA_UI_VM_STOPPED
$BB poweroff -f
while :; do $BB sleep 60; done
"""
    if a.sdk_squashfs:
        if not a.sdk_squashfs.is_file():
            p.error("--sdk-squashfs must exist")
        init = init.replace(
            "$BB mount -t 9p -o ro,trans=virtio,version=9p2000.L sdk /sdk || exit 70",
            "$BB mount -t squashfs -o ro /dev/vda /sdk || exit 70")
    arc.file("init", init, 0o755)
    (a.output / "init.cpio.gz").write_bytes(gzip.compress(arc.build(), mtime=0))
    qmp = a.output / "qmp.sock"
    command = [str(QEMU), "-M", "malta", "-cpu", "74Kf-lilsmoke", "-smp", "1", "-m", "48",
               "-kernel", str(B / "images/vmlinux"), "-initrd", str(a.output / "init.cpio.gz"),
               "-append", "console=ttyS0,115200 rdinit=/init video=cirrusfb:640x480M-24@60 panic=-1 nowait",
               "-vga", "cirrus", "-device", "usb-kbd", "-display", "none", "-serial", "stdio", "-monitor", "none",
               "-nic", "none", "-no-reboot", "-qmp", f"unix:{qmp},server=on,wait=off"]
    shares = [("qa", a.output, False)]
    if a.sdk_squashfs:
        command += ["-drive", f"if=none,id=sdkdisk,file={a.sdk_squashfs},format=raw,readonly=on",
                    "-device", "virtio-blk-pci,drive=sdkdisk,addr=0x15"]
    else:
        shares.insert(0, ("sdk", B / "target", True))
    for index, (tag, path, ro) in enumerate(shares):
        command += ["-fsdev", f"local,id=s{index},path={path},security_model=none" + (",readonly=on" if ro else ""),
                    "-device", f"virtio-9p-pci,fsdev=s{index},mount_tag={tag},addr=0x14.{index}" + (",multifunction=on" if index == 0 else "")]
    (a.output / "command.json").write_text(json.dumps(command, indent=2))
    log = (a.output / "serial.log").open("w")
    process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
    shots = []
    try:
        deadline = time.monotonic() + a.startup_timeout
        while time.monotonic() < deadline:
            ui = a.output / "ui.log"
            if ui.exists() and "UI: entering main loop" in ui.read_text(errors="replace"):
                break
            if process.poll() is not None:
                raise RuntimeError("Guest stopped before UI startup")
            time.sleep(.5)
        else:
            raise RuntimeError("UI startup deadline exceeded")
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
            client.settimeout(15)
            client.connect(str(qmp))
            stream = client.makefile("rwb", buffering=0)
            json.loads(stream.readline())

            def call(name, arguments=None):
                stream.write((json.dumps(dict(execute=name, arguments=arguments or {})) + "\n").encode())
                while True:
                    value = json.loads(stream.readline())
                    if "error" in value:
                        raise RuntimeError(value["error"])
                    if "return" in value:
                        return value["return"]

            call("qmp_capabilities")
            for label, key in (("home", None), ("application", "ret"), ("home-return", "ctrl")):
                if key:
                    call("send-key", dict(keys=[dict(type="qcode", data=key)], **{"hold-time": 80}))
                time.sleep(2)
                raw = a.output / (label + ".ppm")
                call("screendump", dict(filename=str(raw)))
                with Image.open(raw) as frame:
                    assert frame.size == (640, 480), frame.size
                    colors = frame.convert("RGB").getextrema()
                    assert max(hi-lo for lo, hi in colors) > 20, colors
                    frame.save(a.output / (label + ".png"))
                shots.append(label + ".png")
        (a.output / "stop").write_text("done\n")
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if "QA_UI_VM_STOPPED" in (a.output / "serial.log").read_text(errors="replace"):
                break
            if process.poll() is not None:
                raise RuntimeError("VM exited before UI cleanup marker")
            time.sleep(.2)
        else:
            raise RuntimeError("UI cleanup deadline exceeded")
        assert sha(a.output / "home.png") != sha(a.output / "application.png"), "navigation did not change the view"
        report = dict(passed=True, ram_mib=48, cpus=1, framebuffer="640x480 Cirrus substitute",
                      actual_ui_sha256=sha(a.ui), hardware_boot_verified=False, hardware_fps_verified=False,
                      readonly_file_backed_sdk=True, no_physical_block_devices=True,
                      readonly_sdk_virtual_disk=bool(a.sdk_squashfs),
                      input="QMP -> emulated USB keyboard -> guest evdev",
                      ui_cleanup_marker_observed=True, physical_shutdown_tested=False,
                      screenshots=shots, ui_log=(a.output / "ui.log").read_text(),
                      memory=(a.output / "memory-last.txt").read_text(),
                      process_status=(a.output / "ui-status-last.txt").read_text())
        (a.output / "report.json").write_text(json.dumps(report, indent=2))
        print("R36_UI_VM_OK ram=48MiB canvas=640x480 screenshots=" + str(len(shots)))
    except Exception as error:
        (a.output / "report.json").write_text(json.dumps(dict(
            passed=False, error=str(error), ram_mib=48, cpus=1,
            actual_ui_sha256=sha(a.ui), startup_timeout_seconds=a.startup_timeout,
            hardware_boot_verified=False, hardware_fps_verified=False,
            screenshots=shots), indent=2))
        raise
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        log.close()


if __name__ == "__main__":
    main()
