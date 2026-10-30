#!/usr/bin/env python3
"""Run only a compiled guard's read-only CLI, never its shutdown/prepare paths."""
import argparse
import subprocess
import tempfile
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("components", type=Path)
a = p.parse_args()
guard = a.components / "m8-safe-power"
for name in ("m8-safe-power", "libr36-display.so"):
    assert b"/dev/dis\0" not in (a.components / name).read_bytes(), name
qemu = ["qemu-mipsel", "-cpu", "74Kf", str(guard)]
result = subprocess.run(qemu + ["--self-check"], check=True, capture_output=True, text=True)
assert result.stdout.strip() == "M8_POWER_GUARD version=2"
cases = [("ro", "/dev/mmcblk0p1 /mnt/sdcard vfat ro,nosuid 0 0\n", 0),
         ("rw", "/dev/mmcblk0p1 /mnt/sdcard vfat rw,nosuid 0 0\n", 75),
         ("errors-ro", "/dev/mmcblk0p1 /mnt/sdcard vfat rw,errors=remount-ro 0 0\n", 75),
         ("readonly-errors", "/dev/mmcblk0p1 /lib vfat ro,errors=remount-ro 0 0\n", 0),
         ("alias", "/dev/mmcblk0p2 /tmp/root/storage vfat rw 0 0\n", 75),
         ("space", "/dev/mmcblk0p2 /media/My\\040Card vfat ro 0 0\n", 0),
         ("exfat", "/dev/mmcblk0p2 /storage exfat rw 0 0\n", 75),
         ("other", "tmpfs /tmp tmpfs rw 0 0\n", 0)]
with tempfile.TemporaryDirectory(prefix="r36-readonly-guard-") as name:
    for title, contents, expected in cases:
        file = Path(name) / title
        file.write_text(contents)
        result = subprocess.run(qemu + ["--check-table", str(file)], capture_output=True, text=True)
        assert result.returncode == expected, (title, result.stdout, result.stderr)
print("PASS MIPS R36 power guard identity, 8 read-only mount parser cases, no HDMI device strings; no device opened")
