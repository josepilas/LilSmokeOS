#!/usr/bin/env python3
"""Qualify delivered FAT files/logging in a disposable MIPS guest, not HC16xx boot."""
# SPDX-License-Identifier: MPL-2.0
import argparse
import gzip
import json
from pathlib import Path
import subprocess
import sys
import time
from boot_report import decode_journal
from build_candidate import sha

P = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--stock-loader", type=Path, required=True)
    parser.add_argument("--stock-root", type=Path, required=True)
    parser.add_argument("--sdk", type=Path, default=Path("/root/lilsmoke-os/build/buildroot/output"))
    args = parser.parse_args()
    if args.image.is_symlink() or not args.image.is_file() or args.image.suffix != ".img":
        parser.error("Only a built regular file image is accepted, never a physical disk")
    args.out.mkdir(parents=True, exist_ok=False)
    original_sha = sha(args.image)
    sys.path.insert(0, str(P / "tools/hwtest"))
    sys.path.insert(0, str(P / "tools/portsmoke/sm64"))
    from mips_boot_smoke import Newc
    from mips_disk_fixture import overlay_initramfs
    overlay = Newc()
    overlay.file("usr/bin/r36-stock-loader", args.stock_loader.read_bytes(), 0o755)
    for name in ("ld.so.1", "libc.so.6", "libdl.so.2"):
        overlay.file("lib/" + name, (args.stock_root / "lib" / name).read_bytes(), 0o755)
    overlay.file("usr/bin/bench-init", '''#!/bin/sh
exec </dev/console >/dev/console 2>&1
fail() { echo "R36_FILE_VM_FAILED:$*"; sync; poweroff -f; while :; do sleep 5; done; }
mkdir -p /bench /mnt/sdcard /mnt/roms
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || fail 'evidence mount'
cat /proc/meminfo > /bench/memory-before.txt
mount -t vfat -o rw,nosuid,nodev /dev/sdb1 /mnt/sdcard || fail 'LILOS'
mount -t vfat -o ro,nosuid,nodev /dev/sdb2 /mnt/roms || fail 'LILROMS'
/mnt/sdcard/lilsmoke/r36-bootlog VM_FILE_SYSTEM_MOUNTED || fail 'first FAT checkpoint'
swapon /mnt/sdcard/cubegm/pagefile.sys || fail 'fresh swap header/allocation'
cat /proc/swaps > /bench/swaps.txt
echo R36_VM_SWAP_ACTIVE
/bin/sh /mnt/sdcard/cubegm/icube.sh
result=$?
[ "$result" = 70 ] || fail "expected absent-panel diagnostic, got $result"
[ ! -d /tmp/lilsmoke-m8-launch.lock ] || fail 'lock cleanup'
/usr/bin/r36-stock-loader /mnt/sdcard/cubegm/cores/libemu_md.so
result=$?
[ "$result" = 70 ] || fail "expected production plugin shell handoff, got $result"
[ ! -d /tmp/lilsmoke-m8-launch.lock ] || fail 'plugin path lock cleanup'
cp /mnt/sdcard/lilsmoke/boot-journal.bin /bench/returned-journal.bin || fail 'journal readback'
cp /mnt/sdcard/lilsmoke/bringup.log /bench/bringup.log || fail 'startup log readback'
cat /proc/meminfo > /bench/memory-after.txt
swapoff /mnt/sdcard/cubegm/pagefile.sys || fail 'swapoff'
umount /mnt/roms || fail 'LILROMS close'
umount /mnt/sdcard || fail 'LILOS close'
sync
echo R36_FILE_VM_PASS
poweroff -f
''', 0o755)
    root = args.out / "sdk.squashfs"
    subprocess.run([str(args.sdk / "host/bin/mksquashfs"), str(args.sdk / "target"), str(root),
                    "-noappend", "-comp", "gzip", "-processors", "1", "-no-xattrs"],
                   check=True, stdout=subprocess.DEVNULL)
    initrd = args.out / "boot.cpio.gz"
    initrd.write_bytes(gzip.compress(overlay_initramfs(args.sdk, overlay, "/usr/bin/bench-init",
        overlay_mib=8, root_device="/dev/sda", root_fstype="squashfs")))
    command = ["/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel", "-M", "malta",
        "-cpu", "74Kf-lilsmoke", "-smp", "1", "-m", "48", "-kernel", str(args.sdk / "images/vmlinux"),
        "-initrd", str(initrd), "-append", "console=ttyS0 panic=-1", "-display", "none",
        "-serial", "stdio", "-monitor", "none", "-no-reboot", "-nic", "none",
        "-drive", f"file={root},if=ide,index=0,format=raw,snapshot=on",
        "-drive", f"file={args.image.resolve()},if=ide,index=1,format=raw,snapshot=on",
        "-fsdev", f"local,id=bench,path={args.out.resolve()},security_model=none",
        "-device", "virtio-9p-pci,fsdev=bench,mount_tag=bench"]
    (args.out / "command.json").write_text(json.dumps(command, indent=2) + "\n")
    report = dict(passed=False, physical_hc16xx=False, kernel_from_image_booted=False,
                  scope="FAT mounts/swap/production shell early failure/journal, injected Malta kernel",
                  ram_mib=48, cpus=1, image_sha256=original_sha,
                  image_writes="discarded QEMU snapshot only", physical_sd_access=False)
    vm = None
    try:
        serial = args.out / "serial.log"
        with serial.open("wb") as log:
            vm = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 180
            while True:
                text = serial.read_text(errors="replace")
                if "R36_FILE_VM_PASS" in text:
                    break
                if "R36_FILE_VM_FAILED:" in text or vm.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError(text[-5000:])
                time.sleep(.3)
        journal = decode_journal((args.out / "returned-journal.bin").read_bytes())
        events = [row["event"] for row in journal["records"]]
        assert events == ["VM_FILE_SYSTEM_MOUNTED", "SHELL_HOOK:/mnt/sdcard/cubegm/icube.sh", "SHELL_ENTRY",
                          "RAM_SHELL", "LAUNCH_LOCKED", "LAUNCH_FAILED:not an enabled handheld panel",
                          "PLUGIN_DLOPEN", "PLUGIN_INIT", "PLUGIN_LOAD_GAME", "PLUGIN_EXEC_SHELL", "SHELL_ENTRY",
                          "RAM_SHELL", "LAUNCH_LOCKED", "LAUNCH_FAILED:not an enabled handheld panel"], journal
        assert not journal["invalid_slots"], journal
        assert "R36_VM_SWAP_ACTIVE" in text
        assert "pagefile.sys" in (args.out / "swaps.txt").read_text()
        assert not any(word in text for word in ("Out of memory", "FAT-fs (sdb1): error", "I/O error"))
        report.update(passed=True, journal=journal, swap_file_verified=True, volumes_closed=True,
                      exact_stock_glibc_production_plugin=True, exec_replacement_from_glibc_to_musl_shell=True)
    finally:
        if vm and vm.poll() is None:
            vm.terminate()
            try:
                vm.wait(timeout=10)
            except subprocess.TimeoutExpired:
                vm.kill(); vm.wait()
        unchanged = sha(args.image) == original_sha
        report["image_unchanged"] = unchanged
        report["passed"] = report["passed"] and unchanged
        root.unlink(missing_ok=True)
        (args.out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    if not report["passed"]:
        raise RuntimeError("File-image qualification failed; do not deliver this image")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
