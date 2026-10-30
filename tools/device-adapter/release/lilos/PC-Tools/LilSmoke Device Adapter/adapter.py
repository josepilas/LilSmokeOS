#!/usr/bin/env python3
"""Folder-only, private firmware adapter. Never opens a raw disk or runs firmware."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import plistlib
import re
import shutil
import stat
import struct
import subprocess
import sys
import time
import uuid
import zlib

VERSION = "1.0.0"
BASE = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent))
BOOT_FILES = ("dtb.bin", "dtc.bin", "Bubbles.scr", "vmlinux.uImage", "advapi32.dll",
              "avp.uImage", "ApplicationFrame.dll", "app.uImage", "driver.so", "rkgame", "icube")
GATE_FILES = BOOT_FILES + ("usr/lib/libhcfota.so",)
HOOK = b"#!/bin/sh\n# Called by this exact stock kernel's S99app. Never patch SPI or load a new DTB.\nexec /mnt/sdcard/lilsmoke/m8-launch.sh\n"
MAX_FILE = 64 * 1024 * 1024
MAX_JSON = 4 * 1024 * 1024
MAX_TOTAL = 512 * 1024 * 1024
CONTROL = "lilsmoke/device-adapter"


class Refusal(ValueError):
    pass


def digest_bytes(data):
    return hashlib.sha256(data).hexdigest()


def io_path(path):
    """Extended Windows names avoid legacy MAX_PATH on deep export folders."""
    path = Path(path)
    if os.name != "nt":
        return path
    value = os.path.abspath(str(path))
    if value.startswith("\\\\?\\"):
        return Path(value)
    if value.startswith("\\\\"):
        return Path("\\\\?\\UNC\\" + value[2:])
    return Path("\\\\?\\" + value)


def relative(text):
    if not isinstance(text, str) or not text or len(text) > 512 or "\\" in text:
        raise Refusal("Invalid relative path")
    parts = text.split("/")
    if any(p in ("", ".", "..") or p.endswith((".", " ")) or ":" in p for p in parts):
        raise Refusal("Unsafe path: " + text)
    if any(ord(c) < 32 or ord(c) > 126 for c in text) or PurePosixPath(text).is_absolute():
        raise Refusal("Unsafe path: " + text)
    if any(re.fullmatch(r"(?i)(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?", p) for p in parts):
        raise Refusal("Reserved path: " + text)
    return text


def plain(path, directory=False):
    value = io_path(path).lstat()
    if stat.S_ISLNK(value.st_mode) or getattr(value, "st_file_attributes", 0) & 0x400:
        raise Refusal("Links and Windows reparse points are refused: " + str(path))
    if not (stat.S_ISDIR(value.st_mode) if directory else stat.S_ISREG(value.st_mode)):
        raise Refusal("Not a regular " + ("directory: " if directory else "file: ") + str(path))
    return value


def safe_absolute(path, must_exist=True):
    value = os.path.expanduser(str(path))
    if os.name == "nt":
        if value.startswith("\\\\?\\UNC\\"):
            value = "\\\\" + value[8:]
        elif value.startswith("\\\\?\\"):
            value = value[4:]
    path = Path(os.path.abspath(value))
    current = Path(path.anchor)
    plain(current, True)
    for part in path.parts[1:]:
        current /= part
        if io_path(current).exists() or io_path(current).is_symlink():
            plain(current, current != path or io_path(current).is_dir())
        elif must_exist:
            raise Refusal("Missing path: " + str(current))
    return path


def within(root, name, missing=False):
    root = safe_absolute(root)
    plain(root, True)
    path = root / relative(name)
    safe_absolute(path, not missing)
    return path


def separate(*paths):
    values = [safe_absolute(p, False) for p in paths]
    for i, a in enumerate(values):
        for b in values[i + 1:]:
            if a == b or a in b.parents or b in a.parents:
                raise Refusal("Source, export and target must not overlap: " + str(a) + " / " + str(b))


def signature(s):
    return s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns


def hash_file(path, maximum=MAX_FILE):
    path = safe_absolute(path)
    before = plain(path)
    if before.st_size > maximum:
        raise Refusal("File exceeds inspection budget: " + str(path))
    hasher = hashlib.sha256()
    fd = os.open(io_path(path), os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    with os.fdopen(fd, "rb") as stream:
        if signature(os.fstat(stream.fileno())) != signature(before):
            raise Refusal("File replaced while opening")
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(block)
        after = os.fstat(stream.fileno())
    if signature(before) != signature(after) or signature(after) != signature(plain(path)):
        raise Refusal("File changed during inspection: " + str(path))
    return hasher.hexdigest(), before.st_size


def read_small(path, maximum=MAX_JSON):
    before = plain(safe_absolute(path))
    if before.st_size > maximum:
        raise Refusal("Metadata exceeds budget")
    with io_path(path).open("rb") as stream:
        data = stream.read(maximum + 1)
    if len(data) > maximum or signature(before) != signature(plain(path)):
        raise Refusal("Metadata changed or too large")
    return data


def read_json(path):
    def pairs(items):
        result = {}
        for k, v in items:
            if k in result:
                raise Refusal("Duplicate JSON key: " + k)
            result[k] = v
        return result
    try:
        return json.loads(read_small(path), object_pairs_hook=pairs)
    except (UnicodeError, json.JSONDecodeError) as error:
        raise Refusal("Invalid JSON: " + str(path)) from error


def sync_directory(path):
    if os.name != "nt":
        fd = os.open(path, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
        try:
            os.fsync(fd)
        except OSError:
            # Some mounted FAT drivers do not implement directory fsync.
            pass
        finally:
            os.close(fd)


def write_atomic(path, data):
    path = safe_absolute(path, False)
    plain(path.parent, True)
    temporary = path.with_name(path.name + ".tmp-" + uuid.uuid4().hex)
    try:
        with io_path(temporary).open("xb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(io_path(temporary), io_path(path))
        sync_directory(path.parent)
    finally:
        if io_path(temporary).exists():
            io_path(temporary).unlink()


def write_json(path, value):
    write_atomic(path, (json.dumps(value, indent=2, sort_keys=True) + "\n").encode())


def copy_checked(source, target, expected, progress=None):
    before = plain(safe_absolute(source))
    if before.st_size > MAX_FILE:
        raise Refusal("Firmware dependency too large")
    target = safe_absolute(target, False)
    io_path(target.parent).mkdir(parents=True, exist_ok=True)
    safe_absolute(target.parent)
    digest = hashlib.sha256()
    fd = os.open(io_path(source), os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    with os.fdopen(fd, "rb") as src, io_path(target).open("xb") as dst:
        if signature(os.fstat(src.fileno())) != signature(before):
            raise Refusal("Source replaced")
        for block in iter(lambda: src.read(1024 * 1024), b""):
            digest.update(block)
            dst.write(block)
            if progress:
                progress(len(block))
        dst.flush()
        os.fsync(dst.fileno())
        after = os.fstat(src.fileno())
    if signature(before) != signature(after) or signature(after) != signature(plain(source)):
        raise Refusal("Source changed while copying")
    if digest.hexdigest() != expected or hash_file(target)[0] != expected:
        raise Refusal("Copy/readback hash mismatch: " + str(target))


def command(args):
    result = subprocess.run(args, capture_output=True, timeout=30, check=True)
    return result.stdout


def macos_identity(path, run=command):
    data = plistlib.loads(run(["diskutil", "info", "-plist", str(path)]))
    disk = data.get("ParentWholeDisk", data.get("DeviceIdentifier"))
    whole = plistlib.loads(run(["diskutil", "info", "-plist", disk])) if disk else {}
    unique = next((whole.get(k) for k in ("MediaUUID", "DiskUUID", "DeviceSerialNumber", "SerialNumber")
                   if isinstance(whole.get(k), str) and whole[k].strip() and
                   set(whole[k].replace("-", "")) != {"0"}), None)
    result = dict(root=data.get("MountPoint"), label=data.get("VolumeName", ""),
                  filesystem=data.get("FilesystemType", ""), volume_id=data.get("VolumeUUID"),
                  disk_id="mac:" + unique if unique else None,
                  hardware_id=unique, whole_device=disk, whole_bytes=whole.get("TotalSize"),
                  offset=data.get("PartitionMapPartitionOffset"), bytes=data.get("TotalSize"),
                  external=whole.get("Internal") is False, system=data.get("MountPoint") == "/",
                  read_only=bool(data.get("ReadOnlyMedia") or data.get("ReadOnlyVolume") or whole.get("ReadOnlyMedia")),
                  device=data.get("DeviceIdentifier"), roms=[])
    if disk and result["label"] == "LILOS":
        listing = plistlib.loads(run(["diskutil", "list", "-plist", disk]))
        count = 0
        for row in listing.get("AllDisksAndPartitions", []):
            for part in row.get("Partitions", []):
                count += 1
                if part.get("VolumeName") == "LILROMS":
                    info = plistlib.loads(run(["diskutil", "info", "-plist", part["DeviceIdentifier"]]))
                    result["roms"].append(dict(FileSystem=info.get("FilesystemType", "")))
        result["partition_count"] = count
    return result


def linux_identity(path, mountinfo=None, sys_root=Path("/sys"), run=command):
    chosen = None
    def decode(value):
        return re.sub(r"\\([0-7]{3})", lambda m: chr(int(m[1], 8)), value)
    mountinfo = Path("/proc/self/mountinfo").read_text() if mountinfo is None else mountinfo
    for line in mountinfo.splitlines():
        fields, tail = line.split(" - ", 1)
        fields, tail = fields.split(), tail.split()
        mount = Path(decode(fields[4]))
        if path == mount or mount in path.parents:
            if chosen is None or len(str(mount)) > len(chosen["root"]):
                chosen = dict(root=str(mount), filesystem=tail[0], device=decode(tail[1]),
                              volume_id=fields[2], disk_id=None, external=False, system=str(mount) == "/", label="")
    if not chosen:
        raise Refusal("Cannot identify Linux mount")
    node = sys_root / "dev/block" / chosen["volume_id"]
    if node.exists():
        node = node.resolve()
        disk = node.parent if (node / "partition").exists() else node
        chosen["disk_id"] = "linux:" + str(disk)
        removable = disk / "removable"
        chosen["external"] = "/usb" in str(disk).replace("\\", "/") or (removable.exists() and removable.read_text().strip() == "1")
        chosen["whole_device"] = "/dev/" + disk.name
        chosen["offset"] = int((node / "start").read_text()) * 512 if (node / "start").exists() else None
        chosen["bytes"] = int((node / "size").read_text()) * 512 if (node / "size").exists() else None
        chosen["whole_bytes"] = int((disk / "size").read_text()) * 512 if (disk / "size").exists() else None
        chosen["read_only"] = (disk / "ro").exists() and (disk / "ro").read_text().strip() != "0"
        # The whole sysfs path alone is reusable after card substitution. A
        # serial (USB/MMC) is required to approve writes; ambiguous readers refuse.
        serial = None
        for location in (disk / "device/serial", disk / "serial", disk / "device/cid"):
            if location.exists() and location.read_text().strip():
                serial = location.read_text().strip()
                break
        if not serial:
            for parent in disk.parents:
                location = parent / "serial"
                if parent == sys_root or sys_root not in parent.parents:
                    break
                if location.exists() and location.read_text().strip():
                    serial = location.read_text().strip()
                    break
        chosen["hardware_id"] = serial
        chosen["disk_id"] = "linux:" + serial if serial else None
    try:
        labels = json.loads(run(["lsblk", "--json", "--output", "PATH,LABEL,FSTYPE", chosen["device"]]))
        chosen["label"] = labels["blockdevices"][0].get("label") or ""
        if chosen["label"] == "LILOS" and chosen.get("whole_device"):
            listing = json.loads(run(["lsblk", "--json", "--output", "PATH,LABEL,FSTYPE", chosen["whole_device"]]))
            parts = [p for d in listing["blockdevices"] for p in d.get("children", [])]
            chosen["partition_count"] = len(parts)
            chosen["roms"] = [dict(FileSystem=p.get("fstype", "")) for p in parts if p.get("label") == "LILROMS"]
    except (OSError, ValueError, subprocess.SubprocessError):
        pass
    return chosen


def identity(path):
    """Read-only mounted-volume identity, including whole-disk identity when known."""
    path = safe_absolute(path)
    if os.name == "nt":
        from ctypes import wintypes as w
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.GetVolumePathNameW.argtypes = (w.LPCWSTR, w.LPWSTR, w.DWORD)
        kernel.GetVolumePathNameW.restype = w.BOOL
        kernel.GetVolumeInformationW.argtypes = (w.LPCWSTR, w.LPWSTR, w.DWORD,
                                                ctypes.POINTER(w.DWORD), ctypes.POINTER(w.DWORD),
                                                ctypes.POINTER(w.DWORD), w.LPWSTR, w.DWORD)
        kernel.GetVolumeInformationW.restype = w.BOOL
        volume = ctypes.create_unicode_buffer(1024)
        if not kernel.GetVolumePathNameW(str(path), volume, len(volume)):
            raise Refusal("Cannot identify Windows volume")
        label, fs = ctypes.create_unicode_buffer(256), ctypes.create_unicode_buffer(256)
        serial, maxname, flags = w.DWORD(), w.DWORD(), w.DWORD()
        if not kernel.GetVolumeInformationW(volume.value, label, 256, ctypes.byref(serial),
                                           ctypes.byref(maxname), ctypes.byref(flags), fs, 256):
            raise Refusal("Cannot read Windows volume identity")
        result = dict(root=volume.value, label=label.value, filesystem=fs.value.lower(),
                      volume_id=str(serial.value), disk_id=None, external=False)
        if re.fullmatch(r"[A-Za-z]:\\", volume.value):
            drive = volume.value[0]
            script = ("$ErrorActionPreference='Stop';$p=Get-Partition -DriveLetter '" + drive + "';"
                      "$d=$p|Get-Disk;$v=$p|Get-Volume;"
                      "$all=@(Get-Partition -DiskNumber $d.Number);$r=$all|Get-Volume|"
                      "Where-Object FileSystemLabel -eq LILROMS;"
                      "[pscustomobject]@{disk=[string]$d.UniqueId;external=($d.BusType -in @('USB','SD','MMC'));"
                      "system=($d.IsBoot -or $d.IsSystem);offset=$p.Offset;size=$p.Size;whole_bytes=$d.Size;"
                      "read_only=($d.IsReadOnly -or $d.IsOffline);partition_count=$all.Count;"
                      "roms=@($r|Select-Object FileSystemLabel,FileSystem)}|ConvertTo-Json -Depth 4")
            try:
                disk = json.loads(command(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", script]))
                result.update(disk_id="win:" + disk["disk"], external=bool(disk["external"]),
                              system=bool(disk["system"]), offset=disk["offset"], bytes=disk["size"],
                              roms=disk["roms"], whole_bytes=disk["whole_bytes"], read_only=disk["read_only"],
                              partition_count=disk["partition_count"])
            except (OSError, ValueError, subprocess.SubprocessError):
                pass
        return result
    if sys.platform == "darwin":
        return macos_identity(path)
    if sys.platform.startswith("linux"):
        return linux_identity(path)
    raise Refusal("Unsupported PC platform")


def profile_load():
    profile = read_json(BASE / "profiles/segam-m8-v6.json")
    files = profile.get("files", {})
    if profile.get("schema") != 1 or not set(GATE_FILES).issubset(files) or len(files) > 2048:
        raise Refusal("Invalid bundled profile")
    seen = set()
    for name, row in files.items():
        relative(name)
        if name.casefold() in seen or not re.fullmatch(r"[0-9a-f]{64}", row.get("sha256", "")) or not 0 < row.get("bytes", 0) <= MAX_FILE:
            raise Refusal("Invalid profile file")
        seen.add(name.casefold())
    if sum(row["bytes"] for row in files.values()) > MAX_TOTAL:
        raise Refusal("Profile exceeds budget")
    return profile


def fingerprint(data):
    if data[:4] == b"\x7fELF" and len(data) >= 20:
        order = "little" if data[5] == 1 else "big"
        return dict(kind="elf", bits=32 if data[4] == 1 else 64,
                    endian=order, machine=int.from_bytes(data[18:20], order))
    if data[:4] == b"\xd0\x0d\xfe\xed":
        return dict(kind="fdt", bytes=int.from_bytes(data[4:8], "big"))
    if data[:4] == b"\x27\x05\x19\x56":
        return dict(kind="uimage")
    return dict(kind="opaque")


def inspect(source, profile=None, progress=None):
    profile = profile or profile_load()
    root = safe_absolute(source)
    plain(root, True)
    selection = str(root)
    if (root / "cubegm").exists() or (root / "cubegm").is_symlink():
        root = within(root, "cubegm")
        plain(root, True)
    files, total = {}, 0
    for index, name in enumerate(GATE_FILES):
        row = {}
        try:
            path = within(root, name)
            actual, size = hash_file(path)
            with io_path(path).open("rb") as stream:
                row.update(fingerprint(stream.read(64)))
            row.update(sha256=actual, bytes=size, matches=actual == profile["files"][name]["sha256"])
            total += size
        except (OSError, Refusal) as error:
            row.update(matches=False, error=str(error))
        files[name] = row
        if progress:
            progress(index + 1, len(GATE_FILES), name)
    matched = all(v["matches"] for v in files.values())
    return dict(schema=1, tool_version=VERSION, source=str(root), source_selection=selection, source_identity=identity(root),
                files=files, hashed_bytes=total, profile_id=profile["id"] if matched else None,
                compatibility="exact-profile" if matched else "unknown-export-only", read_only=True,
                physical_device_certified=False, reserved_boot_region_available=False,
                reference_image_sha256=profile["reference_image_sha256"],
                warning="Exact files match the tested owner's M8 lineage, not certification of another board. "
                        "Folder import cannot supply reserved boot sectors. Target must already have a compatible LilSmoke base.")


def export_bundle(source, destination, profile=None, progress=None):
    profile = profile or profile_load()
    report = inspect(source, profile)
    root, destination = Path(report["source"]), safe_absolute(destination, False)
    separate(Path(report["source_selection"]), destination)
    separate(root, destination)
    if destination.exists():
        raise Refusal("Export must be a new directory")
    plain(destination.parent, True)
    names = sorted(profile["files"] if report["profile_id"] else [k for k, v in report["files"].items() if "sha256" in v])
    total = sum(profile["files"][k]["bytes"] if report["profile_id"] else report["files"][k]["bytes"] for k in names)
    if shutil.disk_usage(destination.parent).free < total + 8 * 1024 * 1024:
        raise Refusal("Not enough free space for export")
    io_path(destination).mkdir()
    io_path(destination / "files/cubegm").mkdir(parents=True)
    write_json(destination / "source.json", report)
    inventory, done = {}, 0
    def copied(amount):
        nonlocal done
        done += amount
        if progress:
            progress(done, total, "Exporting private firmware")
    for name in names:
        expected = profile["files"][name] if report["profile_id"] else report["files"][name]
        copy_checked(within(root, name), within(destination, "files/cubegm/" + name, True), expected["sha256"], copied)
        if hash_file(within(root, name))[0] != expected["sha256"]:
            raise Refusal("Stock source changed after export")
        inventory[name] = dict(sha256=expected["sha256"], bytes=expected["bytes"])
    write_json(destination / "inventory.json", inventory)
    complete = dict(schema=1, complete=True, profile_id=report["profile_id"],
                    inventory_sha256=hash_file(destination / "inventory.json")[0],
                    source_sha256=hash_file(destination / "source.json")[0],
                    private_firmware=True, file_count=len(inventory), bytes=total)
    # This marker is written last; interrupted exports cannot become install inputs.
    write_json(destination / "complete.json", complete)
    return complete


def validate_bundle(bundle, profile):
    bundle = safe_absolute(bundle)
    complete = read_json(within(bundle, "complete.json"))
    if complete.get("complete") is not True or complete.get("profile_id") != profile["id"]:
        raise Refusal("Incomplete or unknown firmware bundle: export-only")
    for file, key in (("inventory.json", "inventory_sha256"), ("source.json", "source_sha256")):
        if hash_file(within(bundle, file))[0] != complete.get(key):
            raise Refusal("Bundle metadata was modified")
    inventory = read_json(within(bundle, "inventory.json"))
    if inventory != profile["files"]:
        raise Refusal("Bundle does not match the public exact-file profile")
    for name, row in inventory.items():
        actual, size = hash_file(within(bundle, "files/cubegm/" + name))
        if actual != row["sha256"] or size != row["bytes"]:
            raise Refusal("Modified exported dependency: " + name)
    return read_json(within(bundle, "source.json"))


def target_identity(target, source_identity, provider=identity):
    value = provider(target)
    if not value.get("disk_id") or not value.get("external") or value.get("system") or value.get("read_only"):
        raise Refusal("Target must be an identifiable external card, never the PC system disk")
    if Path(value["root"]) != target or value.get("label") != "LILOS" or value.get("filesystem", "").lower() not in ("fat32", "vfat", "msdos"):
        raise Refusal("Select the mounted LILOS FAT32 volume root")
    if not source_identity.get("disk_id"):
        raise Refusal("Source physical disk is unknown; inspect/export only")
    if source_identity["disk_id"] == value["disk_id"]:
        raise Refusal("Source and target are the same physical disk")
    if provider is identity:
        if not value.get("volume_id") or any(not isinstance(value.get(k), int) or value[k] <= 0 for k in ("offset", "bytes", "whole_bytes")) or \
                value["offset"] + value["bytes"] > value["whole_bytes"] or value.get("partition_count") != 2:
            raise Refusal("Incomplete disk/partition identity or not a two-partition card: inspect/export only")
        roms = value.get("roms", [])
        if not isinstance(roms, list) or len(roms) != 1 or str(roms[0].get("FileSystem", "")).lower() not in ("fat32", "exfat", "vfat", "msdos"):
            raise Refusal("The same card must have one LILROMS FAT32/exFAT volume")
    return value


def mappings(profile):
    return {"cubegm/" + ("icube.stock" if name == "icube" else name): row for name, row in profile["files"].items()} | {
        "cubegm/icube": dict(sha256=digest_bytes(HOOK), bytes=len(HOOK))}


def validate_target(target, profile):
    candidate = read_json(within(target, "lilsmoke/candidate.json"))
    expected = {k: profile["files"][k]["sha256"] for k in BOOT_FILES}
    if candidate.get("source_img_sha256") != profile["reference_image_sha256"] or candidate.get("stock_hashes") != expected:
        raise Refusal("Target lacks exact compatible base-image provenance; no reserved-sector repair is attempted")
    layout = read_small(within(target, "lilsmoke/layout.conf"), 8192).decode("ascii")
    settings = {}
    for line in layout.splitlines():
        if not line or line.startswith("#"):
            continue
        key, equals, value = line.partition("=")
        if not equals or key in settings:
            raise Refusal("Malformed or duplicate layout setting")
        settings[key] = value
    if settings.get("Board") != "segam-m8-v6-stock" or settings.get("LayoutVersion") != "2":
        raise Refusal("Unsupported LilSmoke layout")
    launch_hash = hash_file(within(target, "lilsmoke/m8-launch.sh"))[0]
    if launch_hash not in profile["launch_sha256"]:
        raise Refusal("Unreviewed LilSmoke launcher; requires a newer adapter profile")
    manifest = read_small(within(target, "lilsmoke/runtime.tsv"), 2 * 1024 * 1024)
    lines = manifest.decode("ascii").splitlines()
    if not 1 <= len(lines) <= 8192:
        raise Refusal("Invalid runtime manifest")
    for line in lines:
        fields = line.split("\t")
        if len(fields) != 4 or fields[0] not in ("D", "F", "C", "L") or not re.fullmatch("[0-7]{4}", fields[1]):
            raise Refusal("Invalid runtime record")
        relative(fields[2])
        if fields[0] in ("F", "C"):
            relative(fields[3])
            plain(within(target, "lilsmoke/files/" + fields[3]))
    for name in ("cubegm/HCFOTA.bin", "cubegm/usr/bin/hcfota"):
        if within(target, name, True).exists():
            raise Refusal("Target contains vendor flash updater: " + name)
    if plain(within(target, "cubegm/pagefile.sys")).st_size < 128 * 1024 * 1024:
        raise Refusal("Missing stock boot-route swap file; adapter does not create it")
    return dict(launch_sha256=launch_hash, manifest_sha256=digest_bytes(manifest),
                candidate_sha256=hash_file(within(target, "lilsmoke/candidate.json"))[0],
                layout_sha256=hash_file(within(target, "lilsmoke/layout.conf"))[0])


def plan_apply(bundle, target, profile=None, provider=identity):
    profile = profile or profile_load()
    bundle, target = safe_absolute(bundle), safe_absolute(target)
    source = validate_bundle(bundle, profile)
    separate(bundle, source["source"], target)
    separate(bundle, source.get("source_selection", source["source"]), target)
    for tool in (Path(__file__).resolve(), Path(sys.executable).resolve()):
        if target == tool or target in tool.parents:
            raise Refusal("Copy the PC tool off the target card before adapting it")
    disk = target_identity(target, source["source_identity"], provider)
    exported_disk = provider(bundle)
    if exported_disk.get("disk_id") == disk["disk_id"]:
        raise Refusal("Private export is on the target card; move it to the PC first")
    markers = validate_target(target, profile)
    entries, budget = [], 0
    for name, new in sorted(mappings(profile).items(), key=lambda p: (p[0] == "cubegm/icube", p[0])):
        path = within(target, name, True)
        old = None
        if path.exists():
            old_hash, size = hash_file(path)
            old = dict(sha256=old_hash, bytes=size)
        if old and old["sha256"] == new["sha256"]:
            continue
        entries.append(dict(path=name, old=old, new=new))
        budget += new["bytes"] + (old["bytes"] if old else 0)
    if shutil.disk_usage(target).free < budget + 8 * 1024 * 1024:
        raise Refusal("Insufficient space for complete staged files and rollback backups")
    result = dict(schema=1, profile_id=profile["id"], bundle=str(bundle), target=str(target),
                  target_identity=disk, markers=markers, entries=entries, required_bytes=budget,
                  raw_writes=False, geometry_changed=False, physical_boot_verified=False,
                  boot_hook_committed_last=True)
    result["plan_sha256"] = digest_bytes(json.dumps(result, sort_keys=True).encode())
    return result


def stable_disk(target, wanted, provider):
    current = provider(target)
    for key in ("disk_id", "volume_id", "root", "label", "filesystem", "offset", "bytes", "whole_bytes", "hardware_id", "whole_device"):
        if current.get(key) != wanted.get(key):
            raise Refusal("Target device/partition changed during operation")


def reviewed_paths_equal(bundle, target, plan):
    # WindowsPath equality is case-insensitive and normalizes separators.
    return safe_absolute(bundle) == safe_absolute(plan["bundle"]) and safe_absolute(target) == safe_absolute(plan["target"])


def claim_lock(control, transaction):
    lock = within(control, "active", True)
    ready = within(control, "lock-ready-" + transaction, True)
    if ready.exists():
        owner = within(ready, "owner.json", True)
        if owner.exists():
            if read_json(owner).get("transaction") != transaction or len(list(ready.iterdir())) != 1:
                raise Refusal("Conflicting transaction lock preparation")
        elif list(ready.iterdir()):
            raise Refusal("Conflicting transaction lock preparation")
        else:
            write_json(owner, dict(transaction=transaction))
    else:
        ready.mkdir()
        write_json(ready / "owner.json", dict(transaction=transaction))
    try:
        os.rename(ready, lock)
        sync_directory(control)
    except OSError as error:
        raise Refusal("Another adapter transaction owns this card; recover it first") from error


def release_lock(control, transaction, boundary=None):
    lock = within(control, "active", True)
    released = within(control, "released-" + transaction, True)
    if lock.exists():
        if read_json(within(lock, "owner.json")).get("transaction") != transaction or released.exists():
            raise Refusal("Conflicting transaction lock release")
        # Rename the populated directory; interrupted deletion can never leave
        # an empty active lock. Released private markers do not block installs.
        os.rename(lock, released)
        sync_directory(control)
    if boundary:
        boundary("lock-released", "")
    if released.exists():
        owner = within(released, "owner.json", True)
        if owner.exists():
            if read_json(owner).get("transaction") != transaction:
                raise Refusal("Released lock belongs to another transaction")
            owner.unlink()
        if boundary:
            boundary("cleanup-owner-removed", "")
        released.rmdir()
        sync_directory(control)
    if boundary:
        boundary("cleanup-complete", "")


def apply_bundle(bundle, target, approved_hash, profile=None, provider=identity, progress=None, boundary=None):
    profile = profile or profile_load()
    plan = plan_apply(bundle, target, profile, provider)
    if plan["plan_sha256"] != approved_hash:
        raise Refusal("The reviewed plan changed; inspect it again before installing")
    target, bundle = Path(plan["target"]), Path(plan["bundle"])
    pending = within(target, CONTROL + "/active", True)
    if pending.exists():
        raise Refusal("Unfinished adapter transaction; use Recover/Rollback before installing")
    if not plan["entries"]:
        return dict(status="already-adapted", physical_boot_verified=False, changed_files=0)
    control = within(target, CONTROL, True)
    control.mkdir(parents=True, exist_ok=True)
    safe_absolute(control)
    lock = control / "active"
    transaction = uuid.uuid4().hex
    work = control / "transactions" / transaction
    work.mkdir(parents=True)
    (work / "backup").mkdir()
    (work / "stage").mkdir()
    journal = dict(schema=1, transaction=transaction, status="preparing", plan=plan,
                   entries=plan["entries"], finished=[], started_at=time.time())
    write_json(work / "journal.json", journal)
    # Publish an already populated lock atomically. There is no empty-lock
    # interruption window, and another populated active directory cannot be replaced.
    claim_lock(control, transaction)
    def save(state, path=""):
        journal["status"] = state
        journal["current"] = path
        write_json(work / "journal.json", journal)
        if boundary:
            boundary(state, path)
    try:
        for index, entry in enumerate(journal["entries"]):
            name, expected = entry["path"], entry["new"]["sha256"]
            stable_disk(target, plan["target_identity"], provider)
            if entry["old"]:
                copy_checked(within(target, name), within(work, "backup/" + name, True), entry["old"]["sha256"])
            staged = within(work, "stage/" + name, True)
            if name == "cubegm/icube":
                staged.parent.mkdir(parents=True, exist_ok=True)
                write_atomic(staged, HOOK)
            else:
                original = name.removeprefix("cubegm/")
                if original == "icube.stock":
                    original = "icube"
                copy_checked(within(bundle, "files/cubegm/" + original), staged, expected)
            save("preparing", name)
            if progress:
                progress(index + 1, len(journal["entries"]) * 2, "Backing up and staging " + name)
        save("prepared")
        if validate_target(target, profile) != plan["markers"]:
            raise Refusal("Target runtime changed during staging")
        for index, entry in enumerate(journal["entries"]):
            name = entry["path"]
            stable_disk(target, plan["target_identity"], provider)
            current = within(target, name, True)
            actual = hash_file(current)[0] if current.exists() else None
            if actual != (entry["old"]["sha256"] if entry["old"] else None):
                raise Refusal("Target file changed since review: " + name)
            save("committing", name)
            current.parent.mkdir(parents=True, exist_ok=True)
            os.replace(within(work, "stage/" + name), current)
            sync_directory(current.parent)
            if hash_file(current)[0] != entry["new"]["sha256"]:
                raise Refusal("Installed file failed readback: " + name)
            journal["finished"].append(name)
            save("committed-file", name)
            if progress:
                progress(len(journal["entries"]) + index + 1, len(journal["entries"]) * 2, "Verifying " + name)
        stable_disk(target, plan["target_identity"], provider)
        for name, expected in mappings(profile).items():
            if hash_file(within(target, name)) != (expected["sha256"], expected["bytes"]):
                raise Refusal("Final independent readback failed")
        if validate_target(target, profile) != plan["markers"]:
            raise Refusal("LilSmoke runtime changed during installation")
        save("complete")
        release_lock(control, transaction, boundary)
        return dict(status="installed", transaction=transaction, changed_files=len(journal["entries"]),
                    physical_boot_verified=False, safe_eject_required=True,
                    warning="Do not cut power during this operation. File renames are not a multi-file power-loss-atomic transaction on FAT.")
    except Exception:
        # Leave the durable journal and verified backups, including on disconnect.
        # Recovery refuses changed files rather than guessing or overwriting them.
        raise


def rollback(target, transaction=None, profile=None, provider=identity, progress=None, boundary=None):
    profile = profile or profile_load()
    target = safe_absolute(target)
    control = within(target, CONTROL)
    lock = within(control, "active", True)
    explicit = transaction is not None
    if transaction is None:
        if lock.exists():
            if not within(lock, "owner.json", True).exists():
                raise Refusal("Legacy empty active lock: select its exact verified transaction journal explicitly")
            transaction = read_json(within(lock, "owner.json"))["transaction"]
        else:
            # Cleanup may already have released the active marker. A completed
            # journal remains the durable recovery authority, never a directory name alone.
            transactions = within(control, "transactions")
            candidates = []
            directories = list(transactions.iterdir())
            if len(directories) > 128:
                raise Refusal("Too many journals; select one exact transaction explicitly")
            for folder in directories:
                if re.fullmatch("[0-9a-f]{32}", folder.name):
                    row = read_json(within(transactions, folder.name + "/journal.json"))
                    if row.get("status") in ("complete", "rolling-back", "restored-file", "rolled-back"):
                        candidates.append((row.get("started_at", 0), folder.name))
            if not candidates:
                raise Refusal("No completed recovery journal; select a transaction explicitly")
            transaction = max(candidates)[1]
    if not isinstance(transaction, str) or not re.fullmatch("[0-9a-f]{32}", transaction):
        raise Refusal("Invalid recovery transaction")
    legacy_empty = False
    if lock.exists():
        owner = within(lock, "owner.json", True)
        if not owner.exists():
            if not explicit or list(lock.iterdir()):
                raise Refusal("Empty legacy lock requires an explicit verified journal")
            legacy_empty = True
        elif read_json(owner)["transaction"] != transaction:
            raise Refusal("Another unfinished transaction must be recovered first")
    work = within(control, "transactions/" + transaction)
    journal = read_json(within(work, "journal.json"))
    plan = journal["plan"]
    if plan.get("profile_id") != profile["id"] or safe_absolute(plan.get("target", "")) != target:
        raise Refusal("Recovery target/profile mismatch")
    stable_disk(target, plan["target_identity"], provider)
    allowed = mappings(profile)
    entries = journal["entries"]
    if not isinstance(entries, list) or len(entries) > len(allowed) or len({r["path"] for r in entries}) != len(entries):
        raise Refusal("Invalid recovery entries")
    # Verify the entire rollback before changing any target file.
    restore = []
    for entry in entries:
        name = entry["path"]
        if name not in allowed or entry["new"] != allowed[name]:
            raise Refusal("Recovery destination not allowlisted")
        current = within(target, name, True)
        actual = hash_file(current)[0] if current.exists() else None
        old = entry["old"]
        old_hash = old["sha256"] if old else None
        if actual == old_hash:
            continue
        if actual != entry["new"]["sha256"]:
            raise Refusal("Target changed after installation; recovery refuses to overwrite: " + name)
        if old and hash_file(within(work, "backup/" + name))[0] != old_hash:
            raise Refusal("Rollback backup missing/corrupt")
        restore.append(entry)
    # Restore the old hook first so the previous launcher is selected again.
    restore.sort(key=lambda e: (e["path"] != "cubegm/icube", e["path"]))
    if legacy_empty:
        # Only after every journal destination, current hash and backup passed
        # validation may an old empty marker be associated with this journal.
        write_json(lock / "owner.json", dict(transaction=transaction))
    elif not lock.exists():
        claim_lock(control, transaction)
    def save_recovery(state, path=""):
        journal["status"], journal["current"] = state, path
        write_json(work / "journal.json", journal)
        if boundary:
            boundary(state, path)
    save_recovery("rolling-back")
    for index, entry in enumerate(restore):
        stable_disk(target, plan["target_identity"], provider)
        name = entry["path"]
        current = within(target, name, True)
        actual = hash_file(current)[0] if current.exists() else None
        if actual != entry["new"]["sha256"]:
            raise Refusal("Target changed during recovery: " + name)
        save_recovery("restoring-file", name)
        if entry["old"]:
            staged = current.with_name(current.name + ".restore-" + transaction)
            if io_path(staged).exists():
                # This exact transaction-owned temporary path may be truncated
                # by a previous recovery interruption. No target file is deleted.
                plain(staged)
                io_path(staged).unlink()
            copy_checked(within(work, "backup/" + name), staged, entry["old"]["sha256"])
            os.replace(staged, current)
            if hash_file(current)[0] != entry["old"]["sha256"]:
                raise Refusal("Restored file failed readback")
        else:
            current.unlink()
        sync_directory(current.parent)
        save_recovery("restored-file", name)
        if progress:
            progress(index + 1, len(restore), "Restoring " + name)
    save_recovery("rolled-back")
    # A previous interrupted cleanup can leave a private released marker.
    released = within(control, "released-" + transaction, True)
    if released.exists():
        owner = within(released, "owner.json", True)
        if owner.exists():
            if read_json(owner).get("transaction") != transaction:
                raise Refusal("Conflicting released marker")
            owner.unlink()
        released.rmdir()
        sync_directory(control)
    release_lock(control, transaction, boundary)
    return dict(status="rolled-back", transaction=transaction, restored_files=len(restore), safe_eject_required=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", action="version", version=VERSION)
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("inspect", "export", "plan", "apply", "rollback"):
        p = sub.add_parser(name)
        if name in ("inspect", "export"):
            p.add_argument("source")
        if name == "export":
            p.add_argument("destination")
        if name in ("plan", "apply"):
            p.add_argument("bundle")
        if name in ("plan", "apply", "rollback"):
            p.add_argument("target")
        if name == "apply":
            p.add_argument("--approve-plan", required=True, help="SHA256 from reviewed plan")
        if name == "rollback":
            p.add_argument("--transaction")
    args = parser.parse_args()
    try:
        if args.command == "inspect":
            result = inspect(args.source)
        elif args.command == "export":
            result = export_bundle(args.source, args.destination)
        elif args.command == "plan":
            result = plan_apply(args.bundle, args.target)
        elif args.command == "apply":
            result = apply_bundle(args.bundle, args.target, args.approve_plan)
        else:
            result = rollback(args.target, args.transaction)
        print(json.dumps(result, indent=2))
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(json.dumps(dict(status="refused", error=str(error)), indent=2), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
