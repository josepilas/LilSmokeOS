#!/usr/bin/env python3
"""Pinned, isolated native-console source builds; never installs into Buildroot."""
import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import shlex
import time

CACHE = Path("/root/lilsmoke-emulator-expansion-20261008")
DEFAULT_BR = Path("/root/lilsmoke-os/build/buildroot/output")
DEFAULT_REPO = Path(__file__).resolve().parents[3]
FLAGS = "-O2 -march=mips32r2 -mabi=32 -mtune=74kc -mdspr2 -mfp32 -mhard-float -mplt -mlong-calls -fno-strict-aliasing -fcommon"


def run(command, directory, environment, logfile):
    logfile.write("\n$ " + shlex.join(command) + "\n")
    logfile.flush()
    subprocess.run(command, cwd=directory, env=environment, stdout=logfile,
                   stderr=subprocess.STDOUT, check=True)


def source_fixes(engine, work, package, log):
    fixes = json.loads((package / "source-fixes.json").read_text()).get(engine, [])
    for index, fix in enumerate(fixes):
        path = work / fix["file"]
        original = path.read_text(encoding="latin1")
        expected = fix["after"]
        for later in fixes[index + 1:]:
            if later["file"] == fix["file"]:
                expected = expected.replace(later["before"], later["after"])
        if expected and expected in original and (expected not in fix["before"] or fix["before"] not in original):
            continue
        if fix["after"] in original and (fix["after"] not in fix["before"] or fix["before"] not in original):
            continue
        if fix.get("marker") and fix["marker"] in original:
            continue
        if fix.get("optional") and fix["before"] not in original:
            continue
        count = fix.get("count", 1)
        if original.count(fix["before"]) != count:
            raise RuntimeError("source patch context mismatch: " + str(path) + " for " + repr(fix["before"]))
        patched = original.replace(fix["before"], fix["after"], count)
        log.write("".join(difflib.unified_diff(original.splitlines(True), patched.splitlines(True),
                                            "a/" + fix["file"], "b/" + fix["file"])))
        path.write_text(patched, encoding="latin1")


def image_dependency(args, package, env, log):
    source = args.cache / "od-console-src/sdl-image12"
    pin = "2ffb2e3e1eba037897164e3ac6c67570d8bccd79"
    if subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip() != pin:
        raise RuntimeError("SDL_image 1.2 source pin mismatch")
    deps = args.cache / "od-console-deps"
    objects = deps / "obj"
    objects.mkdir(parents=True, exist_ok=True)
    sysroot = args.buildroot / "host/mipsel-buildroot-linux-musl/sysroot"
    inputs = [source / "IMG.c"] + sorted(source.glob("IMG_*.c"))
    outputs = []
    for path in inputs:
        output = objects / (path.stem + ".o")
        if not output.exists() or path.stat().st_mtime > output.stat().st_mtime:
            run([env["CC"], *shlex.split(FLAGS), "-DLOAD_PNG", "-DLOAD_BMP",
                 "-I" + str(sysroot / "usr/include/SDL"), "-c", str(path), "-o", str(output)],
                source, env, log)
        outputs.append(str(output))
    library = deps / "usr/lib/libSDL_image.a"
    library.parent.mkdir(parents=True, exist_ok=True)
    run([str(args.buildroot / "host/bin/mipsel-linux-ar"), "rcs", str(library), *outputs], source, env, log)
    include = deps / "usr/include/SDL"
    include.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source / "SDL_image.h", include / "SDL_image.h")


def build(engine, args, manifest):
    entry = manifest["engines"][engine]
    package = args.repo / "package/lilsmoke-od-consoles"
    source = args.cache / "od-console-src" / entry.get("source_directory", engine)
    work = args.cache / "od-console-build" / (engine + "-" + entry["revision"][:12])
    stage = args.cache / "od-console-stage"
    logs = args.cache / "od-console-logs"
    logs.mkdir(parents=True, exist_ok=True)
    sysroot = args.buildroot / "host/mipsel-buildroot-linux-musl/sysroot"
    cc = str(args.buildroot / "host/bin/mipsel-linux-gcc")
    cxx = str(args.buildroot / "host/bin/mipsel-linux-g++")
    env = os.environ.copy()
    env.update(PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
               GIT_TERMINAL_PROMPT="0", LILSMOKE_OD_SYSROOT=str(sysroot),
               CC=cc, CXX=cxx, CFLAGS=FLAGS, CXXFLAGS=FLAGS,
               PKG_CONFIG_SYSROOT_DIR=str(sysroot),
               PKG_CONFIG_LIBDIR=str(sysroot / "usr/lib/pkgconfig"))
    record = dict(engine=engine, revision=entry["revision"], started=time.time(), status="building")
    log_path = logs / (engine + ".log")
    record_path = logs / (engine + ".json")
    with log_path.open("a") as log:
        try:
            if not source.exists():
                source.parent.mkdir(parents=True, exist_ok=True)
                run(["git", "clone", "--no-checkout", entry["repository"], str(source)], args.cache, env, log)
                run(["git", "checkout", "--detach", entry["revision"]], source, env, log)
            head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, env=env, text=True).strip()
            if head != entry["revision"]:
                raise RuntimeError("source pin mismatch: " + head)
            if not work.exists():
                shutil.copytree(source, work, ignore=shutil.ignore_patterns(".git"))
            source_fixes(engine, work, package, log)
            if engine == "regba":
                run([sys.executable,str(Path(__file__).with_name('fix-regba-o32.py')),str(work)],work,env,log)
            # Upstream recipes do not all track external header dependencies.
            for header in (package / "od-content.h", package / "regba-browser.h"):
                for path in work.rglob("*"):
                    if path.suffix in (".c", ".cpp") and header.stat().st_mtime > path.stat().st_mtime:
                        if header.name in path.read_text(encoding="latin1"):
                            os.utime(path, None)
            if engine in ("stella", "atari800", "fceu320", "handy", "race"):
                image_dependency(args, package, env, log)
            native_flags = FLAGS + " -I" + str(package) + " -I" + str(args.cache / "od-console-deps/usr/include")
            cflags = native_flags + " -I" + str(sysroot / "usr/include/SDL") + " -D_GNU_SOURCE -D_REENTRANT"
            libs = "-Wl,--no-as-needed -lSDL -lm -lz -pthread"
            command = ["make", "-j" + str(args.jobs), "CC=" + cc, "CXX=" + cxx,
                       "LD=" + cc, "SDL_CFLAGS=-I" + str(sysroot / "usr/include/SDL"), "SDL_LIBS=-lSDL -pthread"]
            if engine == "picodrive":
                # Generic SDL uses no Ingenic registers, framebuffer ioctls, or GLES.
                sdl_config = package / "sdl12-config.sh"
                sdl_config.chmod(0o755)
                env["SDL_CONFIG"] = str(sdl_config)
                env["CFLAGS"] = native_flags
                run(["git", "submodule", "update", "--init", "--recursive"], source, env, log)
                for relative in ("platform/libpicofe", "cpu/cyclone", "pico/sound/emu2413", "pico/cd/libchdr", "platform/common/dr_libs"):
                    shutil.copytree(source / relative, work / relative, dirs_exist_ok=True,
                                    ignore=shutil.ignore_patterns(".git"))
                source_fixes(engine, work, package, log)
                run(["./configure", "--platform=generic", "--sound-drivers=sdl",
                     "--with-sdl-gles=no", "--with-libavcodec=no", "--with-zlib=yes"], work, env, log)
                command += ["-f", str(package / "Makefile.picodrive"), "picodrive"]
            elif engine == "prosystem":
                command += ["CFLAGS=" + cflags + " -DOPENDINGUX -D_OPENDINGUX_", "LDFLAGS=" + libs,
                            entry["binary"]]
            elif engine == "gnuboy":
                run(["./configure", "--host=mipsel-buildroot-linux-musl", "--with-fb=no",
                     "--with-svgalib=no", "--with-x=no", "--with-sdl=yes", "--with-sdl2=no",
                     "--with-sound=sdl"], work, env, log)
                command += ["CFLAGS=" + cflags + " -DIS_LITTLE_ENDIAN", "sdlgnuboy"]
            else:
                native_make = package / ("Makefile." + engine)
                if not native_make.exists():
                    raise RuntimeError("native build recipe not prepared: " + engine)
                command += ["-f", str(native_make), "OD_FLAGS=" + native_flags, "OD_SYSROOT=" + str(sysroot),
                            "OD_PACKAGE=" + str(package), "OD_DEPS=" + str(args.cache / "od-console-deps")]
            recipe = package / ("Makefile." + engine)
            if recipe.exists():
                digest = hashlib.sha256(recipe.read_bytes()).hexdigest()
                marker = work / ".lilsmoke-recipe.sha256"
                if not marker.exists() or marker.read_text() != digest:
                    # Upstream does not depend on our external makefile. Relink
                    # only this generated executable when its recipe changes.
                    generated = (work / entry["binary"]).resolve()
                    if work.resolve() not in generated.parents:
                        raise RuntimeError("generated target escaped build directory")
                    generated.unlink(missing_ok=True)
                    marker.write_text(digest)
            run(command, work / entry.get("make_directory", "."), env, log)
            artifact = work / entry["binary"]
            if not artifact.is_file():
                raise RuntimeError("upstream target absent: " + str(artifact))
            destination = stage / "storage/emulators/opendingux" / engine
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(artifact, destination)
            destination.chmod(0o755)
            readelf = str(args.buildroot / "host/bin/mipsel-linux-readelf")
            metadata = subprocess.check_output([readelf, "-h", "-A", "-d", str(destination)], env=env, text=True)
            if "ELF32" not in metadata or "MIPS" not in metadata:
                raise RuntimeError("not a MIPS ELF32 executable")
            symbols = subprocess.check_output([readelf, "-Ws", str(destination)], env=env, text=True)
            if " retro_init" in symbols or " retro_run" in symbols:
                raise RuntimeError("libretro entry point in supposed standalone executable")
            log.write(metadata)
            if engine in json.loads((package / "assets.json").read_text())["engines"]:
                run([sys.executable, str(args.repo / "tools/emulators/od-consoles/stage-assets.py"),
                     "--cache", str(args.cache), engine], work, env, log)
            record.update(status="built", artifact=str(destination), bytes=destination.stat().st_size,
                          sha256=hashlib.sha256(destination.read_bytes()).hexdigest(),
                          systems=entry["systems"], frontend=entry["frontend"])
        except (subprocess.CalledProcessError, RuntimeError, OSError) as error:
            record.update(status="failed", error=str(error), log=str(log_path))
        finally:
            record["finished"] = time.time()
            record_path.write_text(json.dumps(record, indent=2) + "\n")
            (logs / (engine + "-" + str(time.time_ns()) + ".json")).write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps(record), flush=True)
    return record["status"] == "built"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("engines", nargs="+")
    parser.add_argument("--repo", type=Path, default=DEFAULT_REPO)
    parser.add_argument("--cache", type=Path, default=CACHE)
    parser.add_argument("--buildroot", type=Path, default=DEFAULT_BR)
    parser.add_argument("--jobs", type=int, default=1)
    args = parser.parse_args()
    if args.cache.resolve() == args.buildroot.resolve() or args.buildroot.resolve() in args.cache.resolve().parents:
        parser.error("cache/staging must be outside Buildroot")
    if args.jobs != 1:
        parser.error("this 8 GiB shared host requires --jobs 1")
    manifest = json.loads((args.repo / "package/lilsmoke-od-consoles/sources.json").read_text())
    result = True
    for engine in args.engines:
        if engine not in manifest["engines"]:
            parser.error("unknown engine: " + engine)
        result = build(engine, args, manifest) and result
    return 0 if result else 1


if __name__ == "__main__":
    sys.exit(main())
