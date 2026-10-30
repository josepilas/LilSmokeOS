#!/usr/bin/env python3
"""Stage or assemble a NEW private R36SX candidate. Never opens a block device."""
import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path

from prepare_image_plan import ARCHIVE_SHA256, build_plan

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/m8"))
from build_stock_candidate import prepare_growable_fat, assemble, sha as large_file_sha
from stock_format import inspect

MIB = 1024 * 1024
SHELL_ENTRIES = ("icube", "icube.sh", "icube_start.sh", "icubemp_start.sh")
LOCKED_ACCOUNTS = {"passwd": "root:x:0:0:root:/root:/bin/sh\n", "group": "root:x:0:\n",
                   "shadow": "root:!:0:0:99999:7:::\n", "gshadow": "root:!::\n"}


def run(args, allowed=(0,), timeout=1200):
    result = subprocess.run(list(map(str, args)), text=True, capture_output=True, timeout=timeout,
                            stdin=subprocess.DEVNULL, start_new_session=True)
    if result.returncode not in allowed:
        raise RuntimeError(repr(args) + "\n" + result.stdout + result.stderr)
    return result.stdout + result.stderr


def sha(path):
    path = Path(path)
    if path.stat().st_size > 1024 * MIB and str(path).startswith("/mnt/c/"):
        return large_file_sha(path)
    h = hashlib.sha256()
    with Path(path).open("rb") as source:
        for chunk in iter(lambda: source.read(MIB), b""):
            h.update(chunk)
    return h.hexdigest()


def plain_tree(path):
    path = Path(path)
    if not path.is_dir() or path.is_symlink():
        raise ValueError("Expected regular source directory")
    files = {}
    for file in sorted(path.rglob("*")):
        if file.is_symlink() or not (file.is_file() or file.is_dir()):
            raise ValueError("Symlinks/special files cannot be placed on FAT: " + str(file))
        if file.is_file():
            name = file.relative_to(path).as_posix()
            if any(part in ("", ".", "..") for part in name.split("/")):
                raise ValueError("Unsafe path")
            files[name] = {"bytes": file.stat().st_size, "sha256": sha(file)}
    return files


def patch_autorun(data):
    body = re.sub(r"^\s*<\?xml[^?]*\?>", "", data, count=1)
    root = ET.fromstring("<stock>" + body + "</stock>")
    nodes = root.findall("autorun")
    if len(nodes) != 1:
        raise ValueError("Expected exactly one stock autorun")
    nodes[0].set("file", "/mnt/sdcard/MD/lilsmoke.md")
    nodes[0].set("driver", "")
    return '<?xml version="1.0" encoding="utf-8"?>\n' + "\n".join(
        ET.tostring(child, encoding="unicode") for child in root) + "\n"


def verify_stock(stock, plan):
    for path, expected in plan["stock_preserve_byte_exact"].items():
        file = stock / path
        if file.is_symlink() or not file.is_file():
            raise ValueError("Missing protected stock file: " + path)
        data = file.read_bytes()
        if len(data) != expected["size"] or f"{zlib.crc32(data):08X}" != expected["crc32"].upper():
            raise ValueError("Protected stock file differs: " + path)


def verify_component(path, standalone=False, freestanding=False):
    data = path.read_bytes()
    if len(data) < 52 or data[:7] != b"\x7fELF\x01\x01\x01" or struct.unpack_from("<H", data, 18)[0] != 8:
        raise ValueError("Expected little-endian ELF32 MIPS: " + str(path))
    offset = struct.unpack_from("<I", data, 28)[0]
    size, count = struct.unpack_from("<HH", data, 42)
    if size != 32 or count > 64 or offset + size * count > len(data):
        raise ValueError("Invalid ELF program headers")
    for i in range(count):
        kind, start, _, _, length, _, _, _ = struct.unpack_from("<8I", data, offset + i * size)
        if start + length > len(data):
            raise ValueError("Invalid ELF segment")
        if standalone and kind == 3:
            raise ValueError("A stock-side helper cannot require a dynamic interpreter")
        if freestanding and kind == 2:
            if length % 8:
                raise ValueError("Invalid dynamic segment")
            for pos in range(start, start + length, 8):
                tag, _ = struct.unpack_from("<II", data, pos)
                if tag == 0:
                    break
                if tag == 1:
                    raise ValueError("Stock plugin cannot depend on libc or another runtime")


def validate_runtime(runtime):
    rows, objects, seen = [], set(), set()
    for line in (runtime / "runtime.tsv").read_text(encoding="ascii").splitlines():
        row = line.split("\t")
        if len(row) != 4 or row[0] not in ("D", "F", "C", "L") or not re.fullmatch("[0-7]{4}", row[1]):
            raise ValueError("Malformed runtime manifest")
        name = row[2]
        if name.startswith("/") or "\\" in name or any(p in ("", ".", "..") for p in name.split("/")) or name in seen:
            raise ValueError("Unsafe runtime path")
        seen.add(name)
        if row[0] in ("F", "C"):
            if not re.fullmatch(r"[0-9]{6}\.bin", row[3]) or not (runtime / "files" / row[3]).is_file():
                raise ValueError("Missing runtime object")
            objects.add(row[3])
        rows.append(row)
    if len(rows) > 8192 or len(rows) < 10:
        raise ValueError("Unexpected runtime size")
    for required in ("usr/bin/lilsmoke-ui", "bin/busybox", "usr/lib/lilsmoke/m8-safe-power"):
        if required not in seen:
            raise ValueError("Missing runtime executable: " + required)
    return rows, objects


def handheld_retroarch(text):
    replacements = {"        menu_ratio=1\n": "        menu_ratio=0\n",
                    "        menu_lock=3\n": "        menu_lock=1\n",
                    '        [ "$MODE" != menu ] || video_aspect=1.777778\n':
                    '        [ "$MODE" != menu ] || video_aspect=1.333333\n'}
    for old, new in replacements.items():
        if text.count(old) != 1:
            raise ValueError("Unexpected RetroArch wrapper: handheld ratio patch must be reviewed")
        text = text.replace(old, new)
    return text


def verify_fat32(path, start, sectors):
    with path.open("rb") as source:
        base = start * 512
        source.seek(base)
        boot = source.read(512)
        if len(boot) != 512 or boot[510:] != b"\x55\xaa" or boot[82:90] != b"FAT32   ":
            raise ValueError("Invalid FAT32 boot sector")
        bps = struct.unpack_from("<H", boot, 11)[0]
        spc, reserved, fats = boot[13], struct.unpack_from("<H", boot, 14)[0], boot[16]
        total, fat = struct.unpack_from("<I", boot, 32)[0], struct.unpack_from("<I", boot, 36)[0]
        info, backup = struct.unpack_from("<HH", boot, 48)
        if bps != 512 or spc not in (1, 2, 4, 8, 16, 32, 64, 128) or fats != 2 or total != sectors:
            raise ValueError("FAT32 geometry does not match the partition")
        data = total - reserved - fats * fat
        clusters = data // spc
        if not (65525 <= clusters < 0x0FFFFFF5) or fat * 128 < clusters + 2:
            raise ValueError("FAT32 classification/capacity failure: Windows would not see a valid FAT32 volume")
        if not (0 < info < reserved and 0 < backup < reserved and backup + info < reserved):
            raise ValueError("Invalid backup BPB/FSInfo location")
        if struct.unpack_from("<I", boot, 28)[0] != start:
            raise ValueError("Incorrect hidden-sector offset")
        source.seek(base + backup * 512)
        if source.read(512) != boot:
            raise ValueError("Backup boot sector differs")
        source.seek(base + info * 512)
        fsinfo = source.read(512)
        if (fsinfo[:4], fsinfo[484:488], fsinfo[508:512]) != (b"RRaA", b"rrAa", b"\0\0\x55\xaa"):
            raise ValueError("Invalid FSInfo signatures")
        source.seek(base + (backup + info) * 512)
        if source.read(512) != fsinfo:
            raise ValueError("Backup FSInfo differs")
    return {"label": boot[71:82].decode("ascii").rstrip(), "data_clusters": clusters,
            "cluster_sectors": spc, "fat_sectors": fat, "windows_classification": "FAT32",
            "backup_boot_exact": True, "backup_fsinfo_exact": True}


def audit_allowlist(report):
    source = Path(report["source_report"]).parent.parent / "stock-report.json"
    stock = json.loads(source.read_text())
    if stock["archive_sha256"] != ARCHIVE_SHA256 or stock["roms_extracted"]:
        raise ValueError("Prepared stock report lineage/privacy check failed")
    accounts = LOCKED_ACCOUNTS
    files = report["os_files"]
    for name, expected in stock["files"].items():
        actual = "cubegm/setting.stock.xml" if name == "cubegm/setting.xml" else name
        if files.get(actual, {}).get("sha256") != expected["sha256"]:
            raise ValueError("Allowlisted stock bytes changed: " + actual)
    for name, contents in accounts.items():
        if files.get("rootfs/etc/" + name, {}).get("sha256") != hashlib.sha256(contents.encode("ascii")).hexdigest():
            raise ValueError("Locked baseline accounts changed")
    allowed = set(stock["files"]) | {"rootfs/etc/" + name for name in accounts}
    allowed |= {"cubegm/setting.stock.xml", "cubegm/cores/libemu_md.so", "cubegm/allfiles.lst", "cubegm/favorites.lst", "cubegm/recent.lst"}
    if report.get("diagnostic_revision") == 2:
        allowed |= {"cubegm/" + name for name in SHELL_ENTRIES} | {"cubegm/pagefile.sys"}
        for name in SHELL_ENTRIES:
            if files.get("cubegm/" + name, {}).get("sha256") != report["shell_entry_sha256"]:
                raise ValueError("R36 shell entry changed")
    actual = {name for name in files if name.startswith(("rootfs/", "cubegm/"))}
    if actual != allowed:
        raise ValueError("Stock stage contains unexpected or missing files: " + repr(sorted(actual ^ allowed)))
    if report["rom_files"]:
        raise ValueError("The ROM partition must contain empty folders only")


def stage(args):
    if args.output.exists() or args.output.is_symlink():
        raise ValueError("Output must be NEW")
    inventory = json.loads(args.inventory.read_text())
    probe = json.loads(args.probe.read_text())
    plan = build_plan(inventory, probe)
    verify_stock(args.stock, plan)
    for name, item in probe["files"].items():
        file = args.stock / name
        if name in plan["stock_preserve_byte_exact"] and sha(file) != item["sha256"]:
            raise ValueError("Protected stock SHA256 differs: " + name)
    plain_tree(args.stock)
    rows, objects = validate_runtime(args.runtime)
    ui_object = next(row[3] for row in rows if row[2] == "usr/bin/lilsmoke-ui")
    for name in ("libemu_md.so", "r36-launch.sh", "r36-input", "libr36-display.so", "r36-bootlog", "r36-entry.sh"):
        if not (args.components / name).is_file():
            raise ValueError("Components are incomplete")
    verify_component(args.components / "libemu_md.so", freestanding=True)
    verify_component(args.components / "libr36-display.so")
    for name in ("m8-probe", "m8-grow", "r36-input", "m8-safe-power", "r36-bootlog"):
        verify_component(args.components / name, standalone=True)
    if args.ui_sha256:
        if sha(args.ui_binary or args.runtime / "files" / ui_object) != args.ui_sha256:
            raise ValueError("UI is not the exact parent-qualified object")
    if args.session_binary:
        verify_component(args.session_binary)
    if args.ui_binary and b"/usr/lib/lilsmoke/lilsmoke-session\0" in args.ui_binary.read_bytes():
        if not args.session_binary and not any(row[2] == "usr/lib/lilsmoke/lilsmoke-session" for row in rows):
            raise ValueError("This UI requires the matching game-session helper")
    args.output.mkdir(parents=True)
    os_tree, rom_tree = args.output / "LILOS", args.output / "LILROMS"
    os_tree.mkdir(); rom_tree.mkdir()
    for name in ("cubegm", "rootfs"):
        shutil.copytree(args.stock / name, os_tree / name)
    for name, contents in LOCKED_ACCOUNTS.items():
        (os_tree / "rootfs/etc" / name).write_bytes(contents.encode("ascii"))
    verify_stock(os_tree, plan)
    setting = os_tree / "cubegm/setting.xml"
    shutil.copyfile(setting, setting.with_name("setting.stock.xml"))
    setting.write_text(patch_autorun(setting.read_text(encoding="utf-8")), encoding="utf-8")
    config = (os_tree / "cubegm/cores/config.xml").read_text(encoding="utf-8")
    if "libemu_md.so" not in config:
        raise ValueError("Stock extension mapping does not contain libemu_md.so")
    shutil.copyfile(args.components / "libemu_md.so", os_tree / "cubegm/cores/libemu_md.so")
    for name in SHELL_ENTRIES:
        if (os_tree / "cubegm" / name).exists():
            raise ValueError("Refusing to overwrite a supplied stock startup entry")
        shutil.copyfile(args.components / "r36-entry.sh", os_tree / "cubegm" / name)
    # A fresh swap container avoids depending on an initramfs fallocate operation
    # before it will call icube.sh. No bytes from the owner's old swap are used.
    swap = os_tree / "cubegm/pagefile.sys"
    with swap.open("xb") as stream:
        stream.truncate(128 * MIB)
        header = bytearray(4096)
        struct.pack_into("<III", header, 1024, 1, 128 * MIB // 4096 - 1, 0)
        header[4086:4096] = b"SWAPSPACE2"
        stream.write(header)
    (os_tree / "MD").mkdir()
    (os_tree / "MD/lilsmoke.md").write_bytes(b"LILSMOKE_R36_BOOTSTRAP\n")
    (os_tree / "MD/filelist.csv").write_text("lilsmoke.md,LilSmokeOS,MD\n", encoding="ascii")
    for name in ("allfiles.lst", "favorites.lst", "recent.lst"):
        (os_tree / "cubegm" / name).write_text("", encoding="ascii")
    payload = os_tree / "lilsmoke"
    (payload / "files").mkdir(parents=True)
    shutil.copyfile(args.runtime / "runtime.tsv", payload / "runtime.tsv")
    for name in objects:
        shutil.copyfile(args.runtime / "files" / name, payload / "files" / name)
    if args.session_binary:
        if any(row[2] == "usr/lib/lilsmoke/lilsmoke-session" for row in rows):
            raise ValueError("Review an existing session helper before replacing it")
        extra = next((f"{i:06d}.bin" for i in range(999999, 900000, -1) if f"{i:06d}.bin" not in objects), None)
        if not extra:
            raise ValueError("No available runtime object ID")
        shutil.copyfile(args.session_binary, payload / "files" / extra)
        objects.add(extra)
        rows.append(["F", "0755", "usr/lib/lilsmoke/lilsmoke-session", extra])
        (payload / "runtime.tsv").write_text("\n".join("\t".join(row) for row in rows) + "\n", encoding="ascii")
    power_object = next(row[3] for row in rows if row[2] == "usr/lib/lilsmoke/m8-safe-power")
    shutil.copyfile(args.components / "m8-safe-power", payload / "files" / power_object)
    ra_object = next(row[3] for row in rows if row[2] == "usr/lib/lilsmoke/lilsmoke-retroarch")
    ra_script = payload / "files" / ra_object
    ra_script.write_text(handheld_retroarch(ra_script.read_text()), encoding="ascii")
    for name in ("m8-probe", "m8-grow", "m8-safe-power"):
        shutil.copyfile(args.components / name, payload / name)
    for name in ("r36-launch.sh", "r36-input", "libr36-display.so", "r36-bootlog"):
        shutil.copyfile(args.components / name, payload / name)
    if args.ui_binary:
        verify_component(args.ui_binary)
        shutil.copyfile(args.ui_binary, payload / "files" / ui_object)
    (payload / "boot-journal.bin").write_bytes(bytes(16384))
    manifest = {"profile": plan["profile_id"], "revision": 2,
                "protected": {name: sha(os_tree / name) for name in plan["stock_preserve_byte_exact"]}}
    (payload / "boot-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="ascii")
    diagnostics = os_tree / "diagnostics"
    diagnostics.mkdir()
    shutil.copyfile(ROOT / "tools/r36sx/boot_report.py", diagnostics / "boot_report.py")
    (diagnostics / "README.txt").write_text(
        "R36SX boot diagnostics (read only)\n"
        "On a PC with Python 3: python boot_report.py PATH_TO_LILOS\n"
        "Return lilsmoke/boot-journal.bin, bringup.log, bringup.previous.log and kernel-early.log.\n"
        "A blank journal means no checkpoint persisted, not proof of a failed kernel.\n"
        "If neither entry is reached, collect board-specific UART boot output.\n"
        "Do not connect a PC serial port directly to unknown-voltage board pins.\n", encoding="ascii")
    # Existing application scripts use this old filename; its bytes are R36-only.
    shutil.copyfile(args.components / "libr36-display.so", payload / "libm8-display.so")
    defaults = args.runtime / "initial.conf"
    baseline = next((row[3] for row in rows if row[2] == "etc/lilsmoke/lilsmoke.conf.default"), None)
    text = defaults.read_text() if defaults.is_file() else (args.runtime / "files" / baseline).read_text() if baseline else ""
    replace = {"DEVICE_MODE": "handheld", "DISPLAY_WIDTH": "640", "DISPLAY_HEIGHT": "480",
               "TV_SAFE_AREA_PERCENT": "0", "SHOW_BATTERY": "text", "ASSET_CACHE_MB": "4",
               "UI_RENDER_MODE": "native", "DEFAULT_THEME": '"default"', "REWIND_BUFFER_MB": "0",
               "ZRAM_ENABLED": "off", "LILSMOKE_VERSION": '"Pre-Alpha-0.0.1-R36SX-experimental"'}
    lines = [line for line in text.splitlines() if line.split("=", 1)[0] not in replace]
    (payload / "initial.conf").write_text("\n".join(lines + [key + "=" + value for key, value in replace.items()]) + "\n")
    for folder in ("roms", "bios", "themes", "independent-apps", "packages", "opendingux/apps"):
        (rom_tree / folder).mkdir(parents=True, exist_ok=True)
    for system in ("nes", "snes", "gb", "gbc", "gba", "md", "sms", "gg", "pce", "psx", "n64", "arcade", "lynx"):
        (rom_tree / "roms" / system).mkdir()
    for name in ("LICENSE", "LICENSE_POLICY.md", "THIRD_PARTY_NOTICES.md", "CREDITS.md"):
        if (ROOT / name).is_file():
            shutil.copyfile(ROOT / name, os_tree / name)
    if (ROOT / "LICENSES").is_dir():
        shutil.copytree(ROOT / "LICENSES", os_tree / "LICENSES")
    for folder, names in ((ROOT, ("SOURCE-OFFER.txt",)), (args.runtime, ("SOURCE-OFFER.txt", "SOURCE-OFFER.baseline.txt"))):
        for name in names:
            if (folder / name).is_file():
                shutil.copyfile(folder / name, os_tree / ("BASELINE-" + folder.name + "-" + name))
    (os_tree / "SOURCE-OFFER.txt").write_text(
        "PRIVATE EXPERIMENTAL R36SX IMAGE - NOT SOURCE-COMPLIANCE CLEARANCE.\n"
        "Third-party notices and any available baseline source references are preserved.\n"
        "A complete exact-version BSP/vendor and third-party corresponding-source offer\n"
        "has NOT been established by this builder. Do not represent this as a public release.\n"
        "See THIRD_PARTY_NOTICES.md and the build report. No ROMs or BIOS are included.\n")
    report = {"profile": plan["profile_id"], "archive_sha256": ARCHIVE_SHA256,
              "diagnostic_revision": 2, "shell_entry_sha256": sha(args.components / "r36-entry.sh"),
              "status": "private-experimental-unverified-hardware", "physical_boot_verified": False,
              "runtime_manifest_sha256": sha(payload / "runtime.tsv"), "runtime_objects": len(objects),
              "ui_sha256": sha(payload / "files" / ui_object), "ui_object": "lilsmoke/files/" + ui_object,
              "session_object": "lilsmoke/files/" + extra if args.session_binary else None,
              "protected_paths": sorted(plan["stock_preserve_byte_exact"]),
              "source_report": str(args.stock), "protected_files_unchanged": True,
              "boot_route": "R36 shell/sentinel entries or stock autorun -> checkpointed O32 execve bootstrap -> musl exec/chroot",
              "display": "640x480; no /dev/dis or HDMI/MIPI timing ioctls",
              "input": "same-inode SysV panel key; uinput with ABI1 broker fallback; four USB slots retained",
              "audio": "bundled native PCM backends, R36-only 632-byte auddec ioctl adapter; not stock ALSA",
              "limitations": ["exact stock rkgame acceptance unverified", "uinput kernel support unverified; broker fallback built/tested",
                              "native game scaling/audio and handheld shutdown unverified",
                              "complete corresponding source/vendor redistribution rights not established"],
              "partitions": plan["partitions"], "os_files": plain_tree(os_tree), "rom_files": plain_tree(rom_tree)}
    (args.output / "stage.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({key: report[key] for key in ("status", "runtime_objects", "protected_files_unchanged", "physical_boot_verified")}, indent=2))


def image(args):
    if sys.platform != "linux":
        raise ValueError("Assemble under WSL/Linux with mtools, not a Windows physical drive")
    if args.output.exists() or args.output.is_symlink() or args.output.suffix != ".img":
        raise ValueError("Output must be a NEW .img regular-file path")
    if str(args.output).startswith("/dev/"):
        raise ValueError("Block devices are prohibited")
    report = json.loads((args.stage / "stage.json").read_text())
    if report["archive_sha256"] != ARCHIVE_SHA256 or report["physical_boot_verified"]:
        raise ValueError("Unexpected stage lineage/status")
    for folder, key in (("LILOS", "os_files"), ("LILROMS", "rom_files")):
        if plain_tree(args.stage / folder) != report[key]:
            raise ValueError("Stage changed after qualification")
    audit_allowlist(report)
    p1, p2 = report["partitions"]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    required = (p2["start_sector"] + p2["sectors"]) * 512 * 2 + 500 * MIB
    available = shutil.disk_usage(args.output.parent).free
    if available < required:
        raise ValueError(f"Insufficient assembly margin: {available} bytes free, {required} required; no image created")
    with tempfile.TemporaryDirectory(prefix="r36-file-image-") as temporary:
        work = Path(temporary)
        osfat, romfat = work / "0.fat", work / "1.fat"
        with osfat.open("xb") as stream:
            stream.truncate(p1["sectors"] * 512)
        run(["mkfs.vfat", "-F", "32", "-S", "512", "-R", "64", "-g", "255/32",
             "-h", p1["start_sector"], "-n", "LILOS", osfat])
        with osfat.open("rb") as stream:
            bpb = stream.read(512)
        if struct.unpack_from("<I", bpb, 32)[0] != p1["sectors"]:
            raise ValueError("Formatter rounded the OS BPB; no image assembled")
        growth = prepare_growable_fat(romfat, p2["sectors"] // 2048, p2["start_sector"], 131072)
        for volume, tree in ((osfat, args.stage / "LILOS"), (romfat, args.stage / "LILROMS")):
            for entry in sorted(tree.iterdir()):
                run(["mcopy", "-s", "-i", volume, entry, "::/"])
            with volume.open("r+b") as stream:
                stream.seek(512); info = stream.read(512); stream.seek(7 * 512); stream.write(info)
            run(["fsck.vfat", "-n", volume])
        prefix = bytearray(1024 * 1024)
        prefix[440:444] = bytes.fromhex(report["runtime_manifest_sha256"][:8])
        prefix[510:512] = b"\x55\xaa"
        for slot, part in enumerate((p1, p2)):
            prefix[446 + slot * 16:462 + slot * 16] = struct.pack(
                "<B3sB3sII", 128 if slot == 0 else 0, b"\xfe\xff\xff", 12, b"\xfe\xff\xff",
                part["start_sector"], part["sectors"])
        assemble(args.output, prefix, osfat, romfat)
    qualify_image(args, report, growth)


def qualify_image(args, report, growth):
    p1, p2 = report["partitions"]
    parsed = inspect(args.output)
    if [part["label"] for part in parsed["partitions"]] != ["LILOS", "LILROMS"]:
        raise ValueError("Image label verification failed")
    fat_reports = [verify_fat32(args.output, part["start_sector"], part["sectors"]) for part in (p1, p2)]
    # Read back the actual image filesystem, not merely the staging tree.
    with tempfile.TemporaryDirectory(prefix="r36-image-readback-") as name:
        targets = report["protected_paths"] + [report["ui_object"], "cubegm/cores/libemu_md.so"]
        if report.get("diagnostic_revision") == 2:
            targets += ["cubegm/" + path for path in SHELL_ENTRIES] + [
                "cubegm/pagefile.sys", "lilsmoke/r36-launch.sh", "lilsmoke/r36-bootlog",
                "lilsmoke/boot-journal.bin", "lilsmoke/boot-manifest.json", "diagnostics/boot_report.py"]
        if report.get("session_object"):
            targets.append(report["session_object"])
        for index, relative in enumerate(targets):
            recovered = Path(name) / f"{index:03d}.bin"
            expected = report["os_files"][relative]
            run(["mcopy", "-o", "-i", str(args.output) + "@@" + str(p1["start_sector"] * 512),
                 "::/" + relative, recovered], timeout=60)
            if sha(recovered) != expected["sha256"]:
                raise ValueError("Image readback SHA256 differs: " + relative)
        run(["mdir", "-i", str(args.output) + "@@" + str(p2["start_sector"] * 512), "::/roms"])
    report.update(image=str(args.output.resolve()), image_sha256=sha(args.output),
                  image_bytes=args.output.stat().st_size, image_generated=True, fat_growth=growth,
                  mbr="plain; generated zero boot gap, no M8 sectors borrowed",
                  physical_device_accessed=False, fat_validation=fat_reports,
                  image_readback_protected_sha256=True, image_readback_ui_sha256=True,
                  exact_stock_allowlist_verified=True, no_roms_bios_saves_private_accounts=True)
    report.pop("os_files"); report.pop("rom_files")
    args.output.with_suffix(".img.json").write_text(json.dumps(report, indent=2) + "\n")
    args.output.with_suffix(".img.sha256").write_text(report["image_sha256"] + "  " + args.output.name + "\n")
    print(json.dumps({key: report[key] for key in ("image", "image_sha256", "image_bytes", "physical_boot_verified")}, indent=2))


def verify_existing(args):
    if args.output.is_symlink() or not args.output.is_file() or args.output.suffix != ".img":
        raise ValueError("Only an existing regular-file image can be verified")
    report = json.loads((args.stage / "stage.json").read_text())
    if report["archive_sha256"] != ARCHIVE_SHA256 or report["physical_boot_verified"]:
        raise ValueError("Unexpected stage lineage/status")
    for folder, key in (("LILOS", "os_files"), ("LILROMS", "rom_files")):
        if plain_tree(args.stage / folder) != report[key]:
            raise ValueError("Stage changed after qualification")
    audit_allowlist(report)
    qualify_image(args, report, {"max_disk_mib": 131072, "cluster_sectors": 16, "verification_only": True})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="mode", required=True)
    make = sub.add_parser("stage")
    for name in ("stock", "inventory", "probe", "runtime", "components", "output"):
        make.add_argument("--" + name, type=Path, required=True)
    make.add_argument("--ui-sha256", help="Require the exact parent-qualified UI object")
    make.add_argument("--ui-binary", type=Path, help="Use a separately qualified ELF32 MIPS UI")
    make.add_argument("--session-binary", type=Path, help="Bundle the matching bounded game-session helper")
    pack = sub.add_parser("image")
    pack.add_argument("--stage", type=Path, required=True)
    pack.add_argument("--output", type=Path, required=True)
    check = sub.add_parser("verify", help="Read-only verification of an existing file image; emit reports")
    check.add_argument("--stage", type=Path, required=True)
    check.add_argument("--output", type=Path, required=True)
    for command in (make, pack, check):
        command.add_argument("--private-experimental", action="store_true", required=True)
    args = parser.parse_args()
    {"stage": stage, "image": image, "verify": verify_existing}[args.mode](args)


if __name__ == "__main__":
    main()
