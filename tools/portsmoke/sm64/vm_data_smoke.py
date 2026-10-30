#!/usr/bin/env python3
"""Run the independent BYO app in one-CPU MIPS Linux on disposable data disks.

Tests .lilpck data preparation with a 32 MiB process cap, not UI/gameplay/BSP.
Never writes to an SD card. No ROM/assets are retained in the report folder.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time
import zipfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/hwtest"))
from mips_boot_smoke import Newc
from test_private_rom import compare
from generate_manifest import convert, SOURCE_SHA256

INIT = r'''#!/bin/sh
exec </dev/console >/dev/console 2>&1
/bin/busybox --install -s /bin
export PATH=/bin:/usr/bin
mkdir -p /proc /sys /dev /tmp /storage /run
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
echo "SM64_DATA_VM_ARCH=$(uname -m)"
awk '/MemTotal:/ { print "SM64_DATA_VM_MEM_KB=" $2 }' /proc/meminfo
FAIL=0
disk=/dev/sda
[ -b "$disk" ] || disk=/dev/hda
mount -t @FS@ -o rw "$disk" /storage || { echo 'FAIL: data disk mount'; poweroff -f; }
SM64_TEST_FAT=1 SM64_TEST_TMPDIR=/storage /usr/bin/test_asset_pack || FAIL=1
(ulimit -v 32768; /usr/bin/test_runtime_assets) || FAIL=1
export PORTSMOKE_SM64_DIAGNOSTICS=1
export LILSMOKE_APP_DIR='/storage/independent-apps/SM64 BYO Data - NOT PLAYABLE'
cd "$LILSMOKE_APP_DIR" || FAIL=1
for attempt in first cache; do
    (ulimit -v 32768; /bin/sh ./SM64.sh) > /tmp/launch.log 2>&1
    result=$?
    cat /tmp/launch.log
    if [ "$result" -eq 0 ] && grep -q '^SM64_DATA_PREPARED_GAMEPLAY_UNIMPLEMENTED$' /tmp/launch.log; then
        echo "PASS: development launcher $attempt reports successful data diagnostics and unimplemented gameplay";
    else echo "FAIL: launcher $attempt returned $result"; FAIL=1; fi
done
DATA="$LILSMOKE_APP_DIR/runtime/bin/portsmoke-sm64-data"
(ulimit -v 32768; "$DATA" --verify .sm64/sm64-us-v1.pdat) || FAIL=1
(ulimit -v 32768; "$DATA" --texture-checksums .sm64/sm64-us-v1.pdat) > /tmp/textures.log 2>&1
if [ "$?" -eq 0 ]; then tail -n 2 /tmp/textures.log; echo 'PASS: bounded typed texture diagnostics';
else echo 'FAIL: typed texture diagnostics'; FAIL=1; fi
if [ "$(sha1sum './Super Mario 64 (USA).z64' | cut -d ' ' -f 1)" = 9bef1128717f958171a4afac3ed78ee2bb4e86ce ]; then
    echo 'PASS: original ROM unchanged'
else echo 'FAIL: ROM changed'; FAIL=1; fi
sync
cd /
umount /storage || { echo 'FAIL: storage unmount'; FAIL=1; }
echo "SM64_DATA_VM_RESULT=$FAIL"
poweroff -f
'''


def run(command):
    result = subprocess.run(list(map(str, command)), text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(f"{command}: {result.stdout}{result.stderr}")
    return result.stdout


def make_initramfs(target, unit, textures, output, fs):
    archive = Newc()
    for path in ("bin", "usr", "usr/bin", "lib", "dev", "proc", "sys", "tmp", "run", "storage"):
        archive.dir(path)
    archive.chrdev("dev/console", 5, 1)
    archive.file("bin/busybox", (target / "bin/busybox").read_bytes(), 0o755)
    archive.symlink("bin/sh", "busybox")
    archive.file("lib/libc.so", (target / "lib/libc.so").read_bytes(), 0o755)
    archive.symlink("lib/ld-musl-mipsel.so.1", "libc.so")
    archive.file("usr/bin/test_asset_pack", unit.read_bytes(), 0o755)
    archive.file("usr/bin/test_runtime_assets", textures.read_bytes(), 0o755)
    archive.file("init", INIT.replace("@FS@", fs), 0o755)
    output.write_bytes(gzip.compress(archive.build(), compresslevel=1))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--build", type=Path, required=True)
    p.add_argument("--qemu", type=Path, required=True)
    p.add_argument("--rom", type=Path, required=True)
    p.add_argument("--assets-json", type=Path, required=True)
    p.add_argument("--unit", type=Path, required=True)
    p.add_argument("--textures", type=Path, required=True)
    p.add_argument("--lilpck", type=Path, required=True)
    p.add_argument("--cpu", default="74Kf-lilsmoke")
    p.add_argument("--out", type=Path, required=True)
    a = p.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    original = hashlib.sha256(a.rom.read_bytes()).hexdigest()
    manifest = a.assets_json.read_bytes()
    assert hashlib.sha256(manifest).hexdigest() == SOURCE_SHA256
    assets = convert(json.loads(manifest))
    results = []
    for fs in ("exfat", "vfat"):
        with tempfile.TemporaryDirectory(prefix="portsmoke-sm64-vm-private-") as tmp:
            work = Path(tmp)
            disk = work / "roms.img"
            with disk.open("wb") as stream:
                stream.truncate(64 * 1024 * 1024)
            if fs == "exfat": run(["mkfs.exfat", "-L", "LILROMS", disk])
            else: run(["mkfs.vfat", "-F", "32", "-n", "LILROMS", disk])
            mount = work / "mount"; mount.mkdir()
            run(["mount", "-o", "loop", disk, mount])
            try:
                game = mount / "independent-apps/SM64 BYO Data - NOT PLAYABLE"
                game.mkdir(parents=True)
                with zipfile.ZipFile(a.lilpck) as archive:
                    assert all(not Path(n).is_absolute() and ".." not in Path(n).parts for n in archive.namelist())
                    archive.extractall(game)
                shutil.copyfile(a.rom, game / "Super Mario 64 (USA).z64")
            finally:
                run(["umount", mount])
            initramfs = work / "init.cpio.gz"
            make_initramfs(a.build / "target", a.unit, a.textures, initramfs, fs)
            log_path = work / "serial.log"
            retained_log = a.out / f"mips-120mb-{fs}-data.log"
            command = [str(a.qemu), "-M", "malta", "-cpu", a.cpu, "-m", "120", "-smp", "1",
                       "-kernel", str(a.build / "images/vmlinux"), "-initrd", str(initramfs),
                       "-append", "console=ttyS0 rdinit=/init", "-drive", f"file={disk},format=raw,if=ide",
                       "-display", "none", "-serial", "stdio", "-monitor", "none", "-no-reboot"]
            with log_path.open("w") as log:
                process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + 120
                    while process.poll() is None and time.monotonic() < deadline:
                        text = log_path.read_text()
                        if "SM64_DATA_VM_RESULT=0" in text and ("System halted" in text or "Power down" in text):
                            break
                        time.sleep(0.2)
                    if process.poll() is None:
                        text = log_path.read_text()
                        if "SM64_DATA_VM_RESULT=0" not in text or not ("System halted" in text or "Power down" in text):
                            raise RuntimeError("MIPS data test timed out before clean shutdown")
                finally:
                    if process.poll() is None:
                        process.terminate()
                        try: process.wait(timeout=5)
                        except subprocess.TimeoutExpired: process.kill(); process.wait()
                    log.flush()
                    shutil.copyfile(log_path, retained_log)
            text = log_path.read_text()
            assert "SM64_DATA_VM_RESULT=0" in text and "FAIL:" not in text, text[-6000:]
            assert re.search(r"PASS \d+ SM64 data checks", text) and "PASS 140294 typed texture checks" in text
            assert "PASS: bounded typed texture diagnostics" in text
            assert "Extracted 1322 private US assets" in text and "Reused verified cache: 1322" in text
            memory = int(re.search(r"SM64_DATA_VM_MEM_KB=(\d+)", text)[1])
            peaks = list(map(int, re.findall(r"ExtractorPeakRSSKB=(\d+)", text)))
            assert 90000 < memory < 131072 and peaks and max(peaks) < 32768
            fsck = run(["fsck.exfat" if fs == "exfat" else "fsck.vfat", "-n", disk])
            (a.out / f"mips-120mb-{fs}-fsck.log").write_text(fsck)
            run(["mount", "-o", "loop,ro", disk, mount])
            try:
                comparison = compare(mount / "independent-apps/SM64 BYO Data - NOT PLAYABLE/.sm64/sm64-us-v1.pdat",
                                     a.rom.read_bytes(), assets)
            finally:
                run(["umount", mount])
            results.append(dict(filesystem=fs, status="PASS", guest_mem_total_kb=memory,
                                extractor_peak_rss_kb=max(peaks), virtual_memory_cap_kb=32768, **comparison))
    assert hashlib.sha256(a.rom.read_bytes()).hexdigest() == original
    report = dict(status="PASS", machine="QEMU Malta", cpu=a.cpu, ram_mb=120,
                  cpus=1, libc="musl", results=results, gameplay_tested=False,
                  assets_retained=False, physical_boot_verified=False,
                  note="No physical frequency/performance claim; successful data diagnostics exit 0 with explicit unimplemented-gameplay marker.")
    (a.out / "mips-data-vm-results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
