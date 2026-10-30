#!/usr/bin/env python3
"""Private full-engine build. Source/asset-bearing binaries are NEVER release artifacts."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
BUILD = HERE / "_build"
PORT = Path("/root/lilsmoke-sm64-soft")
BASE = Path("/root/lilsmoke-sm64-base")
PORT_PIN = "a173bd0a3eeed9b064a234e528f128424d46ea08"
BASE_PIN = "9921382a68bb0c865e5e45eb594d9c64db59b1af"
SDK = Path("/root/lilsmoke-os/build/buildroot/output")
PLATFORM = ROOT / "package/lilsmoke-sm64-original/src/platform_sdl2.c"


def git(repo, *args):
    return subprocess.check_output(["git", "-C", str(repo), *args], text=True).strip()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--target", choices=("host", "mips"), required=True)
    parser.add_argument("--sanitize-renderer", action="store_true")
    args = parser.parse_args()
    assert not args.sanitize_renderer or args.target == "host"
    assert git(PORT, "rev-parse", "HEAD") == PORT_PIN
    assert git(BASE, "rev-parse", "HEAD") == BASE_PIN
    assert not git(PORT, "status", "--porcelain", "--untracked-files=no")
    rom_digest = hashlib.sha256(args.rom.read_bytes()).hexdigest()
    assert hashlib.sha1(args.rom.read_bytes()).hexdigest() == "9bef1128717f958171a4afac3ed78ee2bb4e86ce"
    BUILD.mkdir(exist_ok=True)
    pointer = BUILD / "private-workspace.json"
    if pointer.exists():
        source = Path(json.loads(pointer.read_text())["source"])
        assert source.parent in (Path("/tmp"), Path("/root")) and source.name.startswith("lilsmoke-sm64-original-private-")
        if not source.is_dir():
            source = Path(tempfile.mkdtemp(prefix="lilsmoke-sm64-original-private-", dir="/root"))
            pointer.write_text(json.dumps({"source": str(source), "private": True}) + "\n")
    else:
        source = Path(tempfile.mkdtemp(prefix="lilsmoke-sm64-original-private-", dir="/root"))
        pointer.write_text(json.dumps({"source": str(source), "private": True}) + "\n")
    marker = source / ".lilsmoke-private-source.json"
    if not marker.exists() or not json.loads(marker.read_text()).get("ready"):
        marker.write_text(json.dumps({"port": PORT_PIN, "private": True, "ready": False}) + "\n")
        archive = BUILD / "source.tar"
        with archive.open("wb") as output:
            subprocess.run(["git", "-C", str(PORT), "archive", PORT_PIN], stdout=output, check=True)
        with tarfile.open(archive) as files:
            files.extractall(source, filter="data")
        archive.unlink()
        marker.write_text(json.dumps({"port": PORT_PIN, "private": True, "ready": True}) + "\n")
    assert json.loads(marker.read_text())["port"] == PORT_PIN
    # A platform-only renderer patch removes the fork's FunKey square crop.
    gfx = source / "src/pc/gfx/gfx_soft.c"
    shutil.copyfile(PORT / "src/pc/gfx/gfx_soft.h", source / "src/pc/gfx/gfx_soft.h")
    subprocess.run(["patch", "-p1", "--fuzz=0", "--batch", "-i",
                    str(HERE / "0005-rgb565-header.patch")], cwd=source, check=True)
    shutil.copyfile(PORT / "src/pc/gfx/gfx_soft.c", gfx)
    subprocess.run(["patch", "-p1", "--fuzz=0", "--batch", "-i",
                    str(HERE / "0001-full-frame-scissor.patch")], cwd=source, check=True)
    subprocess.run(["patch", "-p1", "--fuzz=0", "--batch", "-i",
                    str(HERE / "0003-mips-raster.patch")], cwd=source, check=True)
    subprocess.run(["patch", "-p1", "--fuzz=0", "--batch", "-i",
                    str(HERE / "0006-span-step.patch")], cwd=source, check=True)
    subprocess.run(["patch", "-p1", "--fuzz=0", "--batch", "-i",
                    str(HERE / "0007-depth-first-spans.patch")], cwd=source, check=True)
    subprocess.run(["patch", "-p1", "--fuzz=0", "--batch", "-i",
                    str(HERE / "0008-endpoint-reuse.patch")], cwd=source, check=True)
    subprocess.run(["patch", "-p1", "--fuzz=0", "--batch", "-i",
                    str(HERE / "0009-block-occlusion.patch")], cwd=source, check=True)
    shutil.copyfile(PORT / "src/pc/gfx/gfx_pc.c", source / "src/pc/gfx/gfx_pc.c")
    subprocess.run(["patch", "-p1", "--fuzz=0", "--batch", "-i",
                    str(HERE / "0002-hud-raster-size.patch")], cwd=source, check=True)
    subprocess.run(["patch", "-p1", "--fuzz=0", "--batch", "-i",
                    str(HERE / "0004-sampler-cache.patch")], cwd=source, check=True)
    private_rom = source / "baserom.us.z64"
    if private_rom.exists():
        assert hashlib.sha256(private_rom.read_bytes()).hexdigest() == rom_digest
    else:
        shutil.copyfile(args.rom, private_rom)
    private_rom.chmod(0o400)
    env = dict(os.environ, MAKEFLAGS="-j1", LC_ALL="C", OMP_NUM_THREADS="1")
    compiler = str(SDK / "host/bin/mipsel-buildroot-linux-musl-gcc") if args.target == "mips" else "gcc"
    cc_prefix = str(SDK / "host/bin/mipsel-buildroot-linux-musl-") if args.target == "mips" else ""
    if args.target == "mips":
        sdl_cflags = "-I" + str(SDK / "staging/usr/include/SDL2")
        sdl_libs = "-L" + str(SDK / "staging/usr/lib") + " -lSDL2"
    else:
        sdl_cflags = subprocess.check_output(["pkg-config", "--cflags", "sdl2"], text=True).strip()
        sdl_libs = subprocess.check_output(["pkg-config", "--libs", "sdl2"], text=True).strip()
        # These private generated objects must not retain instrumentation when
        # switching between normal and sanitized host builds.
        for relative in ("lilsmoke-platform.o", "src/pc/gfx/gfx_soft.o", "src/pc/gfx/gfx_pc.o"):
            (source / "build/original-host" / relative).unlink(missing_ok=True)
    flags = f"-O2 -fno-fast-math -fno-strict-aliasing -fwrapv -fcommon -D_LANGUAGE_C -DVERSION_US -DNOWUP -DNON_MATCHING -DAVOID_UB -DTARGET_LINUX -DNO_SEGMENTED_MEMORY -DENABLE_SOFTRAST -DF3DEX_GBI_2E -Iinclude -Isrc -I. -Ibuild/original-{args.target}/include -Ibuild/original-{args.target} -I{ROOT}/package/opendingux-emulators {sdl_cflags}"
    flags += " -DCONVERT"
    command = ["make", "-j1", "-f", "Makefile", "-f", str(HERE / "platform.mk"), "VERSION=us",
               "COMPILER=gcc", "TARGET_FUNKEY=0", "TARGET_OD=0", "ENABLE_OPENGL=0", "ENABLE_OPENGL_LEGACY=0",
               "ENABLE_SOFTRAST=1", "BUILD_DIR=build/original-" + args.target, "CC=" + compiler,
               "CC_CHECK=" + compiler + " -fsyntax-only", "CC_CHECK_CFLAGS=" + flags, "CFLAGS=" + flags,
               "LD=" + compiler, "AS=" + cc_prefix + "as", "OBJCOPY=" + cc_prefix + "objcopy",
               "CXX=" + cc_prefix + "g++", "SDL_CFLAGS=" + sdl_cflags, "LILSMOKE_ROOT=" + str(ROOT),
               "LDFLAGS=-lm -no-pie -lstdc++ -lpthread -ldl -Wl,--wrap=gfx_run,--wrap=create_next_audio_buffer " + sdl_libs +
               (" -fsanitize=address,undefined" if args.sanitize_renderer else ""),
               "LILSMOKE_SANITIZE=" + ("1" if args.sanitize_renderer else "0"), "LILSMOKE_PLATFORM=" + str(PLATFORM)]
    log = BUILD / (args.target + "-build.log")
    print("PRIVATE FULL ENGINE BUILD", args.target, "serial; no GL/GPU; log", log, flush=True)
    with log.open("w") as output:
        # The upstream make exports cross CC to recursive tools builds. Build all
        # host converters first so the MIPS build never creates MIPS host tools.
        subprocess.run(["make", "-j1", "-C", "tools", "CC=gcc", "CXX=g++"], cwd=source, env=env,
                       stdout=output, stderr=subprocess.STDOUT, check=True)
        result = subprocess.run(command, cwd=source, env=env, stdout=output, stderr=subprocess.STDOUT)
    assert hashlib.sha256(args.rom.read_bytes()).hexdigest() == rom_digest, "original ROM changed"
    if result.returncode:
        print("\n".join(log.read_text(errors="replace").splitlines()[-40:]))
        raise SystemExit(result.returncode)
    executable = source / f"build/original-{args.target}/sm64.us.f3dex2e"
    report = {"status": "BUILT", "target": args.target, "basePin": BASE_PIN, "portPin": PORT_PIN,
              "privateSource": str(source), "privateBinary": str(executable), "binarySha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
              "romUnchanged": True, "distributable": False, "rendererBinaryRedistributionAllowed": False,
              "compiler": subprocess.check_output([compiler, "--version"], text=True).splitlines()[0],
              "platformSha256": hashlib.sha256(PLATFORM.read_bytes()).hexdigest(), "command": command}
    (BUILD / (args.target + "-build.json")).write_text(json.dumps(report, indent=2) + "\n")
    print("BUILT PRIVATE", executable, flush=True)


if __name__ == "__main__":
    main()
