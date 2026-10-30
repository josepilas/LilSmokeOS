#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Jose Pilas.
"""Qualify a trusted source ZIP after extraction, without private OS sources.

Linux/WSL only. Uses existing host SDL2 tools; --sdk opts into MIPS builds and
qemu-mipsel user-mode examples. --gui adds private Xvfb keyboard/mouse QA.
"""
import argparse
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import lilsmoke_sdk as sdk
import source_release as release


def colors(path):
    data = path.read_bytes()
    assert data[:2] == b"BM" and struct.unpack_from("<ii", data, 18) == (640, 480)
    offset = struct.unpack_from("<I", data, 10)[0]
    bpp = struct.unpack_from("<H", data, 28)[0]
    assert bpp in (24, 32)
    step = bpp // 8
    count = len({data[i:i+3] for i in range(offset, len(data)-step+1, step)})
    assert count >= 8, "Blank source-release preview capture"
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--sdk", type=Path)
    parser.add_argument("--gui", action="store_true")
    args = parser.parse_args()
    if os.name != "posix":
        parser.error("Host build/preview qualification requires Linux/WSL")
    args.archive, args.out = args.archive.resolve(), args.out.resolve()
    if args.sdk:
        args.sdk = args.sdk.resolve()
        sdk.require(not args.out.is_relative_to(args.sdk) and not args.sdk.is_relative_to(args.out),
                    "Qualification output and SDK must be disjoint")
    verified = release.verify_archive(args.archive)
    args.out.mkdir(parents=True, exist_ok=False)
    report = dict(passed=False, source_archive_sha256=sdk.sha256(args.archive), source=verified,
                  vm=False, card_access=False, sdk_modified=False, commands=[],
                  mips_qualified=False, gui_qualified=False)
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
    for key in ("LD_PRELOAD", "LD_LIBRARY_PATH", "LILSMOKE_HCFB", "LILSMOKE_OD_NATIVE",
                "LILSMOKE_OPEN_CORE", "LILSMOKE_OPEN_ROM", "QEMU_LD_PREFIX"):
        env.pop(key, None)
    root = args.out / "extracted source" / "lilsmoke-sdk"

    def run(label, command, expected=0, environment=None):
        result = subprocess.run(list(map(str, command)), cwd=root, env=environment or env,
                                capture_output=True, text=True, timeout=240)
        (args.out / (label + ".log")).write_text(result.stdout + result.stderr, encoding="utf-8")
        report["commands"].append(dict(label=label, argv=list(map(str, command)), exit=result.returncode))
        assert result.returncode == expected, (label, result.returncode, result.stdout[-1800:], result.stderr[-1800:])
        return result.stdout

    cli = [sys.executable, "-B", root / "lilsmoke_sdk.py"]
    try:
        # verify_archive checked every allowed name, byte budget, CRC and hash.
        with zipfile.ZipFile(args.archive) as archive:
            archive.extractall(root.parent)
        assert not (root.parent / "package").exists()
        assert release.manifest(release.source_files(root)) == (root / "SHA256SUMS").read_bytes()
        run("pure-sdk", [sys.executable, "-B", "tests/test_sdk.py"])
        run("pure-source-release", [sys.executable, "-B", "tests/test_source_release.py"])
        run("pure-runtime-audit", [sys.executable, "-B", "tests/test_runtime_audit.py"])
        doctor = [*cli, "doctor", "--target", "all" if args.sdk else "host"]
        if args.sdk:
            doctor += ["--sdk", args.sdk]
        if args.gui:
            doctor += ["--gui"]
        run("doctor", doctor)
        for language, compiler, standard in (("c", "cc", "c11"), ("c++", "c++", "c++17")):
            binary = args.out / ("headers-" + language.replace("+", "p"))
            run("compile-headers-" + language, [compiler, "-x", language, "-std=" + standard, "-O1", "-g",
                "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                "-I" + str(root / "include"), root / "tests/app_header_test.c", "-o", binary])
            run("run-headers-" + language, [binary])
        report["headers_sanitizers_passed"] = True
        for language, compiler, standard in (("c", "cc", "c11"), ("c++", "c++", "c++17")):
            binary = args.out / ("lifecycle-" + compiler.replace("+", "p"))
            run("compile-lifecycle-" + compiler, [compiler, "-x", language, "-std=" + standard,
                "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-fno-pie", "-no-pie", "-I" + str(root / "include"),
                root / "tests/lifecycle_test.c", "-o", binary])
            run("run-lifecycle-" + compiler, [binary])
        report["lifecycle_sanitizers_passed"] = True
        cflags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "sdl2"], text=True))
        libs = shlex.split(subprocess.check_output(["pkg-config", "--libs", "sdl2"], text=True))
        for name in ("sdl_api_test", "lifecycle_sdl_test"):
            binary = args.out / name
            run("compile-" + name, ["cc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined", "-fno-pie", "-no-pie", *cflags,
                "-I" + str(root / "include"), root / ("tests/" + name + ".c"),
                root / "src/lilsmoke_sdl.c", *libs, "-o", binary])
            run("run-" + name, [binary])
        report["lifecycle_sdl_sanitizers_passed"] = True
        run("compile-lifecycle-example-host", ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", *cflags,
            "-I" + str(root / "include"), root / "examples/lifecycle.c", root / "src/lilsmoke_sdl.c",
            *libs, "-o", args.out / "lifecycle-example-host"])
        run("run-lifecycle-example-host", [args.out / "lifecycle-example-host", "4"],
            environment=dict(env, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy"))
        run("compile-input-example-host", ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", *cflags,
            "-I" + str(root / "include"), root / "examples/input_state.c", root / "src/lilsmoke_sdl.c",
            *libs, "-o", args.out / "input-example-host"])
        shell = args.out / "note project"
        run("new-shell", [*cli, "new", shell, "--id", "org.example.note", "--name", "Note",
                          "--launcher", "A Different Launcher.SH"])
        assert json.loads(run("validate-shell", [*cli, "validate", shell]))["launcher"] == "A Different Launcher.SH"
        run("package-shell", [*cli, "package", shell, "--output", args.out / "Note.lilpck"])
        run("validate-shell-package", [*cli, "validate", args.out / "Note.lilpck"])
        studio = args.out / "studio project"
        run("new-sdl", [*cli, "new", studio, "--template", "sdl2-c", "--id", "org.lilsmoke.sdk.studio",
                        "--name", "SDK Studio", "--app-version", sdk.VERSION, "--launcher", "SDK Studio.sh"])
        run("validate-unbuilt-rejected", [*cli, "validate", studio], expected=1)
        run("build-host", [*cli, "build", studio, "--target", "host"])
        run("validate-host-not-target", [*cli, "validate", studio], expected=1)
        host_env = dict(env, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        for label in ("preview", "reload"):
            run(label, [*cli, "run", studio, "--", "--test-frames", "6", "--test-report",
                        args.out / (label + ".json"), "--test-screenshot", args.out / (label + ".bmp")],
                environment=host_env)
            state = json.loads((args.out / (label + ".json")).read_text())
            assert state["clean_exit"] and state["saves"] == 1
            assert 1 <= state["frame_ms"] <= 250 and state["average_ms"] >= 30
            assert not state["exit_reason"] and not state["clock_resets"]
            assert state["loaded"] == (1 if label == "reload" else 0)
            report[label + "_colors"] = colors(args.out / (label + ".bmp"))
        assert not (studio / "payload/runtime/bin/application").exists()
        if args.gui:
            gui = args.out / "gui project"
            run("new-gui", [*cli, "new", gui, "--template", "sdl2-c", "--id", "org.example.gui",
                            "--name", "SDK Studio", "--launcher", "GUI Launcher.SH"])
            run("build-gui", [*cli, "build", gui, "--target", "host"])
            run("gui-qa", [sys.executable, "-B", root / "tests/test_studio.py", "--project", gui,
                           "--out", args.out / "gui-qa"])
            report["gui_qualified"] = json.loads((args.out / "gui-qa/report.json").read_text())["passed"]
        if args.sdk:
            run("build-mips-sdl", [*cli, "build", studio, "--target", "mips", "--sdk", args.sdk])
            run("validate-mips-sdl", [*cli, "validate", studio])
            run("audit-project", [*cli, "check-runtime", studio, "--base-root", args.sdk / "target"])
            first, second = args.out / "SDK-Studio.lilpck", args.out / "SDK-Studio-repeat.lilpck"
            run("package-mips-sdl", [*cli, "package", studio, "--output", first])
            run("package-repeat", [*cli, "package", studio, "--output", second])
            run("validate-mips-package", [*cli, "validate", first])
            run("audit-package", [*cli, "check-runtime", first, "--base-root", args.sdk / "target"])
            run("audit-no-base-rejected", [*cli, "check-runtime", first], expected=1)
            assert sdk.sha256(first) == sdk.sha256(second)
            report.update(package_sha256=sdk.sha256(first),
                          engine_sha256=sdk.sha256(studio / "payload/runtime/bin/application"))
            with zipfile.ZipFile(first) as archive:
                assert archive.read("runtime/licenses/SDL2.txt")
                assert "SDK Studio.sh" in archive.namelist()
                assert not any(name.startswith(("_build/", "src/", "include/")) for name in archive.namelist())
            for template in ("native-c", "native-cpp"):
                native = args.out / template
                run("new-" + template, [*cli, "new", native, "--template", template,
                                       "--id", "org.example." + template, "--name", "Native"])
                run("build-" + template, [*cli, "build", native, "--target", "mips", "--sdk", args.sdk])
                native_env = dict(env, LILSMOKE_APP_ID="org.example." + template,
                                  LILSMOKE_APP_NAME="Native", LILSMOKE_APP_DIR=str(native / "payload"))
                text = run("run-" + template, ["qemu-mipsel", "-cpu", "74Kf",
                           native / "payload/runtime/bin/application"], environment=native_env)
                assert "Normal application launch; no ROM arguments." in text
                run("package-" + template, [*cli, "package", native, "--output", args.out / (template + ".lilpck")])
            report["mips_qualified"] = True
            cross = args.sdk / "host/bin/mipsel-buildroot-linux-musl-gcc"
            crossflags = ["-Os", "-Wall", "-Wextra", "-Werror", "-mips32r2",
                          "-mabi=32", "-mfp32", "-mhard-float", "-s", "-I" + str(root / "include")]
            for compiler, language, standard in (("gcc", "c", "c11"), ("g++", "c++", "c++17")):
                binary = args.out / ("lifecycle-mips-" + compiler)
                run("compile-lifecycle-mips-" + compiler,
                    [args.sdk / ("host/bin/mipsel-buildroot-linux-musl-" + compiler), *crossflags,
                     "-x", language, "-std=" + standard, "-static", root / "tests/lifecycle_test.c", "-o", binary])
                run("run-lifecycle-mips-" + compiler, ["qemu-mipsel", "-cpu", "74Kf", binary])
            for name, source in (("lifecycle-sdl-mips", "tests/lifecycle_sdl_test.c"),
                                 ("lifecycle-example-mips", "examples/lifecycle.c")):
                binary = args.out / name
                run("compile-" + name, [cross, "-std=c11", *crossflags,
                    "-I" + str(args.sdk / "staging/usr/include/SDL2"), root / source,
                    root / "src/lilsmoke_sdl.c", "-lSDL2", "-o", binary])
                # The target SDL uses DirectFB, not host dummy/X11. Compile/link
                # only; physical/DirectFB execution belongs to the parent.
            report["lifecycle_mips_core_executed"] = True
            report["lifecycle_mips_sdl_compile_only"] = True
            run("compile-input-example-mips", [cross, "-std=c11", "-Os", "-Wall", "-Wextra", "-Werror",
                "-mips32r2", "-mabi=32", "-mfp32", "-mhard-float", "-s",
                "-I" + str(args.sdk / "staging/usr/include/SDL2"), "-I" + str(root / "include"),
                root / "examples/input_state.c", root / "src/lilsmoke_sdl.c", "-lSDL2",
                "-o", args.out / "input-example-mips"])
            for language, compiler, standard in (("c", "gcc", "c11"), ("c++", "g++", "c++17")):
                binary = args.out / ("headers-mips-" + compiler)
                run("compile-mips-headers-" + compiler, [args.sdk / ("host/bin/mipsel-buildroot-linux-musl-" + compiler),
                    "-x", language, "-std=" + standard, "-Os", "-Wall", "-Wextra", "-Werror", "-static",
                    "-mips32r2", "-mabi=32", "-mfp32", "-mhard-float", "-I" + str(root / "include"),
                    root / "tests/app_header_test.c", "-o", binary])
                run("run-mips-headers-" + compiler, ["qemu-mipsel", "-cpu", "74Kf", binary])
        report.update(passed=True, extracted_root=str(root), host_payload_separation=True,
                      arbitrary_launcher=True, source_links_verified=True)
    except Exception as error:
        report["error"] = str(error)
        raise
    finally:
        (args.out / "report.json").write_bytes(sdk.json_bytes(report))
    print(sdk.json_bytes({key: value for key, value in report.items() if key != "commands"}).decode(), end="")


if __name__ == "__main__":
    main()
