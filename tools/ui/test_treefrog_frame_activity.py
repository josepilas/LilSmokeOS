#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Serial, isolated host/MIPS qualification; never installs a runtime payload."""
import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import time

P = Path(__file__).resolve().parents[2]
S = P / "package/lilsmoke-ui/src/src"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inputs():
    paths = sorted(p for p in S.iterdir() if p.suffix in (".cpp", ".h"))
    paths += [Path(__file__), P / "tools/ui/treefrog-frame-activity-test.cpp",
              P / "tools/ui/treefrog-frame-activity-renderer-test.cpp",
              P / "package/lilsmoke-od-sdl/src/input-wire.h"]
    return {str(p.relative_to(P)): sha(p) for p in paths}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, required=True,
                        help="Untouched Candidate43 copies of main/Renderer/FramePacer")
    parser.add_argument("--sdk", type=Path, default=Path("/root/lilsmoke-os/build/buildroot/output"))
    a = parser.parse_args()
    if a.output.exists():
        raise ValueError("Use a new output directory to preserve earlier evidence")
    a.output.mkdir(parents=True)
    snapshot = a.output / "source-snapshot"
    snapshot.mkdir()
    before = inputs()
    for relative in before:
        destination = snapshot / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(P / relative, destination)
    source = snapshot / S.relative_to(P)
    test = snapshot / "tools/ui"
    report = dict(status="running", inputs_before=before, commands=[], results=[],
                  scope="Synthetic correctness and profiler CPU cost, not hardware FPS")
    baseline_sha = {
        "main.cpp": "4656f601326676306e31de2be97fbfb88a8b9d3c5d8c56a9e13d2e61bc09aa31",
        "FramePacer.h": "b380d515d5ecb745d6ff59062911ed10f96f934d8f505773148e44c7449ec685",
        "Renderer.cpp": "eb14dbae720554fe3858f1acc343577ea576092ba61edcbca9f264c2a6808469",
        "Renderer.h": "4f9bd2487219c55fdfc1e76ba4af8e92c8de653ab975681fedb335f99d4700ac",
    }

    def run(name, command, env=None, timeout=240):
        report["commands"].append(dict(name=name, argv=[str(x) for x in command]))
        started = time.monotonic()
        with (a.output / (name + ".log")).open("w") as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                                    timeout=timeout, env=env)
        report["results"].append(dict(name=name, returncode=result.returncode,
                                      wall_seconds=time.monotonic() - started))
        print(name, result.returncode, flush=True)
        result.check_returncode()

    try:
        for name, expected in baseline_sha.items():
            if sha(a.baseline / name) != expected:
                raise ValueError("Candidate43 baseline mismatch: " + name)
            if name != "main.cpp" and sha(S / name) != expected:
                raise ValueError("Out-of-scope renderer/pacer drift: " + name)
        diff = "".join(difflib.unified_diff(
            (a.baseline / "main.cpp").read_text().splitlines(keepends=True),
            (source / "main.cpp").read_text().splitlines(keepends=True),
            fromfile="Candidate43/main.cpp", tofile="profile/main.cpp"))
        (a.output / "main-c43.diff").write_text(diff)
        report["candidate43_baseline_sha256"] = baseline_sha
        hostflags = shlex.split(subprocess.check_output(
            ["pkg-config", "--cflags", "sdl2", "SDL2_image", "SDL2_ttf"], text=True))
        env = os.environ.copy()
        env.update(ASAN_OPTIONS="detect_leaks=1:halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1")
        for arch in ("host", "mips"):
            compiler = "g++" if arch == "host" else str(a.sdk / "host/bin/mipsel-buildroot-linux-musl-g++")
            flags = ["-std=c++17", "-O2", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                     "-I" + str(source)]
            flags += (hostflags + ["-fsanitize=address,undefined", "-fno-pie", "-no-pie"]
                      if arch == "host" else ["-I" + str(a.sdk / "staging/usr/include"),
                                              "-I" + str(a.sdk / "staging/usr/include/SDL2")])
            prefix = [] if arch == "host" else ["qemu-mipsel", "-cpu", "74Kf", "-L", str(a.sdk / "target")]
            for fixture in ("test", "renderer-test"):
                binary = a.output / (arch + "-" + fixture)
                command = [compiler, *flags, str(test / ("treefrog-frame-activity-" + fixture + ".cpp"))]
                if fixture == "renderer-test":
                    command += [str(source / name) for name in ("Renderer.cpp", "Font.cpp", "Log.cpp")]
                    command += ["-lSDL2_image", "-lSDL2_ttf", "-lSDL2", "-ldl"]
                command += ["-o", str(binary)]
                run(arch + "-" + fixture + "-build", command)
                run(arch + "-" + fixture, [*prefix, str(binary)], env)
                report.setdefault("binaries_sha256", {})[binary.name] = sha(binary)
            # Compile the real integration unit, not an invented substitute loop.
            syntax = [compiler, *flags, "-fsyntax-only", "-include",
                      str(snapshot / "package/lilsmoke-od-sdl/src/input-wire.h"),
                      "-I" + str(P / "package/lilsmoke-scripts/src"),
                      "-I" + str(P / "package/lilsmoke-media/src"), str(source / "main.cpp")]
            run(arch + "-main-syntax", syntax)
        report["status"] = "passed"
    except Exception as error:
        report.update(status="failed", error=str(error))
        raise
    finally:
        report["inputs_after"] = inputs()
        report["source_drift"] = report["inputs_before"] != report["inputs_after"]
        if report["source_drift"]:
            report["status"] = "failed-source-drift"
        (a.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        if report["source_drift"]:
            raise RuntimeError("Sources changed during qualification")
    print("TREEFROG_FRAME_ACTIVITY_OK output=" + str(a.output))


if __name__ == "__main__":
    main()
