#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Serial production-view idle reuse qualification; no installation or SD access."""
import argparse
import hashlib
import json
import os
import resource
from pathlib import Path
import shlex
import shutil
import subprocess
import time

P = Path(__file__).resolve().parents[2]
S = P / "package/lilsmoke-ui/src/src"


def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def sources():
    result = sorted(p for p in S.iterdir() if p.suffix in (".h", ".cpp"))
    result += sorted((P / "package/lilsmoke-ui/src/apps").glob("*.cpp"))
    for name in ("lilsmoke-scripts/src", "lilsmoke-media/src", "lilsmoke-od-sdl/src"):
        result += sorted((P / "package" / name).glob("*.h"))
    result += [Path(__file__), P / "tools/ui/idle-reuse-test.cpp", P / "tools/ui/build_qualified_motion.sh",
               P / "tools/ui/build_isolated.sh", P / "package/lilsmoke-ui/src/Makefile"]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--sdk", type=Path, default=Path("/root/lilsmoke-os/build/buildroot/output"))
    parser.add_argument("--full-build", action="store_true")
    a = parser.parse_args()
    a.output.mkdir(parents=True, exist_ok=False)
    snapshot = a.output / "source-snapshot"
    before = {str(p.relative_to(P)): digest(p) for p in sources()}
    for relative in before:
        target = snapshot / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(P / relative, target)
    source = snapshot / S.relative_to(P)
    headers = ["-I" + str(snapshot / "package" / p) for p in
               ("lilsmoke-scripts/src", "lilsmoke-media/src", "lilsmoke-od-sdl/src")]
    report = dict(status="running", inputs_before=before, commands=[], results=[],
                  boundary="Software surfaces and mock native presenter, not HC16xx hardware FPS")

    def run(name, argv, timeout=600):
        report["commands"].append(dict(name=name, argv=list(map(str, argv))))
        started = time.monotonic()
        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1")
        def limit_compiler():
            resource.setrlimit(resource.RLIMIT_AS, (768 * 1024**2, 768 * 1024**2))
        with (a.output / (name + ".log")).open("w") as log:
            result = subprocess.run(argv, stdout=log, stderr=subprocess.STDOUT, timeout=timeout, env=env,
                preexec_fn=limit_compiler if "compile" in name or "link" in name or name == "full-ui-build" else None)
        report["results"].append(dict(name=name, returncode=result.returncode,
                                      wall_seconds=time.monotonic() - started))
        print(name, result.returncode, flush=True)
        result.check_returncode()

    try:
        for arch in ("host", "mips"):
            cxx = "g++" if arch == "host" else str(a.sdk / "host/bin/mipsel-buildroot-linux-musl-g++")
            flags = ["-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                     "-ffunction-sections", "-fdata-sections", "-I" + str(source), *headers]
            libs = ["-lSDL2_image", "-lSDL2_ttf", "-lSDL2", "-ldl", "-lpng", "-ljson-c", "-lz", "-lwebp"]
            if arch == "host":
                flags += shlex.split(subprocess.check_output(
                    ["pkg-config", "--cflags", "sdl2", "SDL2_image", "SDL2_ttf"], text=True))
                flags += ["-fsanitize=address,undefined", "-fno-pie", "-fno-omit-frame-pointer"]
                linkflags = ["-fsanitize=address,undefined", "-no-pie"]
                prefix = []
            else:
                flags += ["-I" + str(a.sdk / "staging/usr/include"),
                          "-I" + str(a.sdk / "staging/usr/include/SDL2")]
                linkflags = []
                prefix = ["qemu-mipsel", "-cpu", "74Kf", "-L", str(a.sdk / "target")]
            inputs = [snapshot / "tools/ui/idle-reuse-test.cpp"] + [source / (name + ".cpp") for name in
                      ("SystemView", "GridView", "Renderer", "Font", "Theme", "ThemeRender", "Log",
                       "Config", "SettingsView", "ProcessLaunch", "Cursor", "GameHistory", "FavoritesStore")]
            objects = []
            for path in inputs:
                obj = a.output / (arch + "-" + path.stem + ".o")
                run(arch + "-compile-" + path.stem, [cxx, *flags, "-c", str(path), "-o", str(obj)])
                objects.append(str(obj))
            binary = a.output / (arch + "-idle-reuse")
            run(arch + "-link", [cxx, *linkflags, "-Wl,--gc-sections", "-Wl,--wrap=SDL_GetTicks",
                                *objects, *libs, "-o", str(binary)])
            run(arch + "-test", [*prefix, str(binary), "/root/lilsmoke-vm-validation/storage/themes",
                                "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", str(a.output / (arch + "-fixtures"))])
            report.setdefault("binaries_sha256", {})[binary.name] = digest(binary)
            run(arch + "-main-syntax", [cxx, *flags, "-include",
                str(snapshot / "package/lilsmoke-od-sdl/src/input-wire.h"), "-fsyntax-only", str(source / "main.cpp")])
        if a.full_build:
            run("full-ui-build", ["sh", str(P / "tools/ui/build_qualified_motion.sh"),
                                  str(a.output / "full-build")], timeout=2400)
            report["full_ui_sha256"] = digest(a.output / "full-build/build/lilsmoke-ui")
        report["status"] = "passed"
    except Exception as error:
        report.update(status="failed", error=str(error))
        raise
    finally:
        report["inputs_after"] = {str(p.relative_to(P)): digest(p) for p in sources()}
        report["source_drift"] = before != report["inputs_after"]
        if report["source_drift"]:
            report["status"] = "failed-source-drift"
        (a.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        if report["source_drift"]:
            raise RuntimeError("Sources changed during qualification")
    print("QUALIFIED_IDLE_REUSE_OK " + str(a.output))


if __name__ == "__main__":
    main()
