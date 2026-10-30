#!/usr/bin/env python3
"""Build a NEW private M8 bring-up image from a read-only stock backup.

No physical-device writes, partition repairs, SPI tools or firmware update blobs.
"""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import tempfile
from stock_format import inspect, kernel_scripts, uimage, xor, BOOT_BYTES, SECTOR

ROOT = Path(__file__).resolve().parents[2]
BOARD = ROOT / "board/segam-m8-v6-stock"
BOOT_FILES = ("dtb.bin", "dtc.bin", "Bubbles.scr", "vmlinux.uImage", "advapi32.dll", "avp.uImage",
              "ApplicationFrame.dll", "app.uImage", "driver.so", "rkgame", "icube")
STOCK_UI_FILES = ("Arial_en.ttf", "Arial_kr.ttf", "Tahoma.ttf", "font.ttf", "resource", "resource.cpd",
                  "setting.xml", "root.dat", "allfiles.lst", "favorites.lst", "xgame-logo.bmp",
                  "joystick.cpd", "Back_In_The_City.mp3", "Button1.wav", "Button2.wav", "Button3.wav", "Button4.wav", "chord.wav")


def run(command):
    result = subprocess.run(list(map(str, command)), text=True, errors="replace", capture_output=True)
    if result.returncode:
        raise RuntimeError(f"{command}: {result.stdout}{result.stderr}")
    return result.stdout


def sha(path):
    path = Path(path)
    # Hash large NTFS backups through the native reader; WSL's DrvFS path can
    # be substantially slower. Only a quoted read-only command is executed.
    powershell = Path("/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe")
    if path.stat().st_size > 1024 * 1024 * 1024 and str(path).startswith("/mnt/c/") and powershell.is_file():
        native = "C:\\" + str(path)[7:].replace("/", "\\")
        command = "$f=[IO.File]::OpenRead('" + native.replace("'", "''") + "');" \
            "try{$h=[Security.Cryptography.SHA256]::Create();" \
            "[BitConverter]::ToString($h.ComputeHash($f)).Replace('-','')}finally{$f.Dispose()}"
        encoded = base64.b64encode(command.encode("utf-16-le")).decode("ascii")
        try:
            value = run([powershell, "-NoProfile", "-NonInteractive", "-EncodedCommand", encoded]).strip().lower()
            if len(value) == 64 and all(c in "0123456789abcdef" for c in value): return value
        except RuntimeError:
            pass  # WSL interop can time out; the read-only Linux path is valid.
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for data in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(data)
    return digest.hexdigest()


def valid_path(path):
    text = path.as_posix()
    if not text or path.is_absolute() or any(p in ("", ".", "..") for p in path.parts) or \
            any(ord(c) < 32 or ord(c) > 126 for c in text) or "\\" in text:
        raise ValueError("unsafe runtime manifest path")
    return text


def build_display_adapter(build, destination):
    compiler = build / "host/bin/mipsel-buildroot-linux-musl-gcc"
    run([compiler, "-shared", "-fPIC", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
         "-I" + str(build / "staging/usr/include/SDL"),
         BOARD / "m8-display.c", BOARD / "m8-native-video.c", BOARD / "m8-damage.c", "-ldl", "-o", destination])


def prepare_runtime(target, destination):
    files = destination / "files"; files.mkdir()
    directories, regular, links = [], [], []
    omitted = ("lib/modules", "boot", "usr/lib/modules")
    for base, dirs, names in os.walk(target, followlinks=False):
        for name in sorted(dirs + names):
            source = Path(base) / name
            path = source.relative_to(target)
            text = valid_path(path)
            if any(text == p or text.startswith(p + "/") for p in omitted):
                if name in dirs: dirs.remove(name)
                continue
            metadata = source.lstat()
            mode = f"{stat.S_IMODE(metadata.st_mode):04o}"
            if stat.S_ISLNK(metadata.st_mode):
                link = os.readlink(source)
                if any(ord(c) < 32 or ord(c) > 126 for c in link) or "\\" in link:
                    raise ValueError("unsafe runtime symlink")
                links.append(("L", mode, text, link))
                if name in dirs: dirs.remove(name)
            elif stat.S_ISDIR(metadata.st_mode):
                directories.append(("D", mode, text, ""))
            elif stat.S_ISREG(metadata.st_mode):
                number = f"{len(regular):06d}.bin"
                shutil.copyfile(source, files / number)
                kind = "C" if text in ("etc/shadow", "etc/gshadow", "etc/passwd", "etc/group") else "F"
                regular.append((kind, mode, text, number))
            else:
                raise ValueError(f"unexpected special file in target: {text}")
    rows = sorted(directories, key=lambda v: (v[2].count("/"), v[2])) + sorted(regular) + sorted(links)
    if len(rows) > 8192:
        raise ValueError("runtime tree exceeds metadata budget")
    (destination / "runtime.tsv").write_text("".join("\t".join(row) + "\n" for row in rows), encoding="ascii")
    return dict(records=len(rows), objects=len(regular), regular_bytes=sum(p.stat().st_size for p in files.iterdir()))


def prepare_boot_logo(build, destination):
    """Build the boot-only payload separately from the chroot runtime."""
    with tempfile.TemporaryDirectory(prefix="m8-logo-") as temporary:
        output = Path(temporary) / "build"
        run(["sh", ROOT / "tools/m8/build_boot_logo.sh", build, output, BOARD / "boot-logo.webp"])
        result = {}
        for name in ("m8-boot-logo", "libm8-boot-logo.so", "boot-logo.argb"):
            source = output / "payload" / name
            shutil.copyfile(source, destination / name)
            result[name] = sha(destination / name)
        return result


def stock_list(image, offset, directory):
    return run(["mdir", "-b", "-i", f"{image}@@{offset}", f"::/{directory}/*"]).splitlines()


def extract_stock(image, offset, destination):
    destination.mkdir()
    source = f"{image}@@{offset}"
    for name in BOOT_FILES + STOCK_UI_FILES:
        run(["mcopy", "-n", "-i", source, f"::/cubegm/{name}", destination / name])
    for entry in stock_list(image, offset, "cubegm"):
        name = entry.rsplit("/", 1)[-1]
        if name.startswith("ui_") and name.endswith(".cpd"):
            run(["mcopy", "-n", "-i", source, entry if entry.startswith("::/") else "::" + entry, destination / name])
    for name in ("lib", "usr"):
        # These are vendor runtime files, not the console's ROM directories.
        run(["mcopy", "-s", "-n", "-i", source, f"::/cubegm/{name}", destination])
    # Do not distribute BIOS/game data incidentally bundled in the vendor lib dir.
    for path in list(destination.rglob("*")):
        if path.is_file() and (path.suffix.lower() == ".zip" or "bios" in path.name.lower() or
                              path.name == "hcfota"):
            path.unlink()
    return {name: sha(destination / name) for name in BOOT_FILES}


def extract_stock_directory(directory, destination, report_path=None, source_img_sha256=None):
    from check_stock_directory import copy_directory, DEFAULT_REPORT
    return copy_directory(directory, destination, report_path or DEFAULT_REPORT, source_img_sha256)


def assemble(output, prefix, p1, p2):
    with output.open("xb") as stream:
        stream.write(prefix)
        for payload in (p1, p2):
            with payload.open("rb") as source:
                for data in iter(lambda: source.read(1024 * 1024), b""):
                    if any(data): stream.write(data)
                    else: stream.seek(len(data), 1)
        stream.truncate()
        stream.flush(); os.fsync(stream.fileno())


def prepare_growable_fat(path, seed_mib, start, max_disk_mib):
    """Keep the formatter's FAT/data offsets, shrinking only the seed BPBs.

    The final FAT table is allocated now. First boot changes volume length,
    not FAT layout or ROM locations. Both Windows and Linux see standard FAT32.
    """
    seed_sectors = seed_mib * 2048
    with path.open("wb") as stream:
        stream.truncate(max_disk_mib * 1024 * 1024 - start * SECTOR)
    run(["mkfs.vfat", "-F", "32", "-S", "512", "-s", "16", "-R", "64",
         "-h", start, "-n", "LILROMS", path])
    with path.open("r+b") as stream:
        boot = bytearray(stream.read(SECTOR))
        reserved = struct.unpack_from("<H", boot, 14)[0]
        fat = struct.unpack_from("<I", boot, 36)[0]
        overhead = reserved + boot[16] * fat
        clusters = (seed_sectors - overhead) // boot[13]
        if clusters < 65525:
            raise ValueError("seed too small for reserved FAT capacity")
        backup = struct.unpack_from("<H", boot, 50)[0]
        info = struct.unpack_from("<H", boot, 48)[0]
        struct.pack_into("<I", boot, 32, seed_sectors)
        for sector in (0, backup):
            stream.seek(sector * SECTOR); stream.write(boot)
        stream.seek(info * SECTOR)
        fsinfo = bytearray(stream.read(SECTOR))
        struct.pack_into("<II", fsinfo, 488, clusters - 1, 3)
        for sector in (info, backup + info):
            stream.seek(sector * SECTOR); stream.write(fsinfo)
        stream.truncate(seed_sectors * SECTOR)
    return dict(fat_sectors=fat, cluster_sectors=boot[13], max_disk_mib=max_disk_mib)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--stock-image", type=Path, required=True)
    p.add_argument("--stock-directory", type=Path,
                   help="import allowlisted contents from an extracted stock folder instead of mcopy; "
                        "--stock-image remains required for the raw MBR/boot gap and full provenance")
    p.add_argument("--stock-directory-report", type=Path,
                   help="Candidate06 JSON reference for the folder's eleven boot files and libhcfota")
    p.add_argument("--build", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--payload-output", type=Path,
                   help="also retain a NEW file-backed runtime for a non-formatting card update")
    p.add_argument("--pc-tools", type=Path,
                   help="verified public Device Adapter delivery to include inside LILOS")
    p.add_argument("--experimental", action="store_true", required=True)
    p.add_argument("--mbr-format", choices=("plain", "stock-xor"), default="plain",
                   help="plain exposes both partitions to Windows; stock-xor is archival bring-up only")
    p.add_argument("--roms-mib", type=int, default=1024)
    p.add_argument("--max-sd-mib", type=int, default=131072)
    a = p.parse_args()
    if a.stock_directory_report and not a.stock_directory:
        p.error("--stock-directory-report requires --stock-directory")
    if not 128 <= a.roms_mib <= 1024:
        p.error("ROM seed must be between 128 and 1024 MiB")
    if not 65536 <= a.max_sd_mib <= 131072:
        p.error("reserved FAT capacity must target 64 to 128 GiB cards")
    if a.output.exists() or a.output.is_symlink() or a.output.suffix != ".img":
        p.error("output must be a NEW regular .img path")
    if a.payload_output and (a.payload_output.exists() or a.payload_output.is_symlink()):
        p.error("payload output must be a NEW directory")
    a.output.parent.mkdir(parents=True, exist_ok=True)
    before = a.stock_image.stat()
    source_hash = sha(a.stock_image)
    print("StockBackupSHA256=" + source_hash, flush=True)
    source = inspect(a.stock_image)
    if source["encoding"] != "hc16xx-xor-deadbeef" or len(source["partitions"]) != 1 or source["partitions"][0]["start"] != 2048:
        raise ValueError("not the inspected M8 stock layout")
    required_tools = ("mcopy", "mmd", "mkfs.vfat", "mkswap", "mke2fs")
    if not a.stock_directory:
        required_tools += ("mdir",)
    for tool in required_tools:
        if not shutil.which(tool): raise ValueError(f"missing {tool}")
    target = a.build / "target"
    compiler = a.build / "host/bin/mipsel-buildroot-linux-musl-gcc"
    with tempfile.TemporaryDirectory(prefix="m8-candidate-", dir="/root") as temp:
        work = Path(temp)
        stock = work / "cubegm"
        directory_import = None
        if a.stock_directory:
            hashes, directory_import = extract_stock_directory(a.stock_directory, stock,
                                                               a.stock_directory_report, source_hash)
        else:
            hashes = extract_stock(a.stock_image, source["partitions"][0]["offset_bytes"], stock)
        print("StockBootFilesImported", flush=True)
        headers = {name: uimage(stock / name) for name in BOOT_FILES if name.endswith((".uImage", ".dll"))}
        startup = kernel_scripts(stock / "advapi32.dll")
        if "/mnt/sdcard/cubegm/icube &" not in startup["scripts"]["etc/init.d/S99app"]:
            raise ValueError("unsupported cold-boot route")
        payload = work / "lilsmoke"; payload.mkdir()
        runtime = prepare_runtime(target, payload)
        print("RuntimePrepared=" + json.dumps(runtime), flush=True)
        run([compiler, "-static", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
             BOARD / "m8-probe.c", "-o", payload / "m8-probe"])
        run([compiler, "-static", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
             BOARD / "m8-grow.c", "-o", payload / "m8-grow"])
        build_display_adapter(a.build, payload / "libm8-display.so")
        boot_logo = prepare_boot_logo(a.build, payload)
        shutil.copyfile(BOARD / "m8-launch.sh", payload / "m8-launch.sh")
        shutil.copyfile(stock / "icube", stock / "icube.stock")
        shutil.copyfile(BOARD / "icube-entry.sh", stock / "icube")
        defaults = (target / "etc/lilsmoke/lilsmoke.conf.default").read_text()
        defaults += "\nDEVICE_MODE=tv\nDISPLAY_WIDTH=1280\nDISPLAY_HEIGHT=720\nTV_SAFE_AREA_PERCENT=5\nSHOW_BATTERY=off\nSNES_EMULATOR=pocketsnes\nPSX_EMULATOR=pcsx4all\n"
        (payload / "initial.conf").write_text(defaults)
        persist = payload / "persist.ext4"
        with persist.open("wb") as stream: stream.truncate(64 * 1024 * 1024)
        run(["mke2fs", "-q", "-t", "ext4", "-F", "-m", "0", "-L", "LILPERSIST", persist])
        swap = stock / "pagefile.sys"
        with swap.open("wb") as stream: stream.truncate(128 * 1024 * 1024)
        run(["mkswap", "-f", swap])
        # Memory/boot aliases are preserved byte-for-byte, never interchanged.
        for name in BOOT_FILES:
            check = stock / ("icube.stock" if name == "icube" else name)
            if sha(check) != hashes[name]: raise ValueError("stock boot dependency changed")
        report = dict(status="experimental-unverified-hardware", source_img_sha256=source_hash,
                      source_bytes=source["bytes"], source_encoding=source["encoding"],
                      encoding="plain" if a.mbr_format == "plain" else source["encoding"], stock_hashes=hashes,
                      required_vendor_libs={"usr/lib/libhcfota.so": sha(stock / "usr/lib/libhcfota.so")},
                      uimages=headers, startup=startup, runtime=runtime,
                      kernel_replaced=False, device_flashed=False, physical_boot_verified=False,
                      roms_filesystem="fat32", automatic_expansion=True,
                      audio="PocketSNES isolated native PCM; audible physical verification pending; other SDL apps muted",
                      display_adapter="HC16xx scale/unblank after SDL present; physical validation pending",
                      windows_accessible=a.mbr_format == "plain",
                      ram_root="metadata/symlinks only; ELF and asset bytes remain file-backed on LILOS")
        report["boot_logo"] = boot_logo
        if directory_import is not None:
            report["stock_directory_import"] = directory_import
        (payload / "layout.conf").write_text("LayoutVersion=2\nBoard=segam-m8-v6-stock\nROMStart=1050624\nExpandOnFirstBoot=yes\nBootAdapter=stock-preserved-experimental\n")
        p1, p2 = work / "0.fat", work / "1.fat"
        with p1.open("wb") as stream: stream.truncate(512 * 1024 * 1024)
        run(["mkfs.vfat", "-F", "32", "-S", "512", "-R", "64", "-h", "2048", "-n", "LILOS", p1])
        report["fat_growth"] = prepare_growable_fat(p2, a.roms_mib, 1050624, a.max_sd_mib)
        (payload / "candidate.json").write_text(json.dumps(report, indent=2) + "\n")
        run(["mcopy", "-s", "-i", p1, stock, "::/"])
        run(["mcopy", "-s", "-i", p1, payload, "::/"])
        if a.pc_tools:
            from pc_tools_payload import copy_to_fat
            report['pc_tools'] = copy_to_fat(a.pc_tools,p1)
        run(["mmd", "-i", p2, "::/roms", "::/bios", "::/themes", "::/portsmoke"])
        for system in ("nes", "snes", "gb", "gbc", "gba", "md", "sms", "gg", "pce", "psx", "n64", "arcade", "lynx"):
            run(["mmd", "-i", p2, "::/roms/" + system])
        for folder in ("engines", "ports", "saves"):
            run(["mmd", "-i", p2, "::/portsmoke/" + folder])
        run(["mmd", "-i", p2, "::/opendingux", "::/opendingux/apps"])
        # Directory creation updates primary FSInfo; keep the backup consistent.
        with p2.open("r+b") as stream:
            stream.seek(SECTOR); fsinfo = stream.read(SECTOR)
            stream.seek(7 * SECTOR); stream.write(fsinfo)
        # Preserve the stock boot gap and decoded disk identity. A normal MBR
        # is necessary for Rufus and Windows to expose both labelled volumes.
        decoded = bytearray(source["decoded_mbr"])
        decoded[446:510] = bytes(64)
        for slot, (start, size, boot) in enumerate(((2048, 1048576, 128), (1050624, a.roms_mib * 2048, 0))):
            entry = struct.pack("<B3sB3sII", boot, b"\xfe\xff\xff", 0xc, b"\xfe\xff\xff", start, size)
            decoded[446 + slot * 16:462 + slot * 16] = entry
        prefix = bytearray(source["prefix"])
        prefix[:SECTOR] = decoded if a.mbr_format == "plain" else xor(decoded)
        assemble(a.output, prefix, p1, p2)
        if sha(a.stock_image) != source_hash or (before.st_size, before.st_mtime_ns) != \
                (a.stock_image.stat().st_size, a.stock_image.stat().st_mtime_ns):
            raise RuntimeError("source backup changed unexpectedly")
        built = inspect(a.output)
        if [part["label"] for part in built["partitions"]] != ["LILOS", "LILROMS"]:
            raise RuntimeError("candidate labels failed verification")
        report.update(output=str(a.output.resolve()), image_sha256=sha(a.output), source_unchanged=True,
                      partitions=built["partitions"])
        a.output.with_suffix(".img.json").write_text(json.dumps(report, indent=2) + "\n")
        a.output.with_suffix(".img.sha256").write_text(report["image_sha256"] + "  " + a.output.name + "\n")
        if a.payload_output:
            shutil.copytree(payload, a.payload_output)
        print(json.dumps({key: report[key] for key in ("output", "image_sha256", "source_unchanged", "physical_boot_verified", "runtime", "partitions")}, indent=2))


if __name__ == "__main__":
    main()
