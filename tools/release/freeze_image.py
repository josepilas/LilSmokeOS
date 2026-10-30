#!/usr/bin/env python3
"""Freeze a NEW file-only M8 release snapshot; never upload or access devices.

Run under WSL Ubuntu-24.04 as root. Integrity checks are not legal clearance.
Only generated build/release-upload output and scratch files are written.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/m8"))
from build_stock_candidate import BOOT_FILES, assemble, prepare_growable_fat, sha
from check_stock_directory import check_directory
from stock_format import BOOT_BYTES, SECTOR, inspect, uimage

MIB = 1024 * 1024
VERSION = "Pre-Alpha-0.0.1"
DESTINATION = ROOT / "build/release-upload" / VERSION
IMAGE_NAME = f"LilSmokeOS-{VERSION}.img"
MIPS_RUNNER = ("qemu-mipsel", "-cpu", "74Kf")
PROTOTYPE = {
    "usr/share/portsmoke/engines/sm64-byo-dev.peng",
    "usr/share/portsmoke/ports/sm64/README.txt",
    "usr/share/portsmoke/ports/sm64/SM64.sh",
    "usr/share/portsmoke/ports/sm64/portsmoke.conf",
    "usr/share/portsmoke/ports/sm64",
}
DOCUMENTS = (
    "LICENSE", "LICENSE_POLICY.md", "THIRD_PARTY_NOTICES.md", "CREDITS.md",
    "LICENSES/LilSmokeOS-PreAlpha-Personal.txt",
    "LICENSES/LilSmokeOS-MIT-legacy.txt", "LICENSES/MPL-2.0.txt",
    "docs/pre-alpha-0.0.1/LICENSES.md",
)
GAME_SUFFIXES = {
    ".z64", ".v64", ".n64", ".sfc", ".smc", ".gba", ".gb", ".gbc",
    ".nes", ".nds", ".iso", ".chd", ".cue", ".pdat", ".sav", ".srm",
    ".state", ".mpq", ".wad", ".pbp", ".3ds",
}
ROM_MAGICS = (b"\x80\x37\x12\x40", b"\x37\x80\x40\x12", b"\x40\x12\x37\x80", b"NES\x1a")
GAPS = [
    "HC16xx stock Linux 4.4.186-release BSP/kernel/initramfs, AVP firmware, "
    "DTB/boot scripts, driver.so, rkgame/icube.stock and vendor libraries: exact "
    "corresponding source/build configuration and redistribution permissions "
    "are not supplied or established by this snapshot.",
    "BusyBox, RetroArch, PCSX4ALL, PCSX ReARMed/Lightrec/GNU lightning, "
    "OpenDingux engines and libretro cores: retained available notices do not "
    "constitute a complete exact-version corresponding-source/build bundle. "
    "Snes9x/PocketSNES, Genesis Plus GX and FBNeo/MAME-derived terms still need "
    "component-specific reconciliation.",
    "DirectFB, FFmpeg and other LGPL dependencies: exact linkage, relinking "
    "materials and complete matching source/configuration are not established "
    "by byte-preservation checks.",
    "Vendor UI .cpd/resource/root.dat, fonts, logo, sounds/music and first-party "
    "theme/icon/font assets: provenance/permissions are not established by "
    "this content audit; stock opaque resources are retained for recovery.",
    "The only file-backed runtime license directory present before freezing "
    "was usr/share/licenses/lilsmoke-ffmpeg (COPYING.LGPLv2.1). The embedded "
    "PCSX4ALL package and LILROMS PCSX ReARmed license directory retain their "
    "own notices; the summary documents are not a substitute for missing "
    "per-component exact-version notices.",
    "Device Adapter Python/Tcl/Tk/OpenSSL/zlib-ng/PyInstaller notices are "
    "retained byte-for-byte, including its historical first-party LICENSE; "
    "the new top-level personal-use policy does not revoke earlier grants.",
    "Private full SM64 binary/assets are not included. The removed old TinyGL "
    "prototype is distinct from the private full SM64 source-only graphics "
    "restriction; that restriction is not assigned to the old prototype.",
]


def run(command, acceptable=(0,)):
    result = subprocess.run(list(map(str, command)), capture_output=True,
                            text=True, errors="replace", timeout=600)
    if result.returncode not in acceptable:
        raise RuntimeError(f"command failed ({result.returncode}): {command}\n"
                           + result.stdout + result.stderr)
    return result.stdout + result.stderr


def plain(path, directory=False):
    path = Path(path)
    for parent in (path, *path.parents):
        if parent.is_symlink():
            raise ValueError(f"symlink path refused: {path}")
    metadata = path.lstat()
    expected = stat.S_ISDIR if directory else stat.S_ISREG
    if not expected(metadata.st_mode):
        raise ValueError(f"not a regular {'directory' if directory else 'file'}: {path}")
    return metadata


def signature(path):
    info = plain(path)
    return info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns


def small_text(path, limit=MIB):
    if plain(path).st_size > limit:
        raise ValueError(f"metadata exceeds budget: {path}")
    return Path(path).read_text(encoding="utf-8")


def relative_name(name):
    value = PurePosixPath(name)
    if not name or value.is_absolute() or any(p in ("", ".", "..") for p in name.split("/")) \
            or "\\" in name or any(ord(c) < 32 or ord(c) > 126 for c in name):
        raise ValueError(f"unsafe relative name: {name!r}")
    return name


def parse_manifest(text):
    if len(text) > MIB:
        raise ValueError("manifest exceeds budget")
    rows, names = [], set()
    for line in text.splitlines():
        row = line.split("\t")
        if len(row) != 4 or row[0] not in ("D", "F", "L", "C") or not re.fullmatch(r"[0-7]{4}", row[1]):
            raise ValueError("invalid manifest record")
        relative_name(row[2])
        if row[2].casefold() in names:
            raise ValueError("duplicate manifest path")
        names.add(row[2].casefold())
        if row[0] in ("F", "C") and not re.fullmatch(r"\d{6}\.bin", row[3]):
            raise ValueError("invalid object name")
        if row[0] == "D" and row[3]:
            raise ValueError("directory target must be empty")
        if row[0] == "L" and (not row[3] or any(ord(c) < 32 or ord(c) > 126 for c in row[3])):
            raise ValueError("invalid symlink target")
        rows.append(row)
    if len(rows) > 8192:
        raise ValueError("too many runtime records")
    return rows


def remove_prototype(rows, payload):
    removed = [r for r in rows if r[2] in PROTOTYPE]
    if {r[2] for r in removed} != PROTOTYPE:
        raise ValueError("prototype removal must match all five exact records")
    survivors = [r for r in rows if r[2] not in PROTOTYPE]
    objects = {r[3] for r in survivors if r[0] in ("F", "C")}
    if any(r[3] in objects for r in removed if r[0] in ("F", "C")):
        raise ValueError("prototype object is shared with a retained runtime file")
    for row in removed:
        if row[0] in ("F", "C"):
            (payload / "files" / row[3]).unlink()
    if any("sm64" in r[2].lower() for r in survivors):
        raise ValueError("unexpected SM64 runtime object remains")
    return survivors, removed


def inventory(tree):
    rows = {}
    for path in sorted(Path(tree).rglob("*")):
        name = relative_name(path.relative_to(tree).as_posix())
        metadata = plain(path, directory=path.is_dir())
        rows[name] = dict(kind="D" if path.is_dir() else "F", bytes=metadata.st_size if path.is_file() else 0)
        if path.is_file():
            rows[name]["sha256"] = sha(path)
    if len(rows) > 12000:
        raise ValueError("partition file-count budget exceeded")
    return rows


def export_volume(image, offset, directory):
    directory.mkdir()
    volume = f"{image}@@{offset}"
    listed = run(["mdir", "-b", "-s", "-i", volume, "::/"]).splitlines()
    if len(listed) > 12000:
        raise ValueError("partition directory budget exceeded")
    for line in listed:
        if not line.startswith("::/"):
            raise ValueError("unexpected FAT directory listing")
        relative_name(line[3:].rstrip("/"))
    run(["mcopy", "-s", "-n", "-i", volume, "::/*", directory])
    actual = inventory(directory)
    if {v[3:].rstrip("/") for v in listed} != set(actual):
        raise ValueError("extracted FAT inventory differs from mdir")
    return actual


def reject_private(name, header):
    lower = name.lower()
    parts = PurePosixPath(lower).parts
    if PurePosixPath(lower).suffix in GAME_SUFFIXES or header.startswith(ROM_MAGICS):
        raise ValueError(f"ROM/save/game asset found: {name}")
    if any(p in (".ssh", ".portsmoke", "private-local-apps", "private-cache", "baserom") for p in parts) \
            or re.search(r"(?:^|/)(?:ssh_host_|dropbear_.*_host_key|id_rsa|id_ed25519)", lower) \
            or "sm64-original" in lower or "sm64.us.f3dex2e" in lower:
        raise ValueError(f"private content found: {name}")
    if any(p in ("roms", "bios", "saves", "states", "root", "home", "persist", "logs") for p in parts):
        raise ValueError(f"nonempty private-data directory: {name}")


def audit_zip(path, label):
    rows = []
    with zipfile.ZipFile(path) as archive:
        entries = archive.infolist()
        if len(entries) > 4096 or sum(e.file_size for e in entries) > 256 * MIB:
            raise ValueError("package audit budget exceeded")
        seen = set()
        for entry in entries:
            name = relative_name(entry.filename.rstrip("/"))
            if name.casefold() in seen or stat.S_ISLNK(entry.external_attr >> 16):
                raise ValueError("unsafe package member")
            seen.add(name.casefold())
            if entry.is_dir():
                continue
            with archive.open(entry) as stream:
                header = stream.read(512)
                reject_private(name, header)
                if header.startswith((b"PK\x03\x04", b"7z\xbc\xaf\x27\x1c", b"Rar!", b"\x1f\x8b", b"\xfd7zXZ\x00")):
                    raise ValueError("nested compressed payload requires a separate audit")
                digest = hashlib.sha256(header)
                for block in iter(lambda: stream.read(MIB), b""):
                    digest.update(block)
            if name in ("meta.json", "runtime/build-info.json"):
                if entry.file_size > 65536:
                    raise ValueError("package metadata too large")
                data = json.loads(archive.read(entry))
                if data.get("private_user_build") or data.get("contains_user_rom_assets"):
                    raise ValueError("private package metadata")
            rows.append(dict(path=name, bytes=entry.file_size, sha256=digest.hexdigest()))
    return dict(package=label, members=rows)


def check_zero_swap(path):
    with path.open("rb") as stream:
        header = stream.read(4096)
        if header[-10:] != b"SWAPSPACE2":
            raise ValueError("unexpected initial swap header")
        for block in iter(lambda: stream.read(MIB), b""):
            if block.strip(b"\0"):
                raise ValueError("initial swap has nonzero payload/private residue")


def audit_contents(os_tree, rom_tree, rows, work):
    checks = {}
    objects = {r[3] for r in rows if r[0] in ("F", "C")}
    if {p.name for p in (os_tree / "lilsmoke/files").iterdir()} != objects:
        raise ValueError("unreferenced file-backed objects remain")
    logical = {r[2]: os_tree / "lilsmoke/files" / r[3] for r in rows if r[0] in ("F", "C")}
    packages = []
    for name, path in logical.items():
        with path.open("rb") as stream:
            header = stream.read(512)
        reject_private(name, header)
        if header.startswith(b"PK\x03\x04"):
            packages.append(audit_zip(path, name))
        elif name.endswith((".peng", ".lilpck", ".opk", ".zip")):
            raise ValueError(f"unreviewed runtime package: {name}")
    shadow = small_text(logical["etc/shadow"])
    for line in shadow.splitlines():
        fields = line.split(":")
        if len(fields) != 9 or fields[1] not in ("", "*", "!", "!!"):
            raise ValueError("password hash or unexpected account metadata present")
    passwd = small_text(logical["etc/passwd"])
    if any(len(line.split(":")) != 7 or line.split(":")[1] not in ("x", "*", "!") for line in passwd.splitlines()):
        raise ValueError("unexpected passwd credential data")
    wpa = small_text(logical["etc/wpa_supplicant.conf"])
    if re.search(r"(?im)^\s*(?:ssid|psk|password|identity|private_key)\s*=", wpa):
        raise ValueError("wireless credentials present")
    checks["account_defaults"] = "No password hashes; root password field remains blank, other accounts locked. Unchanged upstream build default, not a hardened release."
    checks["network_defaults"] = "No SSID/PSK/password/identity; credential-free key_mgmt=NONE template retained."
    for tree, tag in ((os_tree, "LILOS"), (rom_tree, "LILROMS")):
        for path in tree.rglob("*"):
            if not path.is_file():
                continue
            name = path.relative_to(tree).as_posix()
            if tag == "LILOS" and name.startswith("lilsmoke/files/"):
                continue
            if name == "lilsmoke/persist.ext4":
                continue
            with path.open("rb") as stream:
                header = stream.read(512)
            reject_private(name, header)
            if tag == "LILROMS" and not name.startswith("emulators/"):
                raise ValueError("unexpected non-emulator file in LILROMS")
            if name.lower().endswith((".zip", ".peng", ".opk", ".lilpck")):
                packages.append(audit_zip(path, tag + "/" + name))
            if name.lower().endswith((".7z", ".rar", ".pdat")):
                raise ValueError("unreviewed private/container file")
            if path.stat().st_size < MIB and (path.suffix.lower() in (".conf", ".cfg", ".json", ".xml", ".py", ".sh", ".txt") or name.endswith(".bin")):
                data = path.read_bytes()
                if re.search(rb"-----BEGIN (?:OPENSSH |RSA |EC |DSA )?PRIVATE KEY-----", data):
                    raise ValueError("private key content present")
    # e2fsck and debugfs inspect the extracted regular file without mounting it.
    persist = os_tree / "lilsmoke/persist.ext4"
    checks["persist_fsck"] = run(["e2fsck", "-f", "-n", persist])
    checks["persist_root"] = run(["debugfs", "-R", "ls -p /", persist])
    checks["persist_lost_found"] = run(["debugfs", "-R", "ls -p /lost+found", persist])
    for key, expected in (("persist_root", {".", "..", "lost+found"}), ("persist_lost_found", {".", ".."})):
        names = {line.split("/")[5] for line in checks[key].splitlines() if line.startswith("/") and len(line.split("/")) > 5 and line.split("/")[5]}
        if names != expected:
            raise ValueError("initial persistence is not blank")
    summary = run(["dumpe2fs", "-h", persist])
    total = int(re.search(r"Inode count:\s*(\d+)", summary)[1])
    free = int(re.search(r"Free inodes:\s*(\d+)", summary)[1])
    if total - free != 11:
        raise ValueError("persistence has unexpected allocated inodes")
    checks["persist_used_inodes"] = total - free
    checks["persist_sha256"] = sha(persist)
    check_zero_swap(os_tree / "cubegm/pagefile.sys")
    checks["swap_payload_all_zero"] = True
    checks["packages"] = packages
    checks["scope"] = "All FAT directory entries and file-backed runtime paths, credential files, package members, ext4 inodes, and swap payload checked. Opaque stock firmware/resources and packaged desktop executable are byte-preserved, not reverse-engineered; no legal clearance or arbitrary embedded-data absence assertion."
    return checks


def add_documents(tree, rows):
    payload = tree / "lilsmoke"
    new_rows = list(rows)
    names = {r[2] for r in rows}
    number = max(int(p.stem) for p in (payload / "files").iterdir()) + 1
    document_hashes = {}
    for name in DOCUMENTS:
        source = ROOT / name
        content = source.read_bytes()
        target = tree / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content)
        runtime = "usr/share/licenses/lilsmoke-release/" + name
        for parent in reversed(PurePosixPath(runtime).parents):
            directory = str(parent)
            if directory != "." and directory not in names:
                new_rows.append(["D", "0755", directory, ""])
                names.add(directory)
        if runtime in names:
            raise ValueError("release license runtime path already exists")
        object_name = f"{number:06d}.bin"
        number += 1
        (payload / "files" / object_name).write_bytes(content)
        new_rows.append(["F", "0644", runtime, object_name])
        names.add(runtime)
        document_hashes[name] = hashlib.sha256(content).hexdigest()
    return new_rows, document_hashes


def write_manifest(payload, rows):
    rows = sorted((r for r in rows if r[0] == "D"), key=lambda r: (r[2].count("/"), r[2])) + [r for r in rows if r[0] != "D"]
    text = "".join("\t".join(r) + "\n" for r in rows)
    parse_manifest(text)
    (payload / "runtime.tsv").write_text(text, encoding="ascii")


def new_prefix(source, os_mib, rom_mib):
    prefix = bytearray(source["prefix"])
    start = 2048 + os_mib * 2048
    prefix[446:510] = bytes(64)
    for slot, (offset, count, active) in enumerate(((2048, os_mib * 2048, 128), (start, rom_mib * 2048, 0))):
        prefix[446 + slot * 16:462 + slot * 16] = struct.pack("<B3sB3sII", active, b"\xfe\xff\xff", 0x0c, b"\xfe\xff\xff", offset, count)
    return prefix


def synchronize_fsinfo(path):
    with path.open("r+b") as stream:
        boot = stream.read(512)
        info = struct.unpack_from("<H", boot, 48)[0]
        backup = struct.unpack_from("<H", boot, 50)[0]
        stream.seek(info * 512)
        data = stream.read(512)
        stream.seek((backup + info) * 512)
        stream.write(data)


def normalize_os_fat(path, sectors):
    """Undo mkfs.fat's track rounding within its already allocated FAT capacity."""
    path = Path(path).absolute()
    if not path.resolve().is_relative_to(DESTINATION.resolve()) or path.name != "lilos.fat":
        raise ValueError("OS BPB normalization is limited to generated lilos.fat")
    if plain(path).st_size != sectors * 512:
        raise ValueError("OS regular-file length differs from requested geometry")
    with path.open("r+b") as stream:
        boot = bytearray(stream.read(512))
        total, fat = struct.unpack_from("<II", boot, 32)
        reserved = struct.unpack_from("<H", boot, 14)[0]
        info, backup = struct.unpack_from("<HH", boot, 48)
        spc = boot[13]
        overhead = reserved + boot[16] * fat
        if boot[510:] != b"\x55\xaa" or boot[82:90] != b"FAT32   " or boot[71:82] != b"LILOS      " \
                or struct.unpack_from("<H", boot, 11)[0] != 512 or boot[16] != 2 or spc not in (1, 2, 4, 8, 16, 32, 64, 128) \
                or not 0 <= sectors - total <= 128 or (sectors - overhead) // spc + 2 > fat * 128 \
                or info + backup >= reserved:
            raise ValueError("new OS FAT cannot safely cover exact partition length")
        if total == sectors:
            return
        added = (sectors - overhead) // spc - (total - overhead) // spc
        stream.seek(info * 512)
        fsinfo = bytearray(stream.read(512))
        if struct.unpack_from("<I", fsinfo, 0)[0] != 0x41615252 or struct.unpack_from("<I", fsinfo, 484)[0] != 0x61417272:
            raise ValueError("invalid new OS FSInfo")
        free = struct.unpack_from("<I", fsinfo, 488)[0]
        if free != 0xffffffff:
            struct.pack_into("<I", fsinfo, 488, free + added)
        struct.pack_into("<I", boot, 32, sectors)
        for sector, content in ((0, boot), (backup, boot), (info, fsinfo), (backup + info, fsinfo)):
            stream.seek(sector * 512)
            stream.write(content)
        stream.flush()
        os.fsync(stream.fileno())


def fat_geometry(path, start, sectors, label):
    with path.open("rb") as stream:
        boot = stream.read(512)
        u16 = lambda off: struct.unpack_from("<H", boot, off)[0]
        u32 = lambda off: struct.unpack_from("<I", boot, off)[0]
        if boot[510:] != b"\x55\xaa" or boot[82:90] != b"FAT32   " or boot[71:82].rstrip().decode() != label or u16(11) != 512 \
                or u32(28) != start or u32(32) != sectors or boot[16] != 2:
            raise ValueError("FAT32/BPB/hidden-sector contract failed")
        overhead = u16(14) + boot[16] * u32(36)
        clusters = (sectors - overhead) // boot[13]
        if clusters < 65525 or clusters + 2 > u32(36) * 128:
            raise ValueError("invalid FAT32 cluster capacity")
        stream.seek(u16(50) * 512)
        if stream.read(512) != boot:
            raise ValueError("backup boot sector mismatch")
        return dict(fat_sectors=u32(36), cluster_sectors=boot[13], seed_clusters=clusters,
                    capacity_clusters=u32(36) * 128 - 2)


def compare_inventory(expected, actual):
    if expected != actual:
        differing = sorted(set(expected) ^ set(actual) | {k for k in expected.keys() & actual.keys() if expected[k] != actual[k]})
        raise ValueError("partition readback differs: " + repr(differing[:20]))


def sparse_file(path, size=0):
    """Explicitly mark generated NTFS files sparse before large truncations."""
    path = Path(path).absolute()
    if not path.resolve().is_relative_to(DESTINATION.resolve()):
        raise ValueError("sparse target must be inside generated release output")
    with path.open("xb"):
        pass
    plain(path)
    if str(path).startswith("/mnt/c/"):
        native = "C:\\" + str(path)[7:].replace("/", "\\")
        run(["/mnt/c/Windows/System32/fsutil.exe", "sparse", "setflag", native])
        run(["/mnt/c/Windows/System32/fsutil.exe", "sparse", "queryflag", native])
    with path.open("r+b") as stream:
        stream.truncate(size)


def growth_probe(prefix, p1, p2, work, mips_binary=None):
    binary = work / "m8-grow-host"
    run(["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", ROOT / "board/segam-m8-v6-stock/m8-grow.c", "-o", binary])
    probe = work / "growth-probe.img"
    # Growth only reads p1's BPB; a sparse file avoids duplicating its data.
    sparse_file(probe)
    with probe.open("r+b") as target:
        target.write(prefix)
        with p1.open("rb") as source:
            target.write(source.read(512))
        target.seek(2048 * 512 + p1.stat().st_size)
        with p2.open("rb") as source:
            shutil.copyfileobj(source, target, MIB)
        target.truncate(4 * 1024 * MIB)
    start = 2048 + p1.stat().st_size // 512
    before = inventory(work / "roms-tree")
    runner = [binary] if mips_binary is None else [*MIPS_RUNNER, mips_binary]
    log = run([*runner, "--test-image", probe])
    log += run([*runner, "--test-image", probe])
    if mips_binary is not None:
        log += run([binary, "--test-image", probe])
    grown = inspect(probe)
    if grown["partitions"][1]["sectors"] != 4 * 1024 * 2048 - start:
        raise ValueError("growth probe did not reach file capacity")
    actual = export_volume(probe, start * 512, work / "growth-readback")
    compare_inventory(before, actual)
    with probe.open("rb") as stream:
        mbr_before = stream.read(512)
    with probe.open("r+b") as stream:
        stream.truncate(256 * 1024 * MIB)
    refused = run([*runner, "--test-image", probe], acceptable=(2,))
    with probe.open("rb") as stream:
        if stream.read(512) != mbr_before:
            raise ValueError("growth refusal modified MBR")
    probe.unlink()
    return dict(regular_file_only=True, shifted_start=start, grow_to_4gib_passed=True,
                retry_passed=True, files_preserved=True, oversize_refused_before_writes=True,
                shipped_mips_binary_tested=mips_binary is not None,
                shipped_mips_binary_sha256=sha(mips_binary) if mips_binary is not None else None,
                log=log, refusal=refused,
                limitation="Regular-file host/QEMU tests do not establish hardware boot or stock kernel behavior.")


def verify_frozen_mips(image):
    image = Path(image).absolute()
    if image != DESTINATION / IMAGE_NAME or plain(image).st_nlink != 1:
        raise ValueError("MIPS verification only accepts the independent frozen release image")
    report_path = image.with_suffix(".img.json")
    report = json.loads(small_text(report_path, 4 * MIB))
    expected = report["image_sha256"]
    before = signature(image)
    if sha(image) != expected:
        raise ValueError("frozen image SHA-256 mismatch")
    layout = inspect(image)
    os_part, rom_part = layout["partitions"]
    with tempfile.TemporaryDirectory(prefix=".mips-verify-", dir=DESTINATION) as temporary:
        work = Path(temporary)
        p1, p2 = work / "lilos.fat", work / "lilroms.fat"
        sparse_file(p1, os_part["sectors"] * 512)
        sparse_file(p2)
        with image.open("rb") as source, p1.open("r+b") as target:
            source.seek(os_part["offset_bytes"])
            target.write(source.read(512))
        with image.open("rb") as source, p2.open("r+b") as target:
            source.seek(rom_part["offset_bytes"])
            left = rom_part["sectors"] * 512
            while left:
                block = source.read(min(left, MIB))
                if not block:
                    raise ValueError("truncated frozen ROM partition")
                target.write(block)
                left -= len(block)
        export_volume(image, rom_part["offset_bytes"], work / "roms-tree")
        executable = work / "m8-grow-mips"
        run(["mcopy", "-n", "-i", f"{image}@@{os_part['offset_bytes']}", "::/lilsmoke/m8-grow", executable])
        if sha(executable) != report["inventories"]["LILOS"]["lilsmoke/m8-grow"]["sha256"]:
            raise ValueError("frozen MIPS growth executable differs from readback inventory")
        result = growth_probe(layout["prefix"], p1, p2, work, executable)
    if signature(image) != before or sha(image) != expected:
        raise ValueError("frozen image changed during regular-file MIPS verification")
    report["shipped_mips_growth_probe"] = result
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2), flush=True)
    return result


def freeze(candidate, stock_image, stock_directory):
    candidate, stock_image, stock_directory = map(lambda p: Path(p).absolute(), (candidate, stock_image, stock_directory))
    for path in (candidate, stock_image):
        plain(path)
    plain(stock_directory, directory=True)
    output = DESTINATION / IMAGE_NAME
    if output.exists() or output.is_symlink() or output.with_suffix(".img.partial").exists():
        raise ValueError("release snapshot/partial already exists; will not overwrite")
    DESTINATION.mkdir(parents=True, exist_ok=True)
    plain(DESTINATION, directory=True)
    if shutil.disk_usage(DESTINATION).free < 6 * 1024 * MIB:
        raise ValueError("at least 6 GiB free required for bounded scratch/readback")
    reference = json.loads(small_text(candidate.with_suffix(".img.json")))
    before_candidate, before_stock = signature(candidate), signature(stock_image)
    candidate_sha, stock_sha = sha(candidate), sha(stock_image)
    if candidate_sha != reference["image_sha256"] or stock_sha != reference["source_img_sha256"]:
        raise ValueError("protected input SHA-256 does not match candidate provenance")
    print("ProtectedCandidateSHA256=" + candidate_sha, flush=True)
    print("ProtectedStockSHA256=" + stock_sha, flush=True)
    candidate_layout, stock_layout = inspect(candidate), inspect(stock_image)
    if candidate_layout["encoding"] != "plain" or candidate_layout["bytes"] != 1611661312 or \
            [(p["start"], p["sectors"], p["label"]) for p in candidate_layout["partitions"]] != [(2048, 1048576, "LILOS"), (1050624, 2097152, "LILROMS")]:
        raise ValueError("unexpected protected candidate layout")
    if candidate_layout["decoded_mbr"][:446] != stock_layout["decoded_mbr"][:446] or candidate_layout["prefix"][512:] != stock_layout["prefix"][512:]:
        raise ValueError("candidate's protected stock boot prefix differs")
    folder_before = check_directory(stock_directory, candidate.with_suffix(".img.json"))
    if not folder_before["passed"]:
        raise ValueError("owner stock directory boot dependencies differ")
    with tempfile.TemporaryDirectory(prefix=".freeze-", dir=DESTINATION) as temporary:
        work = Path(temporary)
        os_tree, rom_tree = work / "os-tree", work / "roms-tree"
        original_os = export_volume(candidate, 1048576, os_tree)
        original_roms = export_volume(candidate, 537919488, rom_tree)
        print("BothPartitionsExtractedAndInventoried", flush=True)
        payload = os_tree / "lilsmoke"
        rows = parse_manifest(small_text(payload / "runtime.tsv"))
        original_rows = list(rows)
        for name in BOOT_FILES:
            path = os_tree / "cubegm" / ("icube.stock" if name == "icube" else name)
            if sha(path) != reference["stock_hashes"][name]:
                raise ValueError("protected boot dependency differs: " + name)
            if name.endswith((".uImage", ".dll")):
                uimage(path)
        if sha(os_tree / "cubegm/usr/lib/libhcfota.so") != reference["required_vendor_libs"]["usr/lib/libhcfota.so"]:
            raise ValueError("protected vendor libhcfota differs")
        rows, removed = remove_prototype(rows, payload)
        audit = audit_contents(os_tree, rom_tree, rows, work)
        print("PrivateContentAndBlankPersistenceChecksPassed", flush=True)
        rows, document_hashes = add_documents(os_tree, rows)
        write_manifest(payload, rows)
        os_mib, rom_mib = 1024, 768
        start = 2048 + os_mib * 2048
        layout_text = small_text(payload / "layout.conf")
        if layout_text.count("ROMStart=1050624") != 1 or "ExpandOnFirstBoot=yes" not in layout_text:
            raise ValueError("unexpected embedded layout contract")
        (payload / "layout.conf").write_text(layout_text.replace("ROMStart=1050624", f"ROMStart={start}"), encoding="ascii")
        runtime = dict(records=len(rows), objects=sum(r[0] in ("F", "C") for r in rows),
                       regular_bytes=sum(p.stat().st_size for p in (payload / "files").iterdir()))
        report = dict(reference)
        for key in ("image_sha256", "output", "partitions", "pc_tools", "hardlink_not_frozen_snapshot"):
            report.pop(key, None)
        report.update(status="frozen-user-requested-upload-preparation", release_name=VERSION,
                      runtime=runtime, upload_performed=False, device_flashed=False,
                      physical_boot_verified=False, content_integrity_verified=True,
                      legal_clearance_automated=False, manual_redistribution_review_required=True,
                      public_distribution_allowed=False, frozen_independent_regular_file=True,
                      protected_candidate_sha256=candidate_sha, protected_stock_sha256=stock_sha,
                      source_img_sha256=stock_sha, source_unchanged=True,
                      removed_prototype_records=removed, embedded_document_sha256=document_hashes,
                      known_source_notices_gaps=GAPS, content_audit=audit,
                      geometry=dict(lilos_mib=os_mib, lilroms_seed_mib=rom_mib, rom_start_sector=start),
                      fat_growth=dict(cluster_sectors=16, max_disk_mib=131072),
                      verification_scope="Regular-image host verification only; hardware boot/growth not tested.")
        (payload / "candidate.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        (os_tree / "RELEASE-CHECKS.txt").write_text(
            "Frozen Pre-Alpha-0.0.1 image prepared at the owner's explicit request.\n"
            "Content integrity is separate from license/source clearance. No upload or SD write performed.\n"
            "LILOS: 1 GiB FAT32; LILROMS: 768 MiB growable FAT32 seed, capacity reserved for up to 128 GiB cards.\n"
            "Private full SM64 and obsolete TinyGL prototype excluded; empty user-data directories preserved.\n"
            "Initial persist.ext4 blank; swap payload zero. Blank root password build default retained.\n"
            "Stock firmware and opaque stock recovery resources retained byte-for-byte.\n"
            "Known source/notices gaps (not an assertion of legal clearance):\n" + "\n".join("- " + gap for gap in GAPS) + "\n", encoding="ascii")
        os_expected, rom_expected = inventory(os_tree), inventory(rom_tree)
        permitted_changes = {"lilsmoke/runtime.tsv", "lilsmoke/layout.conf", "lilsmoke/candidate.json"}
        removed_objects = {"lilsmoke/files/" + r[3] for r in removed if r[0] in ("F", "C")}
        for name, item in original_os.items():
            if name not in permitted_changes | removed_objects and os_expected.get(name) != item:
                raise ValueError("retained stock/runtime content changed: " + name)
        compare_inventory(original_roms, rom_expected)
        remaining = {r[2]: r for r in rows}
        if any(remaining.get(r[2]) != r for r in original_rows if r[2] not in PROTOTYPE):
            raise ValueError("unrelated runtime manifest record changed")
        p1, p2 = work / "lilos.fat", work / "lilroms.fat"
        sparse_file(p1, os_mib * MIB)
        sparse_file(p2)
        run(["mkfs.fat", "-F", "32", "-S", "512", "-s", "8", "-R", "64", "-h", "2048", "-n", "LILOS", p1])
        normalize_os_fat(p1, os_mib * 2048)
        growth = prepare_growable_fat(p2, rom_mib, start, 131072)
        for tree, volume in ((os_tree, p1), (rom_tree, p2)):
            for path in sorted(tree.iterdir()):
                run(["mcopy", "-s", "-n", "-i", volume, path, "::/"])
            synchronize_fsinfo(volume)
        geometry = [fat_geometry(p1, 2048, os_mib * 2048, "LILOS"), fat_geometry(p2, start, rom_mib * 2048, "LILROMS")]
        fat_checks = [run(["fsck.fat", "-n", volume]) for volume in (p1, p2)]
        print("NewFATGeometryAndFsckPassed", flush=True)
        prefix = new_prefix(candidate_layout, os_mib, rom_mib)
        probe_report = growth_probe(prefix, p1, p2, work)
        print("ShiftedStartGrowthRetryAndRefusalPassed", flush=True)
        partial = output.with_suffix(".img.partial")
        assemble(partial, prefix, p1, p2)
        built = inspect(partial)
        if built["bytes"] >= 2 * 1024 * MIB or built["prefix"][:446] != candidate_layout["prefix"][:446] or built["prefix"][512:] != candidate_layout["prefix"][512:]:
            raise ValueError("asset size/protected boot prefix check failed")
        compare_inventory(os_expected, export_volume(partial, 1048576, work / "os-readback"))
        compare_inventory(rom_expected, export_volume(partial, start * 512, work / "roms-readback"))
        if sha(candidate) != candidate_sha or signature(candidate) != before_candidate or sha(stock_image) != stock_sha or signature(stock_image) != before_stock:
            raise ValueError("protected source changed during freeze")
        folder_after = check_directory(stock_directory, candidate.with_suffix(".img.json"))
        if folder_after != folder_before:
            raise ValueError("owner stock boot dependency hashes changed")
        for name, expected in document_hashes.items():
            if sha(ROOT / name) != expected:
                raise ValueError("license documents changed concurrently; snapshot aborted")
        frozen_sha = sha(partial)
        if output.exists() or output.is_symlink():
            raise ValueError("output appeared concurrently; not overwriting")
        partial.rename(output)
        if plain(output).st_nlink != 1 or os.path.samefile(output, candidate):
            raise ValueError("snapshot is not an independent regular file")
        report.update(output=str(output.resolve()), image_sha256=frozen_sha, image_bytes=output.stat().st_size,
                      partitions=built["partitions"], fat_growth=growth, fat_geometry=geometry,
                      fat_checks=fat_checks, growth_probe=probe_report, readback_all_files_verified=True,
                      protected_ownerstock_dependency_check=folder_after,
                      inventories=dict(LILOS=os_expected, LILROMS=rom_expected),
                      content_audit=audit, source_and_candidate_unchanged=True,
                      github_asset_limit_bytes=2 * 1024 * MIB,
                      changed_existing_source_files=False)
        output.with_suffix(".img.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        output.with_suffix(".img.sha256").write_text(frozen_sha + "  " + output.name + "\n", encoding="ascii")
        print(json.dumps({k: report[k] for k in ("output", "image_sha256", "image_bytes", "geometry", "source_and_candidate_unchanged", "readback_all_files_verified", "public_distribution_allowed", "upload_performed")}, indent=2), flush=True)
        return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate", type=Path, default=ROOT / "build/LilSmokeOS-SEGAM-M8-V6-CANDIDATE-08.img")
    parser.add_argument("--stock-image", type=Path, default=Path("/path/to/local-input/adssad.img"))
    parser.add_argument("--stock-directory", type=Path, default=Path("/path/to/local-input/SD_RECUPERADO"))
    parser.add_argument("--verify-frozen-mips", type=Path,
                        help="read-only frozen-image check using its preserved executable on a NEW regular-file clone")
    args = parser.parse_args()
    if args.verify_frozen_mips:
        verify_frozen_mips(args.verify_frozen_mips)
    else:
        freeze(args.candidate, args.stock_image, args.stock_directory)


if __name__ == "__main__":
    main()
