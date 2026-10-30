#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Jose Pilas.
"""LilSmoke Applications SDK: trusted developer tooling, not a sandbox."""
import argparse
from contextlib import contextmanager
from dataclasses import dataclass
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

sys.dont_write_bytecode = True
VERSION = "0.3.0-dev"
HERE = Path(__file__).resolve().parent
MAX_ENTRIES = 20000
MAX_TOTAL = 256 * 1024 * 1024
MAX_FILE = 64 * 1024 * 1024
MAX_META = 65536
MAX_SCRIPT = 256 * 1024
CHUNK = 65536
# Protocol data, checked against the actual OS registry by integration tests.
SYSTEMS = {
    "nes": ["famicom", "fds"], "snes": ["sfc"], "n64": [],
    "gb": ["gameboy"], "gbc": ["gbcolor"], "gba": ["gbadvance"],
    "md": ["genesis", "megadrive"], "sms": ["mastersystem"], "gg": ["gamegear"],
    "pce": ["pcengine", "tg16", "turbografx16"], "arcade": ["fbneo", "mame"],
    "lynx": ["atarilynx"], "psx": ["ps1", "playstation"],
    "atari2600": ["a26", "stella"], "atari7800": ["a78", "prosystem"],
    "atari800": ["atari8bit", "atari5200", "a52"],
    "ngpc": ["ngp", "neogeopocket"], "wswan": ["wonderswan", "wonderswancolor"],
    "pokemini": ["pokemonmini"], "doom": ["prboom"], "cavestory": ["nxengine"],
    "easyrpg": ["rpgmaker", "rpgmaker2000", "rpgmaker2003"],
    "scummvm": [], "openbor": [],
}
ALIASES = {alias: system for system, aliases in SYSTEMS.items() for alias in [system, *aliases]}
BLOCKED = {".git", ".hg", ".svn", ".ssh", ".aws", ".azure", ".env",
           "id_rsa", "id_ed25519", ".lilsmoke-install.json"}


class SDKError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise SDKError(message)


def json_bytes(value):
    return (json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2, allow_nan=False) + "\n").encode("utf-8")


def parse_json(data):
    require(len(data) <= MAX_META, "JSON exceeds 64 KiB")
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, "Duplicate JSON key: " + key)
            result[key] = value
        return result
    def constant(value):
        raise SDKError("Non-finite JSON number: " + value)
    try:
        value = json.loads(data.decode("utf-8"), object_pairs_hook=pairs, parse_constant=constant)
        def depth(item, level=0):
            require(level <= 16, "JSON exceeds SDK nesting limit")
            if isinstance(item, dict):
                for key, child in item.items():
                    require("\0" not in key, "NUL in JSON key")
                    depth(child, level + 1)
            elif isinstance(item, list):
                for child in item:
                    depth(child, level + 1)
            elif isinstance(item, str):
                require("\0" not in item, "NUL in JSON string")
            elif isinstance(item, float):
                require(abs(item) != float("inf"), "Non-finite JSON number")
        depth(value)
        return value
    except (UnicodeError, json.JSONDecodeError, RecursionError) as error:
        raise SDKError("Invalid UTF-8 JSON: " + str(error)) from error


def component(value, hidden=False):
    require(isinstance(value, str) and value and len(value.encode("utf-8")) <= 120,
            "Name must contain 1..120 UTF-8 bytes")
    require(value not in (".", "..") and not value.endswith((".", " ")), "Unsafe name: " + value)
    require(hidden or not value.startswith("."), "Hidden application name")
    require(not any(ord(c) < 32 or ord(c) == 127 or c in '/\\:*?"<>|' for c in value),
            "Unsafe FAT filename: " + value)
    base = value.split(".")[0].lower()
    require(base not in {"con", "prn", "aux", "nul"} and
            not re.fullmatch(r"(com|lpt)[1-9]", base), "Reserved FAT filename: " + value)


def member_name(name, directory=False):
    require(isinstance(name, str) and len(name.encode("utf-8")) <= 1024, "Oversized member path")
    if directory:
        name = name.removesuffix("/")
    require(name and not name.startswith("/") and "\\" not in name, "Unsafe member path: " + name)
    parts = name.split("/")
    require(len(parts) <= 32, "SDK path depth exceeds 32 components")
    for part in parts:
        component(part, hidden=True)
        require(part.casefold() not in BLOCKED, "Reserved/private payload path: " + name)
    return name


def is_root_launcher(name):
    return "/" not in name and PurePosixPath(name).suffix.lower() == ".sh"


def metadata(data):
    meta = parse_json(data)
    require(isinstance(meta, dict), "meta.json must be an object")
    component(meta.get("name"))
    identity = meta.get("id")
    require(isinstance(identity, str) and 1 <= len(identity) <= 128 and
            re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", identity) and ".." not in identity,
            "SDK requires a stable safe explicit id")
    version = meta.get("version")
    require(isinstance(version, str) and len(version) <= 64 and re.fullmatch(
        r"[0-9]+(?:\.[0-9]+){0,2}(?:-[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)*)?"
        r"(?:\+[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)*)?", version), "Invalid version")
    if "icon" in meta:
        require(isinstance(meta["icon"], str), "icon must be a relative string")
        if meta["icon"]:
            member_name(meta["icon"])
    if "architecture" in meta:
        require(meta["architecture"] == "mipsel-linux-musl", "SDK target is mipsel-linux-musl")
    if "open_core" in meta:
        original = meta["open_core"]
        cores = [original] if isinstance(original, str) else original
        require(isinstance(cores, list) and 1 <= len(cores) <= len(SYSTEMS), "Invalid open_core list")
        normalized = []
        for core in cores:
            require(isinstance(core, str) and core.lower() in ALIASES, "Unknown open_core system")
            canonical = ALIASES[core.lower()]
            if canonical not in normalized:
                normalized.append(canonical)
        meta["open_core"] = normalized[0] if isinstance(original, str) else normalized
    return meta


def regular_file(path, limit=MAX_FILE):
    state = path.lstat()
    require(stat.S_ISREG(state.st_mode) and not path.is_symlink() and
            not (getattr(state, "st_file_attributes", 0) & 0x400), "Links/special files refused: " + str(path))
    require(state.st_nlink <= 1, "Hard-linked payload file refused: " + str(path))
    require(state.st_size <= limit, "File exceeds SDK size limit: " + str(path))
    return state.st_size


@dataclass
class Entry:
    name: str
    size: int
    source: object
    directory: bool = False


def directory_entries(root, prefix=""):
    state = root.lstat()
    require(stat.S_ISDIR(state.st_mode) and not root.is_symlink() and
            not (getattr(state, "st_file_attributes", 0) & 0x400), "Payload root must be a real directory")
    result = []
    def visit(folder, relative=""):
        with os.scandir(folder) as children:
            for child in children:
                name = prefix + relative + child.name
                state = child.stat(follow_symlinks=False)
                require(not child.is_symlink() and not (getattr(state, "st_file_attributes", 0) & 0x400),
                        "Payload links/reparse points refused: " + name)
                is_dir = stat.S_ISDIR(state.st_mode)
                member_name(name, is_dir)
                if is_dir:
                    result.append(Entry(name, 0, Path(child.path), True))
                    require(len(result) <= MAX_ENTRIES, "Too many entries")
                    visit(child.path, relative + child.name + "/")
                else:
                    size = regular_file(Path(child.path))
                    result.append(Entry(name, size, Path(child.path)))
                    require(len(result) <= MAX_ENTRIES, "Too many entries")
    visit(root)
    return result


def zip_preflight(path):
    size = regular_file(path, MAX_TOTAL)
    # Bound the directory before ZipFile constructs its Python entry list.
    with path.open("rb") as stream:
        stream.seek(max(0, size - 65557))
        tail = stream.read(65557)
    at = tail.rfind(b"PK\x05\x06")
    require(at >= 0 and len(tail) - at >= 22, "SDK accepts ZIP .lilpck archives only")
    fields = struct.unpack_from("<4s4H2IH", tail, at)
    _, disk, start_disk, disk_count, count, directory_size, offset, comment_size = fields
    require(disk == start_disk == 0 and disk_count == count and count <= MAX_ENTRIES,
            "Split/ZIP64/oversized ZIP directory refused")
    require(directory_size <= 8 * 1024 * 1024 and offset + directory_size <= size,
            "ZIP directory exceeds SDK bounds")
    require(at + 22 + comment_size == len(tail), "Trailing data or malformed ZIP footer")


def elf_check(prefix, name):
    if not prefix.startswith(b"\x7fELF"):
        return False
    require(len(prefix) >= 52 and prefix[4:7] == b"\x01\x01\x01", "Not ELF32 little-endian: " + name)
    fields = struct.unpack_from("<HHIIIIIHHHHHH", prefix, 16)
    kind, machine, version, _, phoff, _, flags, _, phsize, count, _, _, _ = fields
    require(kind in (2, 3) and machine == 8 and version == 1, "Not a MIPS executable/library: " + name)
    require(flags & 0xF0000000 == 0x70000000 and flags & 0xF000 == 0x1000 and
            not flags & (0x20 | 0x200 | 0x400), "SDK needs MIPS32r2 O32 FP32 legacy-NaN ELF: " + name)
    require(count <= 256 and (not count or phsize == 32) and phoff + phsize * count <= len(prefix),
            "ELF program table outside SDK inspection budget")
    for index in range(count):
        record = struct.unpack_from("<8I", prefix, phoff + index * phsize)
        if record[0] == 3:
            offset, length = record[1], record[4]
            require(0 < length <= 128 and offset + length <= len(prefix) and
                    prefix[offset:offset + length] == b"/lib/ld-musl-mipsel.so.1\0",
                    "Non-musl or malformed ELF interpreter: " + name)
    return True


def inspect_entries(entries, opener, elf_validator=elf_check):
    require(len(entries) <= MAX_ENTRIES, "Too many entries")
    nodes, files, total = {}, {}, 0
    for entry in entries:
        member_name(entry.name, entry.directory)
        require(0 <= entry.size <= MAX_FILE, "Member exceeds SDK file budget")
        total += entry.size
        require(total <= MAX_TOTAL, "Payload exceeds SDK expanded-byte budget")
        parts = entry.name.split("/")
        for depth in range(1, len(parts) + 1):
            path = "/".join(parts[:depth])
            kind = "dir" if depth < len(parts) or entry.directory else "file"
            key = path.casefold()
            if key in nodes:
                old_path, old_kind = nodes[key]
                require(old_path == path and old_kind == kind, "Case or file/directory collision: " + path)
            else:
                nodes[key] = (path, kind)
        require(entry.name not in files, "Duplicate member: " + entry.name)
        files[entry.name] = entry
    require("meta.json" in files and not files["meta.json"].directory, "meta.json must be at archive root")
    scripts = [e for e in entries if not e.directory and is_root_launcher(e.name)]
    require(len(scripts) == 1, "Exactly one archive-root .sh is required")
    meta_entry = files["meta.json"]
    require(meta_entry.size <= MAX_META, "meta.json exceeds 64 KiB")
    with opener(meta_entry) as stream:
        meta = metadata(stream.read(MAX_META + 1))
    icon = meta.get("icon")
    if icon:
        require(icon in files and not files[icon].directory and files[icon].size <= 2 * 1024 * 1024,
                "Icon must be a packaged regular file <=2 MiB")
    elf_files = []
    for entry in entries:
        if entry.directory:
            continue
        with opener(entry) as stream:
            consumed, prefix = 0, b""
            while True:
                block = stream.read(CHUNK)
                if not block:
                    break
                consumed += len(block)
                require(consumed <= entry.size, "Member changed or exceeds advertised size")
                if len(prefix) < CHUNK:
                    prefix += block[:CHUNK - len(prefix)]
            require(consumed == entry.size, "Truncated/changed member: " + entry.name)
        if elf_validator(prefix, entry.name):
            elf_files.append(entry.name)
        if entry.name.lower().endswith(".sh"):
            require(entry.size <= MAX_SCRIPT, "Shell script exceeds SDK limit")
            with opener(entry) as stream:
                script = stream.read(MAX_SCRIPT + 1)
            require(b"\r" not in script and b"\0" not in script, "Shell scripts need LF endings and no NUL")
            require(script.startswith(b"#!/bin/sh\n"), "Shell scripts require #!/bin/sh and LF")
            script.decode("utf-8")
    return dict(valid=True, sdk_version=VERSION, metadata=meta, launcher=scripts[0].name,
                entries=len(entries), bytes=total, elf_files=elf_files,
                limits=dict(entries=MAX_ENTRIES, total_bytes=MAX_TOTAL, file_bytes=MAX_FILE),
                qualification="Package structure/CRC/ELF identity only; not runtime or dependency qualification")


def payload_root(project):
    return project / "payload" if (project / "payload").is_dir() else project


def project_spec(project):
    path = project / "sdk.json"
    if not path.exists():
        return {}
    regular_file(path, MAX_META)
    spec = parse_json(path.read_bytes())
    require(isinstance(spec, dict) and spec.get("format") == 1 and
            spec.get("template") in ("shell", "native-c", "native-cpp", "sdl2-c"), "Invalid sdk.json")
    return spec


def project_entries(project, runtime=None):
    payload = payload_root(project)
    entries = directory_entries(payload)
    if runtime is not None:
        entries += directory_entries(runtime, "runtime/")
    if project_spec(project).get("template") in ("native-c", "native-cpp", "sdl2-c"):
        require(any(e.name == "runtime/bin/application" and not e.directory for e in entries),
                "Build the native template before validating/packaging")
    return entries


def inspect_project(project, entries):
    report = inspect_entries(entries, file_opener)
    if project_spec(project).get("template") in ("native-c", "native-cpp", "sdl2-c"):
        require("runtime/bin/application" in report["elf_files"], "Native template needs an actual target ELF")
    return report


@contextmanager
def file_opener(entry):
    regular_file(entry.source)
    with entry.source.open("rb") as stream:
        yield stream


@contextmanager
def open_payload(path, runtime=None):
    if path.is_dir():
        yield project_entries(path, runtime), file_opener
        return
    require(runtime is None, "--runtime requires a project/payload directory")
    zip_preflight(path)
    with zipfile.ZipFile(path) as archive:
        entries = []
        for info in archive.infolist():
            require("\0" not in info.orig_filename and not info.flag_bits & 1, "NUL/encrypted ZIP member")
            require(info.compress_type in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED), "Unsupported ZIP compression")
            mode = info.external_attr >> 16
            kind = stat.S_IFMT(mode)
            require(kind in (0, stat.S_IFDIR if info.is_dir() else stat.S_IFREG), "ZIP links/special files refused")
            # ZipInfo normalizes host separators on Windows; inspect raw names.
            name = member_name(info.orig_filename, info.is_dir())
            entries.append(Entry(name, info.file_size, info, info.is_dir()))
        yield entries, lambda entry: archive.open(entry.source)


def validate(path, runtime=None):
    with open_payload(path, runtime) as (entries, opener):
        if path.is_dir():
            return inspect_project(path, entries)
        return inspect_entries(entries, opener)


def package(project, output, runtime=None):
    entries = project_entries(project, runtime)
    report = inspect_project(project, entries)
    output = output.absolute()
    require(output.suffix.lower() == ".lilpck", "Output must end in .lilpck")
    for root in (payload_root(project), runtime):
        if root is not None:
            require(not output.resolve().is_relative_to(root.resolve()), "Output cannot be inside payload/runtime")
    output.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive output: never overwrite a package or unrelated developer file.
    with output.open("xb") as target:
        try:
            with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=False) as archive:
                for entry in sorted(entries, key=lambda e: e.name):
                    if entry.directory:
                        continue
                    info = zipfile.ZipInfo(entry.name, (2020, 1, 1, 0, 0, 0))
                    info.create_system = 3
                    executable = entry.name.lower().endswith(".sh") or entry.name.startswith("runtime/bin/")
                    info.external_attr = (stat.S_IFREG | (0o755 if executable else 0o644)) << 16
                    info.compress_type = zipfile.ZIP_DEFLATED
                    with archive.open(info, "w") as destination:
                        if entry.name == "meta.json":
                            destination.write(json_bytes(report["metadata"]))
                        else:
                            with file_opener(entry) as source:
                                shutil.copyfileobj(source, destination, CHUNK)
            target.flush()
            os.fsync(target.fileno())
        except BaseException:
            target.close()
            output.unlink(missing_ok=True)
            raise
    try:
        finished = validate(output)
    except BaseException:
        output.unlink(missing_ok=True)
        raise
    finished.update(output=str(output), sha256=sha256(output))
    return finished


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(CHUNK), b""):
            digest.update(block)
    return digest.hexdigest()


def new_project(destination, identity, name, version="0.1.0", template="shell", cores=None, launcher="run.sh"):
    meta = dict(id=identity, name=name, version=version, architecture="mipsel-linux-musl",
                author="Jos\u00e9 Pilas", license="MPL-2.0")
    if cores:
        meta["open_core"] = cores
    meta = metadata(json_bytes(meta))
    require(template in ("shell", "native-c", "native-cpp", "sdl2-c"), "Unknown template")
    component(launcher, hidden=True)
    require(is_root_launcher(launcher) and PurePosixPath(launcher).stem.strip() not in ("", ".", ".."),
            "Launcher needs a safe filename before .sh (case-insensitive)")
    destination.mkdir(parents=True, exist_ok=False)
    payload = destination / "payload"
    payload.mkdir()
    (payload / "meta.json").write_bytes(json_bytes(meta))
    source = HERE / "templates" / ("shell" if template == "shell" else "sdl2" if template == "sdl2-c" else "native")
    shutil.copyfile(source / "run.sh", payload / launcher)
    (payload / launcher).chmod(0o755)
    for name in ("LICENSE", "NOTICE"):
        shutil.copyfile(HERE / name, payload / name)
    (destination / "sdk.json").write_bytes(json_bytes(dict(format=1, template=template)))
    (destination / ".gitignore").write_text("_build/\n*.lilpck\npayload/runtime/bin/\n", encoding="utf-8", newline="\n")
    if template != "shell":
        (destination / "src").mkdir()
        extension = "cpp" if template == "native-cpp" else "c"
        shutil.copyfile(source / ("main." + extension), destination / "src" / ("main." + extension))
        include = destination / "include/lilsmoke"
        include.mkdir(parents=True)
        shutil.copyfile(HERE / "include/lilsmoke/app.h", include / "app.h")
        if template == "sdl2-c":
            shutil.copyfile(HERE / "include/lilsmoke/sdl.h", include / "sdl.h")
            shutil.copyfile(HERE / "include/lilsmoke/input.h", include / "input.h")
            shutil.copyfile(HERE / "include/lilsmoke/lifecycle.h", include / "lifecycle.h")
            shutil.copyfile(HERE / "src/lilsmoke_sdl.c", destination / "src/lilsmoke_sdl.c")
    return dict(created=str(destination), template=template, metadata=meta)


def build(project, sdk, target="mips"):
    if project_spec(project).get("template") == "sdl2-c":
        from graphical import build_graphical
        return build_graphical(project, sdk, target)
    require(target == "mips", "Host preview is available for the sdl2-c template")
    require(os.name == "posix", "Run native builds on Linux/WSL with the existing Linux SDK")
    require(sdk is not None, "Set LILSMOKE_SDK_OUTPUT or pass --sdk; no SDK is downloaded")
    sdk = Path(sdk).resolve()
    project = project.resolve()
    require(not project.is_relative_to(sdk) and not sdk.is_relative_to(project), "Project and SDK must be disjoint")
    spec = project_spec(project)
    template = spec.get("template")
    require(spec.get("format") == 1 and template in ("native-c", "native-cpp"), "Not a native SDK project")
    cpp = template == "native-cpp"
    compiler = sdk / ("host/bin/mipsel-buildroot-linux-musl-" + ("g++" if cpp else "gcc"))
    source = project / "src" / ("main.cpp" if cpp else "main.c")
    regular_file(source, 1024 * 1024)
    regular_file(project / "include/lilsmoke/app.h", 65536)
    require(compiler.is_file(), "Existing SDK compiler missing")
    output = project / "payload/runtime/bin/application"
    require(output.parent.resolve().is_relative_to(project), "Build output escapes project")
    require(not output.exists(), "Build output exists; remove it explicitly to rebuild")
    output.parent.mkdir(parents=True, exist_ok=True)
    flags = ["-std=" + ("c++17" if cpp else "c11"), "-Os", "-Wall", "-Wextra", "-Werror",
             "-mips32r2", "-mtune=74kc", "-mabi=32", "-mfp32", "-mhard-float", "-static",
             "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-s"]
    def bounded_child():
        import resource
        os.nice(10)
        if hasattr(os, "sched_setaffinity"):
            os.sched_setaffinity(0, {min(os.sched_getaffinity(0))})
        resource.setrlimit(resource.RLIMIT_AS, (768 * 1024 * 1024,) * 2)
    work = project / "_build"
    require(work.resolve().is_relative_to(project), "Build log directory escapes project")
    work.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="sdk-native-", dir=work) as temporary:
        target = Path(temporary) / "application"
        with (work / "build.log").open("wb") as log:
            subprocess.run([str(compiler), *flags, "-I" + str(project / "include"), str(source), "-o", str(target)],
                           check=True, stdout=log, stderr=subprocess.STDOUT, timeout=180, preexec_fn=bounded_child)
        require(elf_check(target.read_bytes()[:CHUNK], "application"), "Compiler did not produce an ELF")
        with target.open("rb") as source_stream, output.open("xb") as destination:
            shutil.copyfileobj(source_stream, destination, CHUNK)
        output.chmod(0o755)
    report = dict(built="payload/runtime/bin/application", sha256=sha256(output), flags=flags, cpus=1,
                  sdk_modified=False, static_example=True, source_sha256={
                      str(path.relative_to(project)): sha256(path) for path in
                      (source, project / "include/lilsmoke/app.h")})
    (work / "build-report.json").write_bytes(json_bytes(report))
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", action="version", version=VERSION)
    commands = parser.add_subparsers(dest="command", required=True)
    new = commands.add_parser("new", help="Create a source project with isolated payload")
    new.add_argument("path", type=Path)
    new.add_argument("--id", required=True)
    new.add_argument("--name", required=True)
    new.add_argument("--app-version", default="0.1.0")
    new.add_argument("--template", choices=("shell", "native-c", "native-cpp", "sdl2-c"), default="shell")
    new.add_argument("--open-core", action="append")
    new.add_argument("--launcher", default="run.sh", help="Any safe single root .sh filename; no metadata entrypoint")
    for command in ("validate", "package"):
        item = commands.add_parser(command)
        item.add_argument("path", type=Path)
        item.add_argument("--runtime", type=Path, help="Explicit regular-file tree to merge under runtime/")
        if command == "package":
            item.add_argument("--output", required=True, type=Path)
    native = commands.add_parser("build", help="Serial MIPS build or SDL2 host preview build on Linux/WSL")
    native.add_argument("path", type=Path)
    native.add_argument("--sdk", default=os.environ.get("LILSMOKE_SDK_OUTPUT"))
    native.add_argument("--target", choices=("mips", "host"), default="mips")
    native.add_argument("--host", action="store_true", help="Shorthand for --target host")
    preview = commands.add_parser("run", help="Run the compiled SDL host preview, without packaging a host ELF")
    preview.add_argument("path", type=Path)
    preview.add_argument("extra", nargs=argparse.REMAINDER, help="App arguments after --")
    doctor = commands.add_parser("doctor", help="Read-only host/MIPS prerequisite checks; never installs or downloads")
    doctor.add_argument("--target", choices=("host", "mips", "all"), default="host")
    doctor.add_argument("--template", choices=("sdl2-c", "native-c", "native-cpp"), default="sdl2-c")
    doctor.add_argument("--sdk", type=Path, default=os.environ.get("LILSMOKE_SDK_OUTPUT"))
    doctor.add_argument("--gui", action="store_true", help="Also require existing Xvfb/xdotool GUI QA tools")
    audit = commands.add_parser("check-runtime", aliases=["audit"], help="Read-only target ELF/DT_NEEDED closure audit")
    audit.add_argument("path", type=Path, help="SDK project, payload directory or .lilpck ZIP")
    audit.add_argument("--runtime", type=Path, help="Explicit regular-file tree to merge under runtime/")
    audit.add_argument("--base-root", type=Path, help="Matching OS runtime root (e.g. SDK target/); never copied")
    args = parser.parse_args(argv)
    try:
        if args.command == "doctor":
            from preflight import doctor
            result = doctor(args.target, args.sdk, args.template, args.gui)
        elif args.command in ("check-runtime", "audit"):
            from runtime_audit import check_runtime
            result = check_runtime(args.path, args.runtime, args.base_root)
        elif args.command == "new":
            result = new_project(args.path, args.id, args.name, args.app_version, args.template, args.open_core, args.launcher)
        elif args.command == "validate":
            result = validate(args.path, args.runtime)
        elif args.command == "package":
            result = package(args.path, args.output, args.runtime)
        elif args.command == "build":
            result = build(args.path, args.sdk, "host" if args.host else args.target)
        else:
            from graphical import run_preview
            return run_preview(args.path, args.extra)
        print(json.dumps(result, indent=2, ensure_ascii=True))
        return 0 if result.get("valid", True) else 1
    except (SDKError, OSError, UnicodeError, zipfile.BadZipFile, RuntimeError,
            subprocess.SubprocessError, struct.error) as error:
        result = dict(valid=False, error=str(error))
        if args.command in ("doctor", "check-runtime", "audit"):
            result.update(schema_version=1, command=args.command, sdk_version=VERSION,
                          errors=[dict(code="INVALID_INPUT", path=str(getattr(args, "path", "")),
                                       message=str(error), action="Correct the path/payload or command arguments and retry.")])
        print(json.dumps(result, ensure_ascii=True), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
