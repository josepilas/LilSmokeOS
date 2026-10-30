# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Jose Pilas.
"""Bounded ELF inspection, without executing binaries, ldd, or external tools."""
from collections import deque
from pathlib import Path, PurePosixPath
import struct

import lilsmoke_sdk as sdk

PROFILE = "mips32r2-le-o32-fp32-hardfloat-legacy-nan-musl"
REBUILD = "Rebuild with the matching musl SDK: -mips32r2 -mabi=32 -mfp32 -mhard-float (legacy NaN)."
MAX_ELFS = 1024
MAX_EDGES = 20000


class ELFError(ValueError):
    pass


def need(ok, message):
    if not ok:
        raise ELFError(message)


def elf_info(data):
    need(len(data) >= 16 and data[:4] == b"\x7fELF", "Missing/truncated ELF identification")
    bits, encoding = data[4], data[5]
    need(bits in (1, 2) and encoding in (1, 2) and data[6] == 1, "Invalid ELF class/data/version")
    order = "<" if encoding == 1 else ">"
    fmt = order + ("HHIIIIIHHHHHH" if bits == 1 else "HHIQQQIHHHHHH")
    size = 52 if bits == 1 else 64
    need(len(data) >= size, "Truncated ELF header")
    kind, machine, version, _, phoff, _, flags, ehsize, phsize, count, _, _, _ = struct.unpack_from(fmt, data, 16)
    info = dict(elf_class=32 if bits == 1 else 64, endian="little" if encoding == 1 else "big",
                machine=machine, type=kind, flags=hex(flags), interpreter=None, abi_flags=None,
                needed=[], soname=None, rpath=None, runpath=None)
    need(version == 1 and ehsize == size, "Invalid ELF header version/size")
    need(count <= 256 and (not count or phsize == (32 if bits == 1 else 56)), "Invalid/oversized program table")
    need(phoff + phsize * count <= len(data), "Program table outside file")
    segments = []
    for i in range(count):
        if bits == 1:
            tag, offset, address, _, length, memory, _, _ = struct.unpack_from(order + "8I", data, phoff + i * phsize)
        else:
            tag, _, offset, address, _, length, memory, _ = struct.unpack_from(order + "II6Q", data, phoff + i * phsize)
        need(offset + length <= len(data), "Segment outside file")
        if tag == 1:
            need(length <= memory, "PT_LOAD file size exceeds memory size")
        segments.append((tag, offset, address, length))
    for unique in (2, 3, 0x70000003):
        need(sum(s[0] == unique for s in segments) <= 1, "Duplicate dynamic/interpreter/ABI segment")
    for tag, offset, _, length in segments:
        if tag == 3:
            raw = data[offset:offset + length]
            need(1 < length <= 128 and raw[-1:] == b"\0" and b"\0" not in raw[:-1], "Invalid interpreter string")
            info["interpreter"] = raw[:-1].decode("ascii")
        if tag == 0x70000003 and machine == 8:
            need(length == 24, "Invalid MIPS ABI flags size")
            values = struct.unpack_from(order + "H6B4I", data, offset)
            info["abi_flags"] = dict(zip(("version", "isa_level", "isa_rev", "gpr_size", "cpr1_size",
                                           "cpr2_size", "fp_abi", "isa_ext", "ases", "flags1", "flags2"), values))
    dynamic = [s for s in segments if s[0] == 2]
    if not dynamic:
        return info
    _, offset, _, length = dynamic[0]
    stride = 8 if bits == 1 else 16
    need(length > 0 and length % stride == 0 and length // stride <= 8192, "Invalid/oversized dynamic table")
    tags = {}
    terminated = False
    for pos in range(offset, offset + length, stride):
        tag, value = struct.unpack_from(order + ("iI" if bits == 1 else "qQ"), data, pos)
        if tag == 0:
            terminated = True
            break
        tags.setdefault(tag, []).append(value)
    need(terminated, "Unterminated dynamic table")
    string_tags = (1, 14, 15, 29)
    if not any(tag in tags for tag in string_tags):
        return info
    need(len(tags.get(5, [])) == len(tags.get(10, [])) == 1, "Missing/duplicate DT_STRTAB or DT_STRSZ")
    address, length = tags[5][0], tags[10][0]
    need(0 < length <= 1024 * 1024, "Invalid/oversized dynamic string table")
    locations = [off + address - va for tag, off, va, n in segments
                 if tag == 1 and va <= address and address + length <= va + n]
    need(len(locations) == 1, "Dynamic strings not uniquely mapped by a file-backed PT_LOAD")
    strings = data[locations[0]:locations[0] + length]

    def string(index):
        need(index < len(strings), "Dynamic string index outside DT_STRSZ")
        end = strings.find(b"\0", index)
        need(end >= 0 and end - index <= 4096, "Unterminated/oversized dynamic string")
        value = strings[index:end].decode("utf-8")
        need(value and not any(ord(c) < 32 or ord(c) == 127 for c in value), "Empty/control character dynamic string")
        return value

    info["needed"] = list(dict.fromkeys(string(i) for i in tags.get(1, [])))
    for tag, key in ((14, "soname"), (15, "rpath"), (29, "runpath")):
        need(len(tags.get(tag, [])) <= 1, "Duplicate dynamic string tag")
        if tag in tags:
            info[key] = string(tags[tag][0])
    return info


def identity_errors(info):
    errors = []
    for ok, code, message in (
        (info["machine"] == 8, "ELF_MACHINE", "Expected MIPS e_machine=8"),
        (info["elf_class"] == 32, "ELF_CLASS", "Expected ELF32"),
        (info["endian"] == "little", "ELF_ENDIAN", "Expected little-endian ELF"),
        (info["type"] in (2, 3), "ELF_TYPE", "Expected ET_EXEC or ET_DYN"),
    ):
        if not ok:
            errors.append((code, message))
    flags = int(info["flags"], 16)
    if flags & 0xF0000000 != 0x70000000 or flags & 0xF000 != 0x1000 or flags & (0x20 | 0x200 | 0x400):
        errors.append(("ELF_ABI", "Expected MIPS32r2 O32 FP32 legacy-NaN e_flags"))
    abi = info["abi_flags"]
    if abi is None:
        errors.append(("ELF_ABI_UNVERIFIED", "PT_MIPS_ABIFLAGS missing; hard-float register ABI cannot be verified"))
    elif (abi["version"], abi["isa_level"], abi["isa_rev"], abi["gpr_size"], abi["cpr1_size"],
          abi["cpr2_size"], abi["fp_abi"], abi["isa_ext"]) != (0, 32, 2, 1, 1, 0, 1, 0):
        errors.append(("ELF_FLOAT_ABI", "Expected hard-float double-precision ABI with 32-bit FP registers"))
    if info["interpreter"] not in (None, "/lib/ld-musl-mipsel.so.1"):
        errors.append(("ELF_INTERPRETER", "Expected /lib/ld-musl-mipsel.so.1, not glibc/another loader"))
    return errors


def base_file(root, relative):
    """Resolve image-root symlinks as the target would, never through host /lib."""
    parts = deque(PurePosixPath(relative).parts)
    resolved, links = [], 0
    while parts:
        part = parts.popleft()
        if part in ("", ".", "/"):
            continue
        if part == "..":
            need(bool(resolved), "Base symlink escapes runtime root")
            resolved.pop()
            continue
        candidate = root.joinpath(*resolved, part)
        if candidate.is_symlink():
            links += 1
            need(links <= 32, "Base symlink loop/limit")
            target = candidate.readlink().as_posix()
            if target.startswith("/"):
                resolved = []
            parts.extendleft(reversed(PurePosixPath(target).parts))
        else:
            resolved.append(part)
    path = root.joinpath(*resolved)
    need(path.resolve().is_relative_to(root), "Base path escapes runtime root")
    return path


def check_runtime(path, runtime=None, base_root=None):
    report = dict(schema_version=1, command="check-runtime", sdk_version=sdk.VERSION,
                  profile=PROFILE, valid=False, errors=[], warnings=[], elf_files=[], dependencies=[],
                  base_root=str(base_root) if base_root else None, executed_binaries=False,
                  qualification="ELF identity and DT_NEEDED closure only; not symbol, dlopen, script, board or VM qualification")

    def issue(code, name, message, action=REBUILD, warning=False):
        report["warnings" if warning else "errors"].append(dict(code=code, path=name, message=message, action=action))

    if base_root is not None:
        base_root = base_root.resolve()
        sdk.require(base_root.is_dir(), "--base-root must be an existing matching OS runtime root")
    infos, bundled, queue = {}, {}, deque()
    loaded_base = set()
    inspected_bytes = 0
    edge_limit = False

    def inspect(name, data, origin):
        nonlocal inspected_bytes
        inspected_bytes += len(data)
        if len(infos) >= MAX_ELFS or inspected_bytes > sdk.MAX_TOTAL:
            issue("DEPENDENCY_LIMIT", name, "ELF graph exceeds 1024 files or 256 MiB", "Reduce the dependency graph.")
            return None
        try:
            info = elf_info(data)
            info.update(path=name, origin=origin)
            infos[name] = info
            report["elf_files"].append(info)
            for code, message in identity_errors(info):
                issue(code, name, message)
            if info["rpath"] or info["runpath"]:
                issue("ELF_SEARCH_PATH", name, "RPATH/RUNPATH is outside this fixed-search audit profile",
                      "Remove embedded search paths; use runtime/lib and the OS launcher library path.")
            queue.append(name)
            return info
        except (ELFError, UnicodeError, struct.error) as error:
            issue("ELF_MALFORMED", name, str(error))
            return None

    with sdk.open_payload(path, runtime) as (entries, opener):
        structure = sdk.inspect_entries(entries, opener, lambda prefix, name: prefix.startswith(b"\x7fELF"))
        report["launcher"] = structure["launcher"]
        for entry in sorted(entries, key=lambda e: e.name):
            if entry.directory:
                continue
            with opener(entry) as stream:
                prefix = stream.read(4)
                if prefix != b"\x7fELF":
                    if entry.name == "runtime/bin/application" and path.is_dir() and sdk.project_spec(path).get("template") in ("native-c", "native-cpp", "sdl2-c"):
                        issue("NATIVE_NOT_ELF", entry.name, "Native SDK project application is not an ELF")
                    if entry.name.startswith("runtime/lib/") and ".so" in PurePosixPath(entry.name).name:
                        issue("LIBRARY_NOT_ELF", entry.name, "Packaged shared library is not an ELF file")
                    continue
                data = prefix + stream.read(sdk.MAX_FILE)
            info = inspect(entry.name, data, "bundled")
            if PurePosixPath(entry.name).parent == PurePosixPath("runtime/lib"):
                bundled[PurePosixPath(entry.name).name] = entry.name
            if info and info["soname"] and info["soname"] != PurePosixPath(entry.name).name:
                issue("SONAME_FILENAME", entry.name, "DT_SONAME differs from packaged filename",
                      "Package a regular copy under the exact DT_NEEDED/SONAME filename; symlinks are not packaged.", warning=True)
    if not infos:
        issue("NO_ELF", str(path), "No target ELF found; shell-only dependencies are not statically audited",
              "Build a native target first, or use validate for a shell-only package.", warning=True)
    visited = set()

    def resolve(requester, dependency, interpreter=False):
        nonlocal edge_limit
        if len(report["dependencies"]) >= MAX_EDGES:
            issue("DEPENDENCY_LIMIT", requester, "Dependency graph exceeds edge budget", "Reduce the dependency graph.")
            edge_limit = True
            return
        edge = dict(requester=requester, needed=dependency, provider=None, origin="missing")
        report["dependencies"].append(edge)
        if not interpreter and ("/" in dependency or "\\" in dependency or dependency in (".", "..")):
            issue("UNSUPPORTED_NEEDED_PATH", requester, "DT_NEEDED contains a path: " + dependency,
                  "Relink against a library SONAME without a path.")
            return
        if not interpreter and dependency in bundled:
            provider = bundled[dependency]
            edge.update(provider=provider, origin="bundled")
            if provider in infos and infos[provider]["type"] != 3:
                issue("DEPENDENCY_NOT_DSO", provider, "DT_NEEDED provider must be ET_DYN")
            return
        if base_root is not None:
            candidates = [dependency.lstrip("/")] if interpreter else [p + dependency for p in ("lib/", "usr/lib/", "usr/local/lib/")]
            for relative in candidates:
                try:
                    source = base_file(base_root, relative)
                    if not source.exists():
                        continue
                    size = sdk.regular_file(source)
                    provider = "base:/" + relative
                    edge.update(provider=provider, origin="base")
                    if provider not in loaded_base:
                        loaded_base.add(provider)
                        if len(infos) >= MAX_ELFS or inspected_bytes + size > sdk.MAX_TOTAL:
                            issue("DEPENDENCY_LIMIT", provider, "ELF graph exceeds 1024 files or 256 MiB", "Reduce the dependency graph.")
                            return
                        inspect(provider, source.read_bytes(), "base")
                    if provider in infos and infos[provider]["type"] != 3:
                        issue("DEPENDENCY_NOT_DSO", provider, "Runtime provider must be ET_DYN")
                    return
                except (ELFError, sdk.SDKError, OSError) as error:
                    issue("BASE_LIBRARY_INVALID", relative, str(error), "Repair the supplied base runtime tree; do not use host libraries.")
                    return
        issue("MISSING_INTERPRETER" if interpreter else "MISSING_DEPENDENCY", requester,
              "No bundled/base provider for " + dependency,
              "Supply --base-root with the matching OS runtime (the absolute ELF interpreter cannot be app-local)." if interpreter else
              "Supply --base-root with the matching OS runtime, or bundle the licensed target DSO under runtime/lib/" + dependency + ".")

    while queue and not edge_limit:
        name = queue.popleft()
        if name in visited:
            continue
        visited.add(name)
        if len(visited) > MAX_ELFS:
            issue("DEPENDENCY_LIMIT", name, "Dependency graph exceeds 1024 ELF files", "Reduce the dependency graph.")
            break
        info = infos[name]
        if info["interpreter"]:
            resolve(name, info["interpreter"], True)
        for dependency in info["needed"]:
            resolve(name, dependency)
            if edge_limit:
                break
    report["inspected_bytes"] = inspected_bytes
    report["valid"] = not report["errors"]
    return report
