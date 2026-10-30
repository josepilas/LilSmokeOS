#!/usr/bin/env python3
"""Build an allowlisted independent development app. Never scan user data."""
import argparse
import json
from pathlib import Path
import re
import struct
import zipfile

PROJECT = Path(__file__).resolve().parents[3]
SOURCE = PROJECT / "package/portsmoke-sm64/src"


def validate_elf(binary, dependencies=("libc.so",), library=False):
    data = binary.read_bytes()
    if len(data) < 52 or data[:7] != b"\x7fELF\x01\x01\x01":
        raise ValueError("runtime must be ELF32 little-endian")
    kind, machine = struct.unpack_from("<HH", data, 16)
    if machine != 8 or kind not in (2, 3):
        raise ValueError("runtime must be a MIPS executable")
    phoff = struct.unpack_from("<I", data, 28)[0]
    phsize, count = struct.unpack_from("<HH", data, 42)
    if phsize != 32 or phoff + phsize * count > len(data):
        raise ValueError("invalid ELF program headers")
    headers = [struct.unpack_from("<8I", data, phoff + i * phsize) for i in range(count)]
    needed, interpreter = [], None
    for kind, offset, _, _, size, _, _, _ in headers:
        if offset + size > len(data):
            raise ValueError("truncated ELF segment")
        if kind == 3:
            interpreter = data[offset:offset + size]
        if kind != 2:
            continue
        if size % 8:
            raise ValueError("invalid ELF dynamic segment")
        tags = list(struct.iter_unpack("<II", data[offset:offset + size]))
        strings = next((value for tag, value in tags if tag == 5), None)
        string_bytes = next((value for tag, value in tags if tag == 10), None)
        for tag, index in tags:
            if tag != 1:
                continue
            if strings is None or string_bytes is None or index >= string_bytes:
                raise ValueError("invalid ELF dependency table")
            segment = next((h for h in headers if h[0] == 1 and
                            h[2] <= strings and strings + string_bytes <= h[2] + h[4]), None)
            if segment is None:
                raise ValueError("unmapped ELF dependency strings")
            start = segment[1] + strings - segment[2] + index
            end = data.find(b"\0", start, start + string_bytes - index)
            if end < 0:
                raise ValueError("unterminated ELF dependency")
            needed.append(data[start:end].decode("ascii"))
    if sorted(needed) != sorted(dependencies) or (not library and interpreter != b"/lib/ld-musl-mipsel.so.1\0"):
        raise ValueError(f"unexpected native runtime dependencies/interpreter: {needed}")
    return data


def build(binary, output, gcc_licenses, source=SOURCE, version=None, launcher="SM64.sh",
          scene=None, sdl_library=None, sdl_license=None):
    if not re.fullmatch(r"[A-Za-z0-9_-]+\.sh", launcher):
        raise ValueError("launcher must be a single safe root .sh filename")
    if scene is None or sdl_library is None or sdl_license is None:
        raise ValueError("native executable and bundled SDL2/license are mandatory")
    # Dereference the SDK SONAME link into regular archive bytes, never ship links.
    sdl_library = sdl_library.resolve(strict=True)
    webp_library = (sdl_library.parent / "libwebp.so.7").resolve(strict=True)
    sharpyuv_library = (sdl_library.parent / "libsharpyuv.so.0").resolve(strict=True)
    inputs = [binary, scene, sdl_library, sdl_license, webp_library, sharpyuv_library,
              *(source / n for n in ("meta.json", "SM64.sh", "README.txt", "LICENSE", "LICENSE.metadata", "icon.webp", "LICENSE.webp")),
              gcc_licenses / "COPYING3", gcc_licenses / "COPYING.RUNTIME"]
    for path in inputs:
        if path.is_symlink() or not path.is_file():
            raise ValueError(f"not a regular package input: {path}")
    meta = json.loads((source / "meta.json").read_text(encoding="utf-8"))
    if version is not None:
        if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
            raise ValueError("release version must be major.minor.patch")
        meta["version"] = version
    runtime = validate_elf(binary)
    scene_data = validate_elf(scene, ("libc.so", "libSDL2-2.0.so.0", "libwebp.so.7"))
    sdl_data = validate_elf(sdl_library, library=True)
    webp_data = validate_elf(webp_library, ("libc.so", "libsharpyuv.so.0"), library=True)
    sharpyuv_data = validate_elf(sharpyuv_library, library=True)
    # Explicit inputs only: ROMs, caches, saves, unrelated sources cannot leak in.
    members = {
        "meta.json": (json.dumps(meta, indent=2) + "\n").encode(),
        launcher: (source / "SM64.sh").read_bytes().replace(b"\r\n", b"\n"),
        "README.txt": (source / "README.txt").read_bytes(),
        "icon.webp": (source / "icon.webp").read_bytes(),
        "runtime/bin/portsmoke-sm64-data": runtime,
        "runtime/bin/sm64-scene-dev": scene_data,
        "runtime/lib/libSDL2-2.0.so.0": sdl_data,
        "runtime/lib/libwebp.so.7": webp_data,
        "runtime/lib/libsharpyuv.so.0": sharpyuv_data,
        "runtime/licenses/tools-MIT.txt": (source / "LICENSE").read_bytes(),
        "runtime/licenses/metadata-CC0.txt": (source / "LICENSE.metadata").read_bytes(),
        "runtime/licenses/decomp-engine-CC0.txt": (source / "LICENSE.metadata").read_bytes(),
        "runtime/licenses/GCC-GPL-3.0.txt": (gcc_licenses / "COPYING3").read_bytes(),
        "runtime/licenses/GCC-runtime-exception.txt": (gcc_licenses / "COPYING.RUNTIME").read_bytes(),
        "runtime/licenses/TinyGL-MIT.txt": (source / "vendor/tinygl/LICENSE").read_bytes(),
        "runtime/licenses/TinyGL-font-public-domain.txt": "\n".join(
            (source / "vendor/tinygl/src/font8x8_basic.h").read_text().splitlines()[:11]).encode() + b"\n",
        "runtime/licenses/SDL2.txt": sdl_license.read_bytes(),
        "runtime/licenses/WebP-BSD.txt": (source / "LICENSE.webp").read_bytes(),
        "original/": b"",
    }
    if output.resolve() in [p.resolve() for p in inputs]:
        raise ValueError("output must not overwrite a source input")
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name, data in members.items():
            info = zipfile.ZipInfo(name, (2026, 10, 10, 0, 0, 0))
            info.create_system = 3
            info.external_attr = (0o40755 if name.endswith("/") else
                                  0o100755 if name == launcher or name.startswith("runtime/bin/") else 0o100644) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--gcc-license-dir", type=Path, required=True)
    parser.add_argument("--scene", type=Path, required=True)
    parser.add_argument("--sdl-library", type=Path, required=True)
    parser.add_argument("--sdl-license", type=Path, required=True)
    parser.add_argument("--source", type=Path, default=SOURCE)
    parser.add_argument("--version")
    parser.add_argument("--launcher", default="SM64.sh")
    args = parser.parse_args()
    print(build(args.binary, args.output, args.gcc_license_dir, args.source, args.version, args.launcher,
                args.scene, args.sdl_library, args.sdl_license))


if __name__ == "__main__":
    main()
