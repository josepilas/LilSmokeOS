#!/usr/bin/env python3
"""Functional MIPS/128 MiB test of the candidate's file-backed UI runtime.

Malta is a substitute: this deliberately does not claim to boot HC16xx firmware.
The stock card/image are never modified; only a private candidate copy is used.
"""
import argparse
import gzip
import hashlib
import json
import re
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/hwtest"))
from mips_boot_smoke import Newc
from build_stock_candidate import sha
from stock_format import inspect

INIT = r'''#!/bin/sh
exec </dev/console >/dev/console 2>&1
/bin/busybox --install -s /bin
ln -sf /sbin/qa-reboot /bin/reboot
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
export LILSMOKE_NATIVE_TRACE=1
mkdir -p /proc /sys /dev /tmp /mnt/sdcard /dev/shm
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
# The diagnostic loader supplies the volume offsets because Malta does not
# implement the vendor's encoded-MBR MMC driver. This is not an HC16xx loader.
losetup /dev/loop0 -o 1048576 /dev/sda || { echo QA_LOOP_FAILED; poweroff -f; }
mknod /dev/mmcblk0p1 b 7 0
mount -t vfat /dev/mmcblk0p1 /mnt/sdcard || { echo QA_MOUNT_FAILED; poweroff -f; }
losetup /dev/loop1 -o 537919488 /dev/sda || { echo QA_LOOP_FAILED; poweroff -f; }
mknod /dev/mmcblk0p2 b 7 1
mknod /dev/mmcblk0 b 8 0
echo QA_DIAGNOSTIC_LOADER_READY
setsid /mnt/sdcard/cubegm/icube &
entry=$!
(
while ! grep -q 'UI: entering main loop' /mnt/sdcard/lilsmoke/bringup.log; do
    if grep -q 'FallbackReason=' /mnt/sdcard/lilsmoke/bringup.log; then
        cat /mnt/sdcard/lilsmoke/bringup.log; echo QA_UI_FAILED; exit 1
    fi
    sleep 1
done
echo QA_UI_READY
while ! pidof pocketsnes >/dev/null; do sleep 1; done
echo QA_SNES_RUNNING
while [ "$(grep -c 'UI: entering main loop' /mnt/sdcard/lilsmoke/bringup.log)" -lt 2 ]; do sleep 1; done
echo QA_UI_RETURNED
) &
# Commands arrive only via this private QEMU serial console.
/bin/sh
kill -TERM "$entry" 2>/dev/null || true
/bin/busybox killall5 -15
sleep 1
/bin/busybox killall5 -9
sync
umount -a -r
echo QA_FILESYSTEMS_CLOSED
poweroff -f
'''


def run(command):
    result = subprocess.run(list(map(str, command)), text=True, errors="replace", capture_output=True,
                            stdin=subprocess.DEVNULL, timeout=120)
    if result.returncode: raise RuntimeError(f"{command}: {result.stdout}{result.stderr}")
    return result.stdout


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--build", type=Path, required=True)
    p.add_argument("--image", type=Path, required=True)
    p.add_argument("--qemu", type=Path, required=True)
    p.add_argument("--archive", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--extended-controls", action="store_true")
    p.add_argument("--settings-extra-items", type=int, default=7,
                   help="Home entries before Settings, excluding game systems (legacy images use 4)")
    p.add_argument("--fat-persistence", action="store_true",
                   help="exercise the real M8's FAT persistence fallback, despite Malta's ext4 support")
    p.add_argument("--retroarch-menu", action="store_true")
    p.add_argument("--opendingux", action="store_true")
    p.add_argument("--pcsx-menu", action="store_true", help="open PCSX4ALL's native application menu through the home UI")
    p.add_argument("--opk-qa", type=Path, help="original disposable native OPK fixture; never shipped on the real card")
    p.add_argument("--gba", type=Path, help="user GBA ZIP, tested both packed and unpacked")
    p.add_argument("--md", type=Path, help="user Mega Drive ZIP, tested both packed and unpacked")
    p.add_argument("--menu-cycles", type=int, choices=range(1, 5), default=1)
    p.add_argument("--pointer-smoke", action="store_true", help="exercise a guest-only USB mouse pointer")
    p.add_argument("--addons", type=Path, help="production ROM-partition payload, seeded offline after guest growth")
    p.add_argument("--native-new-menus", action="store_true", help="open new native apps through the real home UI")
    p.add_argument("--native-subset", default="nxengine,easyrpg,scummvm,mgba")
    p.add_argument("--user-gba-bios",type=Path,help="private user BIOS for this disposable QA card only")
    p.add_argument("--native-only", action="store_true", help="focus on native application flows without replaying old regressions")
    p.add_argument("--applications", type=Path, help="directory containing the bundled Button Tester .lilpck fixtures; full Explorer/installer/app flow")
    p.add_argument('--application-version',default='1.0.0')
    p.add_argument('--application-update-version',default='1.1.0')
    a = p.parse_args()
    if a.pcsx_menu and not (a.opendingux and a.retroarch_menu):
        p.error('--pcsx-menu requires --opendingux and --retroarch-menu')
    if a.opk_qa and (not a.opendingux or not a.opk_qa.is_file()): p.error("--opk-qa requires --opendingux and an existing fixture")
    for path in (a.image, a.archive, a.qemu, a.build / "images/vmlinux"):
        if not path.is_file(): p.error("missing test input: " + str(path))
    a.image = a.image.resolve(strict=True)
    a.out.mkdir(parents=True, exist_ok=False)
    original_hash = sha(a.image)
    initial_layout = inspect(a.image)
    rom_offset = initial_layout["partitions"][1]["offset_bytes"]
    os_bytes = initial_layout["partitions"][0]["sectors"] * 512
    rom_bytes = initial_layout["partitions"][1]["sectors"] * 512
    events = []
    with tempfile.TemporaryDirectory(prefix="m8-runtime-qa-", dir="/root") as temp:
        work = Path(temp)
        image = work / "card.img"; shutil.copyfile(a.image, image)
        # Exercise first-boot growth on a larger private virtual card.
        with image.open("r+b") as stream: stream.truncate(4 * 1024 * 1024 * 1024)
        offset = f"{image}@@{rom_offset}"
        if subprocess.run(["mdir", "-i", offset, "::/roms/snes"], capture_output=True).returncode:
            run(["mmd", "-i", offset, "::/roms/snes"])
        run(["mcopy", "-i", offset, a.archive, "::/roms/snes/SMW-test.7z"])
        if a.applications:
            run(["mmd", "-i", offset, "::/! Applications QA"])
            for version in (a.application_version,a.application_update_version):
                package=a.applications/("Button-Tester-"+version+".lilpck")
                if not package.is_file():p.error("missing bundled app fixture: "+str(package))
                run(["mcopy", "-i", offset, package, "::/! Applications QA/"+package.name])
        if a.opk_qa:
            for directory in ("::/opendingux", "::/opendingux/apps"):
                if subprocess.run(["mdir", "-i", offset, directory], capture_output=True).returncode:
                    run(["mmd", "-i", offset, directory])
            run(["mcopy", "-i", offset, a.opk_qa, "::/opendingux/apps/native-qa.opk"])
        systems = ["snes"]
        extra_games = []
        for system, source in (("gba", a.gba), ("md", a.md)):
            if not source: continue
            if not source.is_file(): p.error("missing user ROM: " + str(source))
            systems.append(system)
            if subprocess.run(["mdir", "-i", offset, "::/roms/" + system], capture_output=True).returncode:
                run(["mmd", "-i", offset, "::/roms/" + system])
            run(["mcopy", "-i", offset, source, "::/roms/" + system + "/A-packed.zip"])
            import zipfile
            extensions = (".gba", ".agb") if system == "gba" else (".md", ".gen", ".smd", ".bin")
            with zipfile.ZipFile(source) as z:
                members = sorted(n for n in z.namelist() if n.lower().endswith(extensions))
                if not members: p.error("no supported ROM in " + str(source))
                if z.getinfo(members[0]).file_size > 64 * 1024 * 1024: p.error("oversized QA ROM")
                native = work / ("B-unpacked" + Path(members[0]).suffix.lower())
                native.write_bytes(z.read(members[0]))
                run(["mcopy", "-i", offset, native, "::/roms/" + system + "/" + native.name])
            extra_games.extend((system, index, label) for index, label in enumerate(("packed", "unpacked")))
        systems.sort()
        if a.user_gba_bios:
            if a.user_gba_bios.stat().st_size != 16384:p.error('invalid private GBA BIOS size')
            if subprocess.run(["mdir","-i",offset,"::/bios"],capture_output=True).returncode:
                run(["mmd","-i",offset,"::/bios"])
            run(["mcopy","-i",offset,a.user_gba_bios,"::/bios/gba_bios.bin"])
        # Cirrus/Malta cannot validate HC16xx's real 1280x720 scanout. Only
        # this disposable QA copy uses the diagnostic display geometry.
        defaults = work / "initial.conf"
        run(["mcopy", "-i", f"{image}@@1048576", "::/lilsmoke/initial.conf", defaults])
        with defaults.open("a") as stream:
            stream.write("\nDISPLAY_WIDTH=854\nDISPLAY_HEIGHT=480\nSCAN_ROMS_ON_BOOT=on\n")
        run(["mcopy", "-o", "-i", f"{image}@@1048576", defaults, "::/lilsmoke/initial.conf"])
        # Cirrus cannot use hcfb's required ARGB32 scanout. Change only this
        # disposable diagnostic copy, never the deliverable's M8 settings.
        launcher = work / "m8-launch.sh"
        run(["mcopy", "-i", f"{image}@@1048576", "::/lilsmoke/m8-launch.sh", launcher])
        launcher.write_text(launcher.read_text().replace("depth=32,pixelformat=ARGB", "depth=24,pixelformat=RGB24"))
        if a.fat_persistence:
            launcher.write_text(launcher.read_text().replace("if grep -q 'ext4' /proc/filesystems && mount", "if false && mount"))
        run(["mcopy", "-o", "-i", f"{image}@@1048576", launcher, "::/lilsmoke/m8-launch.sh"])
        manifest = work / "runtime.tsv"
        run(["mcopy", "-i", f"{image}@@1048576", "::/lilsmoke/runtime.tsv", manifest])
        for line in manifest.read_text().splitlines():
            kind, mode, path, target = line.split("\t")
            if kind == "F" and path == "usr/lib/lilsmoke/od-launch":
                wrapper = work / "od-launch"
                remote = "::/lilsmoke/files/" + target
                run(["mcopy", "-i", f"{image}@@1048576", remote, wrapper])
                wrapper.write_text(wrapper.read_text().replace("depth=32,pixelformat=ARGB", "depth=24,pixelformat=RGB24"))
                run(["mcopy", "-o", "-i", f"{image}@@1048576", wrapper, remote])
        archive = Newc()
        for name in ("bin", "sbin", "lib", "dev", "dev/shm", "proc", "sys", "tmp", "mnt", "mnt/sdcard"):
            archive.dir(name)
        archive.chrdev("dev/console", 5, 1)
        archive.file("bin/busybox", (a.build / "target/bin/busybox").read_bytes(), 0o755)
        archive.symlink("bin/sh", "busybox")
        archive.file("lib/libc.so", (a.build / "target/lib/libc.so").read_bytes(), 0o755)
        archive.symlink("lib/ld-musl-mipsel.so.1", "libc.so")
        archive.file("init", INIT.replace("-o 537919488", "-o " + str(rom_offset)), 0o755)
        # The diagnostic PID 1 is a shell, not the stock BusyBox init. Force
        # only this guest's reset when the real launcher requests its restart.
        archive.file("sbin/qa-reboot", "#!/bin/sh\nexec </dev/console >/dev/console 2>&1\nsync\ncd /\nkill -KILL \"$PPID\"\nsleep 1\numount /mnt/sdcard || { echo QA_UNMOUNT_FAILED; exit 1; }\nsync\necho QA_GROWTH_RESTART_REQUIRED\n/bin/busybox reboot -f\n", 0o755)
        # Diagnostic PID 1 is not stock init. This guest-only adapter records
        # the supervisor's request and safely closes the private filesystems.
        archive.file("sbin/poweroff", "#!/bin/sh\necho QA_SAFE_POWEROFF_CALLED >/dev/console\nsync\nkill -TERM \"$PPID\"\nsleep 1\numount -a -r\nsync\necho QA_FILESYSTEMS_CLOSED >/dev/console\n/bin/busybox poweroff -f\n", 0o755)
        initrd = work / "init.cpio.gz"; initrd.write_bytes(gzip.compress(archive.build(), compresslevel=1))
        qmp_path = work / "qmp.sock"
        command = [str(a.qemu), "-M", "malta", "-cpu", "74Kf-lilsmoke", "-smp", "1", "-m", "128",
                   "-kernel", str(a.build / "images/vmlinux"), "-initrd", str(initrd),
                   "-append", "console=ttyS0 rdinit=/init video=cirrusfb:856x480M-24@60 panic=-1",
                   "-drive", f"file={image},format=raw,if=ide", "-vga", "cirrus", "-display", "none",
                   "-serial", "stdio", "-monitor", "none", "-no-reboot", "-device", "usb-kbd",
                   "-qmp", f"unix:{qmp_path},server=on,wait=off"]
        if a.pointer_smoke: command += ["-device", "usb-mouse"]

        def qmp(name, arguments=None):
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
                connection.settimeout(30); connection.connect(str(qmp_path))
                with connection.makefile("rwb", buffering=0) as stream:
                    json.loads(stream.readline())
                    for request in ({"execute": "qmp_capabilities"}, {"execute": name, "arguments": arguments or {}}):
                        stream.write(json.dumps(request).encode() + b"\n")
                        while True:
                            reply = json.loads(stream.readline())
                            if "error" in reply: raise RuntimeError(reply)
                            if "return" in reply: break
                    return reply["return"]

        def key(name, hold=180):
            # A TCG/software menu can poll less often than the real hcfb menu.
            # Hold QA chords across a slow frame instead of losing press+release between polls.
            if "-" in name: hold = max(hold, 1200)
            qmp("human-monitor-command", {"command-line": f"sendkey {name} {hold}"})
            events.append(dict(key=name, hold_ms=hold))
            time.sleep(hold / 1000 + 1)

        def capture(name, retries=3):
            ppm = work / (name + ".ppm")
            qmp("screendump", {"filename": str(ppm)})
            run(["convert", ppm, a.out / (name + ".png")])
            deviation = float(run(["identify", "-format", "%[standard-deviation]", a.out / (name + ".png")]))
            if deviation < 100:
                if retries:
                    time.sleep(5)
                    return capture(name, retries - 1)
                raise RuntimeError("blank or nearly blank framebuffer: " + name)

        def check_native_menus(launches):
            extra=int(bool(a.addons and (a.addons/'emulators/opendingux/regba').is_file()))
            for name,index,exit_key in (("regba",3,"alt"),("nxengine",15+extra,"esc"),("easyrpg",17+extra,"esc"),("scummvm",18+extra,"ctrl-q"),("mgba",3+extra,"esc")):
                if name not in a.native_subset.split(','):continue
                for _ in range(len(systems)+3):key("right")
                key("ret")
                for _ in range(index):key("down",80)
                capture("native-"+name+"-selected")
                key("ret")
                guest_marked("timeout 45 sh -c 'until pidof "+name+" >/dev/null; do sleep 1; done'","QA_NATIVE_"+name+"_RUNNING")
                wait("QA_NATIVE_"+name+"_RUNNING",55)
                time.sleep(8);capture("native-"+name+"-own-menu")
                if name=="mgba" and a.gba:
                    key("ret");time.sleep(15);capture("native-mgba-user-game")
                    key("x");key("ret");time.sleep(3)
                    key("esc");capture("native-mgba-pause-menu")
                    for _ in range(4):key("down")
                    key("ret")
                elif name=="regba" and a.gba:
                    key("ctrl");time.sleep(2);capture("native-regba-browser")
                    key("down");key("ctrl");time.sleep(15);capture("native-regba-user-game")
                    key("ret");key("ctrl");key("right");time.sleep(3)
                    key("end",500)
                    guest_marked("timeout 30 sh -c 'until [ \"$(grep -c REGBA_MENU_ENTER /tmp/lilsmoke-m8-root/persist/logs/od-regba.log)\" -ge 2 ]; do sleep 1; done'","QA_REGBA_PAUSE_ENTERED")
                    wait("QA_REGBA_PAUSE_ENTERED",40);time.sleep(3);capture("native-regba-pause-menu")
                    key("alt");time.sleep(2);key("end",500)
                    # Native Main Menu wraps up from Load ROM to Exit.
                    key("up");capture("native-regba-exit-selected")
                    key("ctrl")
                else:key(exit_key)
                launches+=1
                marker="QA_RETURNED_"+str(launches)
                guest_marked("timeout 45 sh -c 'until [ \"$(grep -c \"UI: entering main loop\" /mnt/sdcard/lilsmoke/bringup.log)\" -ge "+str(launches+1)+" ]; do sleep 1; done'",marker)
                wait(marker,55);time.sleep(3)
            return launches

        log_path = work / "serial.log"
        with log_path.open("wb") as log:
            vm = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT)
            restarts = 0
            def guest_marked(command, marker):
                # Never place the literal marker in the echoed command line.
                encoded = ''.join('\\0%03o' % ord(c) for c in marker)
                vm.stdin.write((command + " && printf '%b\\n' '" + encoded + "'\n").encode())
                vm.stdin.flush()
            try:
                def wait(marker, timeout=300):
                    nonlocal vm, restarts
                    end = time.monotonic() + timeout
                    pattern = r'(?m)^(?:~ # )?' + re.escape(marker) + r'\r?$'
                    while not re.search(pattern, log_path.read_text(errors="replace")):
                        text = log_path.read_text(errors="replace")
                        if marker == "QA_UI_READY" and restarts < 2 and \
                                text.count("QA_GROWTH_RESTART_REQUIRED") > restarts and \
                                (text.count("Reboot failed -- System halted") > restarts or vm.poll() is not None):
                            if vm.poll() is None:
                                qmp("quit"); vm.wait(timeout=15)
                            if vm.stdin: vm.stdin.close()
                            if qmp_path.exists(): qmp_path.unlink()
                            restarts += 1
                            vm = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT)
                            end = time.monotonic() + timeout
                            continue
                        if any(error in text for error in ("QA_UI_FAILED", "QA_MOUNT_FAILED", "QA_LOOP_FAILED", "QA_UNMOUNT_FAILED", "Kernel panic")) or vm.poll() is not None or time.monotonic() > end:
                            if vm.poll() is None:
                                vm.stdin.write(b"cat /mnt/sdcard/lilsmoke/bringup.log /mnt/sdcard/lilsmoke/persist/logs/retroarch.log /tmp/lilsmoke-m8-root/tmp/lilsmoke-retroarch/retroarch.cfg\n"); vm.stdin.flush()
                                time.sleep(2)
                                text = log_path.read_text(errors="replace")
                            raise RuntimeError("guest did not reach " + marker + "\n" + text[-8000:])
                        time.sleep(1)
                wait("QA_UI_READY")
                if a.addons:
                    # Stop this private guest cleanly before changing its FAT.
                    # The source image and physical card are never opened here.
                    vm.stdin.write(b"exit\n"); vm.stdin.flush()
                    wait("reboot: System halted",45)
                    qmp("quit"); vm.wait(timeout=15)
                    vm.stdin.close()
                    shutil.copyfile(log_path,a.out/"first-boot-growth.log")
                    for directory in sorted(a.addons.iterdir()):
                        if not directory.is_dir():raise RuntimeError("expected addon directories")
                        run(["mcopy","-s","-o","-i",offset,directory,"::/"])
                    # The init monitor must not mistake the previous boot's
                    # UI marker for the new UI being ready to receive input.
                    empty_log=work/"fresh-boot.log"
                    empty_log.write_bytes(b"")
                    run(["mcopy","-o","-i",f"{image}@@1048576",empty_log,"::/lilsmoke/bringup.log"])
                    if qmp_path.exists():qmp_path.unlink()
                    log.seek(0);log.truncate(0);log.flush()
                    vm=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=log,stderr=subprocess.STDOUT)
                    wait("QA_UI_READY")
                time.sleep(3); capture("01-native-ui")
                if a.pointer_smoke:
                    for direction, delta in (("right", 60), ("left", -60)):
                        qmp("human-monitor-command", {"command-line": f"mouse_move {delta} 0"})
                        time.sleep(2); capture("01-pointer-" + direction)
                    if (a.out / "01-pointer-right.png").read_bytes() == (a.out / "01-pointer-left.png").read_bytes():
                        raise RuntimeError("pointer movement did not change the guest UI")
                if a.applications:
                    # Only guest keyboard events drive the actual production UI.
                    for _ in range(len(systems)+2):key("right")
                    key("ret");time.sleep(3);capture("apps-01-explorer")
                    key("ret");time.sleep(2);capture("apps-02-package-files")
                    key("ret");time.sleep(5);capture("apps-03-installer-cancel-default")
                    key("ret");time.sleep(2);capture("apps-04-cancel-return-explorer")
                    guest_marked("test ! -e '/tmp/lilsmoke-m8-root/storage/independent-apps/Button Tester'","QA_APP_CANCEL_CLEAN")
                    wait("QA_APP_CANCEL_CLEAN",15)
                    key("ret");time.sleep(5);key("left");key("ret")
                    guest_marked("timeout 60 sh -c 'until test -f \"/tmp/lilsmoke-m8-root/storage/independent-apps/Button Tester/meta.json\"; do sleep 1; done'","QA_APP_INSTALLED")
                    wait("QA_APP_INSTALLED",70);time.sleep(3);capture("apps-05-install-complete")
                    key("ret");key("ret");time.sleep(4);capture("apps-06-already-installed")
                    key("ret");key("alt");key("alt");key("left");key("left");key("ret")
                    guest_marked("timeout 45 sh -c 'until grep -q \"Applications: catalog ready\" /mnt/sdcard/lilsmoke/bringup.log; do sleep 1; done'","QA_APP_GRID_READY")
                    wait("QA_APP_GRID_READY",55)
                    time.sleep(4);capture("apps-07-grid")
                    key("ret")
                    guest_marked("timeout 45 sh -c 'until pidof button-tester >/dev/null; do sleep 1; done'","QA_APP_RUNNING")
                    wait("QA_APP_RUNNING",55);time.sleep(5);capture("apps-08-bundled-native-app")
                    key("right");key("down");key("ret");key("spc");time.sleep(2);capture("apps-09-app-input")
                    key("ret-esc")
                    guest_marked("timeout 45 sh -c 'until [ \"$(grep -c \"UI: entering main loop\" /mnt/sdcard/lilsmoke/bringup.log)\" -ge 2 ]; do sleep 1; done'","QA_APP_RETURNED")
                    wait("QA_APP_RETURNED",55);time.sleep(3);capture("apps-10-home-return")
                    for _ in range(len(systems)+2):key("right")
                    key("ret");key("ret");key("down");key("ret");time.sleep(4)
                    capture("apps-11-update-confirm");key("left");key("ret")
                    guest_marked("timeout 60 sh -c 'until test -f \"/tmp/lilsmoke-m8-root/storage/independent-apps/Button Tester/launch.sh\"; do sleep 1; done' && test ! -e '/tmp/lilsmoke-m8-root/storage/independent-apps/Button Tester/run.sh' && test -f '/tmp/lilsmoke-m8-root/storage/independent-apps/Button Tester/input-test.log'","QA_APP_UPDATED_DATA_PRESERVED")
                    wait("QA_APP_UPDATED_DATA_PRESERVED",70);time.sleep(2);capture("apps-12-update-complete")
                    key("ret");key("alt");key("alt");key("left");key("left");key("ret")
                    guest_marked("timeout 45 sh -c 'until [ \"$(grep -c \"Applications: catalog ready\" /mnt/sdcard/lilsmoke/bringup.log)\" -ge 2 ]; do sleep 1; done'","QA_APP_UPDATED_GRID_READY")
                    wait("QA_APP_UPDATED_GRID_READY",55);key("f4")
                    time.sleep(2);capture("apps-13-options");key("down");key("ret");time.sleep(1);capture("apps-14-uninstall-confirm")
                    key("left");key("ret")
                    guest_marked("timeout 30 sh -c 'while test -e \"/tmp/lilsmoke-m8-root/storage/independent-apps/Button Tester\"; do sleep 1; done'","QA_APP_UNINSTALLED")
                    wait("QA_APP_UNINSTALLED",40);time.sleep(3);capture("apps-15-uninstalled")
                    vm.stdin.write(b"exit\n");vm.stdin.flush()
                elif a.native_only:
                    check_native_menus(0)
                else:
                    for _ in range(systems.index("snes")): key("right")
                    key("ret"); time.sleep(2); capture("02-snes-list")
                    key("ret"); wait("QA_SNES_RUNNING")
                    time.sleep(20); capture("03-snes-running")
                    key("ret", 650); time.sleep(8); capture("04-start-input")
                if a.extended_controls and not a.native_only and not a.applications:
                    key("ret-spc", 250); time.sleep(2); capture("05-emulator-menu")
                    key("alt", 180); time.sleep(2)
                    key("ret-esc", 250); wait("QA_UI_RETURNED")
                    time.sleep(3); capture("06-returned-ui")
                    launches = 1
                    for system, index, label in extra_games:
                        for _ in range(systems.index(system)): key("right")
                        key("ret")
                        if index: key("right")
                        key("ret")
                        marker = "QA_" + system + "_" + label + "_RUNNING"
                        guest_marked("timeout 90 sh -c 'until pidof retroarch >/dev/null; do sleep 1; done'", marker)
                        wait(marker, 100)
                        time.sleep(20); capture(system + "-" + label + "-running", retries=6)
                        key("ret", 650); time.sleep(8); key("spc", 300)
                        capture(system + "-" + label + "-buttons")
                        for cycle in range(a.menu_cycles):
                            key("ret-spc", 300); time.sleep(3)
                            capture(system + "-" + label + "-menu" + ("" if cycle == 0 else "-" + str(cycle + 1)))
                            key("down"); key("up")
                            if cycle + 1 < a.menu_cycles:
                                key("ret-spc", 300); time.sleep(3)
                                capture(system + "-" + label + "-resumed-" + str(cycle + 1), retries=4)
                        key("ret-esc", 300)
                        launches += 1
                        marker = "QA_RETURNED_" + str(launches)
                        guest_marked("timeout 90 sh -c 'until [ \"$(grep -c \"UI: entering main loop\" /mnt/sdcard/lilsmoke/bringup.log)\" -ge " + str(launches + 1) + " ]; do sleep 1; done' && cp /mnt/sdcard/lilsmoke/persist/logs/retroarch.log /mnt/sdcard/lilsmoke/persist/logs/retroarch-" + system + "-" + label + ".log", marker)
                        wait(marker, 100); time.sleep(3); capture(system + "-" + label + "-return")
                    if a.retroarch_menu:
                        for _ in range(len(systems)+1): key("right")
                        key("ret"); key("ret"); time.sleep(8)
                        capture("06a-retroarch-menu", retries=6)
                        key("down"); key("down"); capture("06b-retroarch-pad-navigation")
                        key("ret-esc", 300)
                        launches += 1
                        marker = "QA_RETURNED_" + str(launches)
                        guest_marked("timeout 60 sh -c 'until [ \"$(grep -c \"UI: entering main loop\" /mnt/sdcard/lilsmoke/bringup.log)\" -ge " + str(launches + 1) + " ]; do sleep 1; done'", marker)
                        wait(marker, 70); time.sleep(3)
                        if a.pcsx_menu:
                            for _ in range(len(systems) + 3): key("right")
                            key("ret"); key("down"); key("ret")
                            guest_marked("timeout 60 sh -c 'until pidof pcsx4all-launch >/dev/null; do sleep 1; done'", "QA_PCSX_NATIVE_MENU_RUNNING")
                            wait("QA_PCSX_NATIVE_MENU_RUNNING", 70)
                            time.sleep(3); capture("06i-pcsx-native-menu")
                            key("alt")
                            launches += 1
                            marker = "QA_RETURNED_" + str(launches)
                            guest_marked("timeout 60 sh -c 'until [ \"$(grep -c \"UI: entering main loop\" /mnt/sdcard/lilsmoke/bringup.log)\" -ge " + str(launches + 1) + " ]; do sleep 1; done'", marker)
                            wait(marker, 70); time.sleep(3)
                        if a.opk_qa:
                            guest_marked("chroot /tmp/lilsmoke-m8-root /usr/lib/lilsmoke/lilsmoke-opendingux scan | tee /mnt/sdcard/lilsmoke/opk-qa-catalog.tsv | awk -F '\\t' '$2 == \"Native OPK QA\" && $4 == \"\" {found=1} END {exit !found}'", "QA_NATIVE_OPK_AVAILABLE")
                            wait("QA_NATIVE_OPK_AVAILABLE", 20)
                            for _ in range(len(systems) + 3): key("right")
                            key("ret"); key("down"); key("down")
                            capture("06h-native-opk-catalog")
                            key("ret"); time.sleep(5); capture("06i-native-opk-running", retries=3)
                            key("right"); key("down"); key("ret", 300)
                            capture("06j-native-opk-buttons")
                            key("ret-shift_r", 300)
                            launches += 1
                            marker = "QA_RETURNED_" + str(launches)
                            guest_marked("timeout 60 sh -c 'until [ \"$(grep -c \"UI: entering main loop\" /mnt/sdcard/lilsmoke/bringup.log)\" -ge " + str(launches + 1) + " ]; do sleep 1; done'", marker)
                            wait(marker, 70); time.sleep(3)
                        capture("06c-retroarch-return")
                        if a.gba:
                            guest_marked("cp /tmp/lilsmoke-m8-root/storage/roms/gba/B-unpacked.gba /tmp/lilsmoke-m8-root/storage/roms/gba/C-newly-added.gba", "QA_NEW_ROM_COPIED")
                            wait("QA_NEW_ROM_COPIED", 15)
                        for _ in range(len(systems) + 4): key("right")
                        key("ret"); key("down"); key("ret"); time.sleep(3)
                        capture("06d-scan-games-now")
                    if a.opendingux:
                        for _ in range(len(systems) + 3): key("right")
                        key("ret"); time.sleep(2); capture("06e-opendingux-apps")
                        key("ret")
                        guest_marked("timeout 60 sh -c 'until pidof pocketsnes >/dev/null; do sleep 1; done'", "QA_OD_POCKET_RUNNING")
                        wait("QA_OD_POCKET_RUNNING", 70)
                        guest_marked("timeout 60 sh -c 'until grep -q POCKETSNES_OWN_MENU_ENTER /tmp/lilsmoke-m8-root/persist/logs/pocketsnes.log; do sleep 1; done'", "QA_OD_OWN_MENU_ENTERED")
                        wait("QA_OD_OWN_MENU_ENTERED", 70)
                        time.sleep(3); capture("06f-opendingux-native-menu")
                        # PocketSNES's own Main Menu, not a LilSmoke ROM selector.
                        for _ in range(5): key("down")
                        key("ctrl"); time.sleep(3); capture("06g-opendingux-native-browser")
                        # Browser starts on '/' then lists '.', '..', and the ROM.
                        for _ in range(3): key("down")
                        key("ctrl")
                        guest_marked("timeout 60 sh -c 'until [ \"$(grep -c POCKETSNES_NATIVE_BROWSER_LOAD /tmp/lilsmoke-m8-root/persist/logs/pocketsnes.log)\" -ge 2 ]; do sleep 1; done'", "QA_OD_BROWSER_ARCHIVE_LOADED")
                        wait("QA_OD_BROWSER_ARCHIVE_LOADED", 70)
                        time.sleep(20); capture("06h-opendingux-game", retries=4)
                        key("ret", 650)
                        key("ret-esc", 300)
                        launches += 1
                        marker = "QA_RETURNED_" + str(launches)
                        guest_marked("timeout 60 sh -c 'until [ \"$(grep -c \"UI: entering main loop\" /mnt/sdcard/lilsmoke/bringup.log)\" -ge " + str(launches + 1) + " ]; do sleep 1; done'", marker)
                        wait(marker, 70); time.sleep(3)
                    if a.native_new_menus:
                        launches=check_native_menus(launches)
                    for _ in range(len(systems) + a.settings_extra_items): key("right")
                    key("ret"); time.sleep(2); capture("07-settings")
                    if a.opendingux:
                        key("down"); key("down"); key("right")
                        capture("07a-global-opendingux")
                        key("ret"); capture("07b-emulator-systems")
                        key("down"); key("ret"); capture("07c-snes-engine-options")
                        for _ in range(3 if a.addons else 2):key("down")
                        key("ret"); capture("07d-snes-pocketsnes-selected")
                        if a.gba:
                            for _ in range(4): key("down")
                            key("ret"); key("down"); key("ret")
                            capture("07e-gba-retroarch-override")
                        key("alt")
                        guest_marked("grep -q '^EMULATOR_BACKEND=opendingux$' /tmp/lilsmoke-m8-root/persist/config/lilsmoke.conf && grep -q '^EMULATOR_snes=od:pocketsnes$' /tmp/lilsmoke-m8-root/persist/config/lilsmoke.conf", "QA_EMULATOR_SETTINGS_SAVED")
                        wait("QA_EMULATOR_SETTINGS_SAVED", 15)
                        key("left")
                        guest_marked("grep -q '^EMULATOR_BACKEND=retroarch$' /tmp/lilsmoke-m8-root/persist/config/lilsmoke.conf && grep -q '^EMULATOR_snes=$' /tmp/lilsmoke-m8-root/persist/config/lilsmoke.conf", "QA_GLOBAL_POLICY_RESET")
                        wait("QA_GLOBAL_POLICY_RESET", 15)
                        key("up"); key("up")
                    key("ret"); capture("08-poweroff-cancel-default")
                    key("alt"); key("ret"); key("left")
                    capture("09-poweroff-confirm")
                    key("ret"); wait("QA_SAFE_POWEROFF_CALLED", 45)
                elif not a.applications:
                    vm.stdin.write(b"exit\n"); vm.stdin.flush()
                wait("reboot: System halted", 45)
                qmp("quit"); vm.wait(timeout=15)
            finally:
                if vm.poll() is None:
                    try:
                        vm.stdin.write(b"echo QA_FAILURE_DIAGNOSTICS; cat /mnt/sdcard/lilsmoke/persist/logs/retroarch.log /tmp/lilsmoke-m8-root/tmp/lilsmoke-retroarch/retroarch.cfg; sync\n")
                        vm.stdin.flush(); time.sleep(4)
                    except (BrokenPipeError, OSError): pass
                    vm.terminate()
                try: vm.wait(timeout=15)
                except subprocess.TimeoutExpired: vm.kill(); vm.wait()
                log.flush(); shutil.copyfile(log_path, a.out / "serial.log")
                # Preserve application diagnostics even if navigation fails.
                if a.fat_persistence:
                    subprocess.run(["mcopy", "-s", "-i", f"{image}@@1048576", "::/lilsmoke/persist/logs", str(a.out / "failure-logs")], capture_output=True)
                    subprocess.run(["mcopy","-o","-i",f"{image}@@1048576","::/lilsmoke/bringup.log",str(a.out/"boot-diagnostic.log")],capture_output=True)
        mount = work / "mount"; mount.mkdir()
        run(["mount", "-o", "loop,ro,offset=1048576", image, mount])
        try:
            shutil.copyfile(mount / "lilsmoke/bringup.log", a.out / "bringup.log")
            run(["e2fsck", "-fn", mount / "lilsmoke/persist.ext4"])
            persistence = mount / "lilsmoke/persist" if a.fat_persistence else work / "persist"
            if not a.fat_persistence:
                persistence.mkdir()
                run(["mount", "-o", "loop,ro", mount / "lilsmoke/persist.ext4", persistence])
            try:
                shutil.copytree(persistence / "logs", a.out / "application-logs")
                bringup = (a.out / "bringup.log").read_text(errors="replace")
                if a.opk_qa:
                    journals = list((persistence / "opendingux/home").glob("*/opk-qa.log"))
                    if len(journals) != 1: raise RuntimeError("native OPK did not create its persistent HOME journal")
                    journal = journals[0].read_text()
                    for marker in ("OPK_QA_READY", "OPK_QA_KEY", "OPK_QA_CHORD", "OPK_QA_EXIT 0"):
                        if marker not in journal: raise RuntimeError("missing native OPK evidence: " + marker)
                    shutil.copyfile(journals[0], a.out / "native-opk-qa.log")
                if a.retroarch_menu and "Library: manual scan" not in bringup:
                    raise RuntimeError("Settings did not request a fresh game scan")
                if a.gba and a.retroarch_menu:
                    playlist = json.loads((persistence / "playlists/LilSmoke-gba.lpl").read_text())
                    if len(playlist['items']) != 3:
                        raise RuntimeError("manual scan did not discover the newly added GBA ROM")
                for system, index, label in ([] if a.native_only or a.applications else extra_games):
                    game_log = (persistence / ("logs/retroarch-" + system + "-" + label + ".log")).read_text(errors="replace")
                    for marker in ("[lilsmoke]: native evdev active", "[lilsmoke audio]", "rom=", "[Core]: Unloading"):
                        if marker not in game_log: raise RuntimeError("missing " + system + " " + label + " evidence: " + marker)
                if a.extended_controls:
                    engine_log = (persistence / "logs/pocketsnes.log").read_text(errors="replace")
                    for marker in ("POCKETSNES_MENU_ENTER", "POCKETSNES_EXIT_REQUEST",
                                   "POCKETSNES_EXIT_APP cleanup=1", "POCKETSNES_PERF"):
                        if marker not in engine_log:
                            raise RuntimeError("missing functional engine evidence: " + marker)
                    if a.opendingux and 'POCKETSNES_OWN_MENU_ENTER' not in engine_log:
                        raise RuntimeError('native OpenDingux home menu not entered')
            finally:
                if not a.fat_persistence: run(["umount", persistence])
        finally:
            run(["umount", mount])
        # Check the guest-written FAT volumes after unmount, never repair them.
        loop = run(["losetup", "--find", "--show", "--offset", "1048576", "--sizelimit", str(os_bytes), image]).strip()
        try: (a.out / "lilos-fsck.log").write_text(run(["fsck.vfat", "-n", loop]))
        finally: run(["losetup", "-d", loop])
        rom_bytes = inspect(image)["partitions"][1]["sectors"] * 512
        loop = run(["losetup", "--find", "--show", "--offset", str(rom_offset), "--sizelimit", str(rom_bytes), image]).strip()
        try: (a.out / "lilroms-fsck.log").write_text(run(["fsck.vfat", "-n", loop]))
        finally: run(["losetup", "-d", loop])
    if sha(a.image) != original_hash: raise RuntimeError("candidate source image changed")
    report = dict(passed=True, ram_mb=128, cpu_count=1, image_sha256=original_hash, events=events,
                  growth_restarts=restarts,
                  extended_controls_tested=a.extended_controls,
                  fat_persistence_tested=a.fat_persistence,
                  retroarch_menu_tested=a.retroarch_menu,
                  opendingux_home_and_policy_tested=a.opendingux,
                  native_opk_extraction_launch_input_home_tested=bool(a.opk_qa),
                  production_addon_payload_present=bool(a.addons),
                  new_native_apps_launched_through_ui=a.native_new_menus,
                  gba_packed_and_plain_tested=bool(a.gba), md_packed_and_plain_tested=bool(a.md),
                  menu_cycles_per_game=a.menu_cycles,
                  mouse_style_pointer_tested=a.pointer_smoke,
                  stock_kernel_boot_tested=False, physical_device_tested=False, audio_tested=False,
                  tests=["metadata-only RAM root", "file-backed native LilSmokeUI", "SNES menu via UI",
                         "first-boot FAT growth preserves seeded ROM",
                         "PocketSNES from 7z", "Start input via QMP", "clean shutdown/filesystems", "source image unchanged"],
                  limits="Malta/Cirrus substitutes, 854x480 RGB24 QA-only scanout, diagnostic volume/shutdown adapters; not HC16xx peripheral, stock-init shutdown or cycle-accurate performance verification.")
    if a.extended_controls:
        report["tests"] += ["Start+X emulator menu", "B resumes game", "Start+Select clean exit to UI",
                            "Settings Power Off Cancel default and explicit confirmation", "supervisor shutdown request"]
    if a.opendingux:
        report["tests"] += ["OpenDingux home opens app-owned menu", "PocketSNES native browser loads 7z through shared resolver",
                            "global OpenDingux selection", "per-system engine selection/save", "global change clears overrides"]
    if a.pcsx_menu:
        report["tests"] += ["PCSX4ALL app-owned menu opens through OpenDingux home UI; B exits to home"]
    if a.opk_qa:
        report["tests"] += ["native OPK compatibility catalog", "FAT SquashFS extraction/cache without loop or flock",
                            "OPK launch through home UI", "OPK software video and arrow/Start/chord input", "persistent per-app HOME journal"]
    if a.native_only:
        report["tests"]=["metadata-only RAM root", "file-backed LilSmokeUI", "first-boot FAT growth",
            "production addon payload", "native application through OpenDingux home UI",
            "application-owned menu", "native GBA ZIP launch/input/pause/exit" if a.gba else "native menu quit",
            "clean shutdown/filesystems", "source image unchanged"]
        report["gba_packed_and_plain_tested"]=False
        report["md_packed_and_plain_tested"]=False
    if a.applications:
        report['applications_full_ui_tested']=True
        report['tests']=['one MIPS CPU/128 MB','production UI and helper','Explorer opens .lilpck',
            'Cancel returns to Explorer without installation','explicit install','same-version refusal',
            'theme-independent app grid','bundled MIPS runtime launch','buttons and Start+Select quit',
            'new-version confirmation','changed launcher removed; user journal retained',
            'Y options and confirmed uninstall','clean FAT shutdown/checks','source image unchanged']
    (a.out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print("M8_RUNTIME_VM_OK")


if __name__ == "__main__": main()
