#!/usr/bin/env python3
"""Compile private actor preview with the pinned static TinyGL; main builds untouched."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

PROJECT = Path(__file__).resolve().parents[3]
S = PROJECT / "package/portsmoke-sm64/src"
T = Path(__file__).parent
B = T / "_build/mario-worker"
SDK = Path("/root/lilsmoke-os/build/buildroot/output")


def main():
    vendor = S / "vendor/tinygl"
    lock = json.loads((vendor / "sources.lock.json").read_text())
    assert lock["revision"] == "c2e48591a6bfba1a85f1b87a78dcf1abf7dff57a"
    for entry in lock["files"]:
        assert hashlib.sha256((vendor / entry["path"]).read_bytes()).hexdigest() == entry["sha256"]
    for target in ("host", "mips"):
        out = B / ("render-" + target); out.mkdir(parents=True, exist_ok=True)
        copy = out / "tinygl"; shutil.copytree(vendor, copy, dirs_exist_ok=True)
        features = copy / "include/zfeatures.h"; text = features.read_text()
        for key, value in {"TEXTURE_POW2": 6, "DISPLAYLISTS": 0, "ARRAYS": 0, "BLEND": 0,
                           "DIRTY_RECTANGLE": 0, "MULTITHREADED_DRAWPIXELS": 0,
                           "MULTITHREADED_COPY_TEXIMAGE_2D": 0, "MULTITHREADED_ZB_COPYBUFFER": 0}.items():
            text, count = re.subn(r"(?m)^#define TGL_FEATURE_" + key + r" \d+$",
                                 f"#define TGL_FEATURE_{key} {value}", text)
            assert count == 1
        features.write_text(text)
        init = copy / "src/init.c"
        init.write_text(init.read_text().replace("GLint t2 = 1 << 31;", "GLint t2 = INT_MIN;"))
        edges = copy / "src/ztriangle.h"
        edges.write_text(edges.read_text().replace("(dx1 << 16)", "(dx1 * 65536)")
                         .replace("(dx2 << 16)", "(dx2 * 65536)").replace("pr1->x << 16", "pr1->x * 65536"))
        header = copy / "src/zgl.h"
        header.write_text(header.read_text().replace("#define MAX_DISPLAY_LISTS 16384", "#define MAX_DISPLAY_LISTS 64"))
        cc = str(SDK / "host/bin/mipsel-buildroot-linux-musl-gcc") if target == "mips" else "gcc"
        cxx = str(SDK / "host/bin/mipsel-buildroot-linux-musl-g++") if target == "mips" else "g++"
        includes = ["-I" + str(copy / "include"), "-I" + str(copy / "src"), "-I" + str(S)]
        objects = []
        for source in sorted((copy / "src").glob("*.c")):
            obj = out / (source.stem + ".o")
            subprocess.run([cc, "-std=c99", "-O2", "-fno-fast-math", *includes, "-c", str(source), "-o", str(obj)], check=True)
            objects.append(str(obj))
        subprocess.run([cxx, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", *includes,
                        str(T / "mario-preview.cpp"), *(str(S / name) for name in
                        ("mario_actor.cpp", "bob_scene.cpp", "asset_pack.cpp", "runtime_assets.cpp")), *objects,
                        *(["-static-libstdc++", "-static-libgcc"] if target == "mips" else []),
                        "-lm", "-o", str(B / (target + "-preview"))], check=True)
        print(f"BUILT {target} private Mario renderer; static CPU TinyGL, no SDL/system GL")


if __name__ == "__main__":
    main()
