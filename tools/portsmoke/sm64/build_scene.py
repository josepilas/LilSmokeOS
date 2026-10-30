#!/usr/bin/env python3
"""Serial build of the static pinned CPU rasterizer and native SDL scene viewer."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

PROJECT = Path(__file__).resolve().parents[3]


def run(command):
    subprocess.run(list(map(str, command)), check=True)


def build(source, out, cc, cxx, sdk=None, sanitize=False, qa=False):
    vendor = source / "vendor/tinygl"
    lock = json.loads((vendor / "sources.lock.json").read_text())
    if lock["revision"] != "c2e48591a6bfba1a85f1b87a78dcf1abf7dff57a" or lock["source_bytes"] > 1024 * 1024:
        raise ValueError("unapproved TinyGL source pin/budget")
    for row in lock["files"]:
        path = vendor / row["path"]
        if path.is_symlink() or hashlib.sha256(path.read_bytes()).hexdigest() != row["sha256"]:
            raise ValueError(f"modified TinyGL source: {path}")
    configured = out / "tinygl-configured"
    configured.mkdir(parents=True, exist_ok=True)
    shutil.copytree(vendor, configured, dirs_exist_ok=True)
    features = configured / "include/zfeatures.h"
    text = features.read_text()
    settings = {"TGL_FEATURE_TEXTURE_POW2": 6, "TGL_FEATURE_DISPLAYLISTS": 0,
                "TGL_FEATURE_ARRAYS": 0, "TGL_FEATURE_BLEND": 0, "TGL_FEATURE_DIRTY_RECTANGLE": 0,
                "TGL_FEATURE_NO_DRAW_COLOR": 1, "TGL_FEATURE_MULTITHREADED_DRAWPIXELS": 0,
                "TGL_FEATURE_MULTITHREADED_COPY_TEXIMAGE_2D": 0,
                "TGL_FEATURE_MULTITHREADED_ZB_COPYBUFFER": 0}
    for key, value in settings.items():
        text, count = re.subn(r"(?m)^#define " + key + r" \d+$", f"#define {key} {value}", text)
        if count != 1:
            raise ValueError(f"unsupported TinyGL configuration: {key}")
    features.write_text(text)
    header = configured / "src/zgl.h"
    text = header.read_text().replace("#define MAX_DISPLAY_LISTS 16384", "#define MAX_DISPLAY_LISTS 64")
    header.write_text(text)
    # The pinned library uses signed left shifts for negative edge slopes.
    # At our fixed 320px viewport these products fit int32; multiplication
    # retains the raster math without C signed-shift UB. Keep originals hashed.
    init = configured / "src/init.c"
    text = init.read_text()
    if text.count("GLint t2 = 1 << 31;") != 1:
        raise ValueError("unexpected TinyGL compatibility-test expression")
    init.write_text(text.replace("GLint t2 = 1 << 31;", "GLint t2 = INT_MIN;"))
    edges = configured / "src/ztriangle.h"
    text = edges.read_text()
    for old, new in [("(dx1 << 16)", "(dx1 * 65536)"), ("(dx2 << 16)", "(dx2 * 65536)"),
                     ("pr1->x << 16", "pr1->x * 65536")]:
        if text.count(old) != 1:
            raise ValueError("unexpected TinyGL fixed-point edge expression")
        text = text.replace(old, new)
    edges.write_text(text)
    raster = configured / "src/ztriangle.c"
    text = raster.read_text()
    prefix, texture_loops = text.split("#define NB_INTERP 8", 1)
    # This revision defines NODRAWTEST but omits it in all eight textured
    # depth/blend variants. Test the raw texel before shading or depth writes.
    for condition in ("if (1 STIPTEST(_a)) {", "if ((zz >= pz[_a]) STIPTEST(_a)) {"):
        if texture_loops.count(condition) != 4:
            raise ValueError("unexpected TinyGL textured cutout variants")
        texture_loops = texture_loops.replace(condition, condition.replace(") {", " NODRAWTEST(TEXTURE_SAMPLE(texture, s, t))) {"))
    raster.write_text(prefix + "#define NB_INTERP 8" + texture_loops)
    include = ["-I" + str(configured / "include"), "-I" + str(configured / "src")]
    flags = ["-O3", "-flto", "-DNDEBUG", "-fno-fast-math", "-pthread", *include]
    if sdk:
        flags += ["-mips32r2", "-mplt", "-mno-shared", "-fno-pie", "-DHAVE_MIPS_DSP"]
    if sanitize:
        flags += ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    objects = []
    fingerprint = hashlib.sha256(json.dumps([cc, flags]).encode())
    for path in sorted(configured.rglob("*")):
        if path.is_file(): fingerprint.update(path.read_bytes())
    signature = fingerprint.hexdigest()
    stamp = out / "objects-config.sha256"
    reuse = stamp.exists() and stamp.read_text().strip() == signature
    for path in sorted((configured / "src").glob("*.c")):
        obj = out / (path.stem + ".o")
        if not reuse or not obj.is_file():
            run([cc, "-std=c99", *flags, "-c", path, "-o", obj])
        objects.append(obj)
    stamp.write_text(signature + "\n")
    if sdk:
        sdl_flags = ["-I" + str(sdk / "staging/usr/include/SDL2")]
        cxx_flags = ["-static-libstdc++", "-static-libgcc", "-no-pie"]
    else:
        sdl_flags = subprocess.check_output(["sdl2-config", "--cflags"], text=True).split()
        cxx_flags = []
    binary = out / ("sm64-scene-qa" if qa else "sm64-scene-dev")
    run([cxx, "-std=c++17", *flags, *sdl_flags, "-Wall", "-Wextra", "-Werror",
         *(["-DSM64_SCENE_TEST"] if qa else []),
         *(source / name for name in ("native_game.cpp", "course_objects.cpp", "sound_effects.cpp", "loading_ui.cpp", "extraction.cpp", "gameplay.cpp", "mario_actor.cpp", "bob_scene.cpp", "asset_pack.cpp", "runtime_assets.cpp")),
         *objects, *cxx_flags, "-lSDL2", "-lwebp", "-lm", "-o", binary])
    if sdk and not sanitize:
        run([sdk / "host/bin/mipsel-buildroot-linux-musl-strip", binary])
    print(f"BUILT {binary}: pinned TinyGL static, 64px textures, no OpenMP/system GL")
    return binary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=PROJECT / "package/portsmoke-sm64/src")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--sdk", type=Path)
    parser.add_argument("--cc")
    parser.add_argument("--cxx")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--qa", action="store_true")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    cc = args.cc or (str(args.sdk / "host/bin/mipsel-buildroot-linux-musl-gcc") if args.sdk else "gcc")
    cxx = args.cxx or (str(args.sdk / "host/bin/mipsel-buildroot-linux-musl-g++") if args.sdk else "g++")
    build(args.source, args.out, cc, cxx, args.sdk, args.sanitize, args.qa)


if __name__ == "__main__":
    main()
