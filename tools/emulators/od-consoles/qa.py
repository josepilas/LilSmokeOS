#!/usr/bin/env python3
"""Headless MIPS SDL menu evidence; not an HC16xx performance certification."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import time

def bitmap(path):
    data = path.read_bytes()
    assert data[:2] == b"BM", "not BMP"
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    pixels = data[offset:]
    size = max(1, bpp // 8)
    colors = len({pixels[i:i + size] for i in range(0, len(pixels), size)})
    return dict(width=width, height=abs(height), bpp=bpp, colors=colors,
                sha256=hashlib.sha256(pixels).hexdigest())

def main():
    p = argparse.ArgumentParser()
    p.add_argument("engines", nargs="+")
    p.add_argument("--mode", choices=("menu", "browser"), default="menu")
    p.add_argument("--system")
    p.add_argument("--export", type=Path)
    p.add_argument("--cache", type=Path, default=Path("/root/lilsmoke-emulator-expansion-20261008"))
    p.add_argument("--buildroot", type=Path, default=Path("/root/lilsmoke-os/build/buildroot/output"))
    a = p.parse_args()
    repo = Path(__file__).resolve().parents[3]
    sysroot = a.buildroot / "host/mipsel-buildroot-linux-musl/sysroot"
    qa_runtime = a.cache / "od-console-qa-runtime/runtime/lib"
    if not (qa_runtime / "libSDL2-2.0.so.0").is_file():
        p.error("build-qa-sdl.py must complete first; SDK SDL has no dummy backend")
    out = a.cache / "od-console-qa" / str(time.time_ns())
    out.mkdir(parents=True)
    shim = out / "qa-sdl12.so"
    subprocess.run([str(a.buildroot / "host/bin/mipsel-linux-gcc"), "-shared", "-fPIC",
                    "-O2", "-mfp32", "-mhard-float", "-I" + str(sysroot / "usr/include/SDL"),
                    str(Path(__file__).with_name("qa-sdl12.c")), "-o", str(shim),
                    "-lSDL", "-ldl"], check=True)
    manifest = json.loads((repo / "package/lilsmoke-od-consoles/sources.json").read_text())
    results = {}
    for engine in a.engines:
        case = out / engine
        case.mkdir()
        home = case / "home"
        roms = case / "roms"
        home.mkdir()
        roms.mkdir()
        (roms / "a-subfolder").mkdir()
        (roms / "b-subfolder").mkdir()
        binary = a.cache / "od-console-stage/storage/emulators/opendingux" / engine
        if not binary.exists():
            binary = a.cache / "od-console-stage/usr/lib/lilsmoke/od-emulators" / engine
        cwd = a.cache / "od-console-stage/storage/emulators/share" / engine
        if not (cwd / "assets-manifest.json").is_file():
            p.error("stage reviewed assets first: " + engine)
        env = os.environ.copy()
        env.update(HOME=str(home), SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
                   PICOFE_SW_ONLY="1", PICOFE_GL="0",
                   SDL_RENDER_DRIVER="software", OD_QA_OUTPUT=str(case),
                   LILSMOKE_OD_SYSTEM=a.system or manifest["engines"][engine]["systems"][0],
                   LILSMOKE_OD_ROM_DIR=str(roms), LILSMOKE_OD_SHARE_DIR=str(cwd))
        if engine == "fceu320":
            # readkey() returns after each event. A held key repeats per loop;
            # uncapped dummy video can wrap the selection back to its origin.
            env["OD_QA_KEYS"] = "3:274:0"
        if engine == "temper":
            env["OD_QA_KEYS"] = "3:275:0,3.3:274:0"
        if a.mode == "browser":
            keys = dict(regba="3:306:0.2", prosystem="3:306:0.2", oswan="3:306:0.2", atari800="3:122:0.2")
            if engine in keys:
                env["OD_QA_KEYS"] = keys[engine]
        command = ["qemu-mipsel", "-cpu", "74Kf", "-L", str(sysroot),
                   "-E", "LD_LIBRARY_PATH=" + str(qa_runtime) + ":" + str(sysroot / "usr/lib") + ":" + str(sysroot / "lib"),
                   "-E", "LD_PRELOAD=" + str(shim), str(binary)]
        with (case / "run.log").open("w") as log:
            try:
                r = subprocess.run(command, cwd=cwd, env=env, stdout=log,
                                   stderr=subprocess.STDOUT, timeout=15)
                result = dict(exit=r.returncode)
            except subprocess.TimeoutExpired:
                result = dict(exit="timeout")
        frames = {}
        for name in ("before", "after"):
            path = case / (name + ".bmp")
            if path.exists():
                frames[name] = bitmap(path)
        result.update(frames=frames, log=str(case / "run.log"), no_rom=True)
        result["system"] = env["LILSMOKE_OD_SYSTEM"]
        result["mode"] = a.mode
        result["opens_rom_directory"] = "OD_QA browse=" + str(roms) in (case / "run.log").read_text()
        result["binary_sha256"] = hashlib.sha256(binary.read_bytes()).hexdigest()
        result["qa_runtime"] = str(qa_runtime)
        result["renders_menu"] = bool(frames and frames.get("before", {}).get("colors", 0) > 2)
        result["navigation_changes_pixels"] = bool(len(frames) == 2 and
            frames["before"]["sha256"] != frames["after"]["sha256"])
        result["handles_sdl_quit"] = result["exit"] == 0
        results[engine] = result
        if a.export:
            from PIL import Image
            destination = a.export / engine
            destination.mkdir(parents=True, exist_ok=True)
            for name in frames:
                Image.open(case / (name + ".bmp")).save(destination / (name + ".png"))
        print(json.dumps({engine: result}), flush=True)
    report = dict(scope="MIPS user-mode, dummy SDL, no ROM, navigation and quit",
                  hardware_audio_tested=False, hardware_performance_tested=False, engines=results)
    (out / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(str(out / "results.json"), flush=True)
    if a.export:
        (a.export / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    return 0 if all(x["renders_menu"] and x["navigation_changes_pixels"] and x["handles_sdl_quit"]
                    and (a.mode != "browser" or x["opens_rom_directory"])
                    for x in results.values()) else 1

if __name__ == "__main__":
    raise SystemExit(main())
