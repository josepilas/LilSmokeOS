#!/usr/bin/env python3
"""Format sparse regular-file fixtures, never a disk, card or final image."""
import struct
import subprocess
import tempfile
from pathlib import Path
from build_candidate import prepare_growable_fat

with tempfile.TemporaryDirectory(prefix="r36-format-fixtures-") as name:
    root = Path(name)
    osfat = root / "os.fat"
    with osfat.open("xb") as file:
        file.truncate(1536 * 1024 * 1024)
    subprocess.run(["mkfs.vfat", "-F", "32", "-S", "512", "-R", "64", "-g", "255/32",
                    "-h", "2048", "-n", "LILOS", str(osfat)], check=True, capture_output=True)
    with osfat.open("rb") as source:
        boot = source.read(512)
    assert struct.unpack_from("<I", boot, 32)[0] == 1536 * 2048
    assert struct.unpack_from("<H", boot, 24)[0] == 32
    subprocess.run(["fsck.vfat", "-n", str(osfat)], check=True, capture_output=True)
    romfat = root / "rom.fat"
    prepare_growable_fat(romfat, 1024, 3147776, 131072)
    with romfat.open("rb") as source:
        boot = source.read(512)
    total = struct.unpack_from("<I", boot, 32)[0]
    reserved = struct.unpack_from("<H", boot, 14)[0]
    fat = struct.unpack_from("<I", boot, 36)[0]
    clusters = (total - reserved - boot[16] * fat) // boot[13]
    assert total == 1024 * 2048 and boot[13] == 16 and clusters >= 65525
    subprocess.run(["fsck.vfat", "-n", str(romfat)], check=True, capture_output=True)
    print(f"PASS actual sparse formatter: exact LILOS BPB/MBR count, LILROMS FAT32 {clusters} data clusters; fsck both clean")
