#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Jose Pilas.
"""Actual helper lifecycle, header tests and optional serial MIPS user-mode proof.

Repository-only qualification: generated binaries stay in the requested output;
fixtures/install roots are disposable. No VM, SDK install, SD, or downloads.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

HERE = Path(__file__).resolve().parents[1]
P = HERE.parents[1]
sys.path.insert(0, str(HERE))
import lilsmoke_sdk as sdk


def bounded(address_space=768 * 1024 * 1024):
    import resource
    os.nice(10)
    if hasattr(os, "sched_setaffinity"):
        os.sched_setaffinity(0, {min(os.sched_getaffinity(0))})
    resource.setrlimit(resource.RLIMIT_AS, (address_space,) * 2)


def bounded_user_emulation():
    # Dynamic MIPS PIE needs a roughly 2 GiB guest VA reservation. This is
    # virtual address space, not a resident-memory allocation or a full VM.
    bounded(4 * 1024 * 1024 * 1024)


def check_existing_mips_helper(helper, sdk_output, package, expected_native_hash, run, environment):
    """Exact offline helper, with an explicit recording-shell exec boundary."""
    helper = helper.resolve()
    size = sdk.regular_file(helper)
    with helper.open("rb") as stream:
        assert sdk.elf_check(stream.read(sdk.CHUNK), "existing-mips-helper")
    before = sdk.sha256(helper)
    target = sdk_output.resolve() / "target"
    assert (target / "lib/ld-musl-mipsel.so.1").is_file(), "SDK target musl loader missing"
    prefix = ["qemu-mipsel", "-cpu", "74Kf", "-L", str(target), str(helper)]
    result = dict(passed=False, helper_sha256=before, helper_bytes=size,
                  qemu_prefix=prefix, package_sha256=sdk.sha256(package),
                  native_c_sha256=expected_native_hash, native_example_is_diagnostic=True,
                  recording_shell_fixture=True, full_mips_exec_chain_qualified=False,
                  native_execution_from_helper_qualified=False, physical_ram_128_mib_qualified=False,
                  shell_execution_architecture="Not qualified; QEMU exec may hand off to WSL host /bin/sh",
                  address_space_limit_mib=4096, address_limit_is_virtual_not_resident=True,
                  command_timeout_seconds=180, cpus=1, nice=10,
                  boundary="Exact MIPS helper inspect/install; run/run-core stop at recording shell. "
                           "Generated C/C++ binaries are tested separately via direct MIPS user emulation.",
                  host_binfmt_modified=False, vm=False, sd_access=False)
    try:
        with tempfile.TemporaryDirectory(prefix="lilsmoke-sdk-card-abi-", dir="/tmp") as temporary:
            root = Path(temporary)
            app_root = root / "independent-apps"
            rom_root = root / "rom fixtures"
            env = dict(environment, LILSMOKE_APPS_ROOT=str(app_root), LILSMOKE_ROM_ROOT=str(rom_root),
                       LD_LIBRARY_PATH=str(target / "usr/lib") + ":" + str(target / "lib"),
                       core="stale", rom="stale", LILSMOKE_OPEN_CORE="stale", LILSMOKE_OPEN_ROM="stale")
            for key in ("QEMU_LD_PREFIX", "QEMU_SET_ENV", "QEMU_UNSET_ENV"):
                env.pop(key, None)
            preview = json.loads(run("card-mips-inspect", [*prefix, "inspect", package, root / "preview"],
                                     env, bounded_run=True).splitlines()[-1])
            assert preview["id"] == "org.example.native-c" and preview["relation"] == "new"
            run("card-mips-install", [*prefix, "install", package], env, bounded_run=True)
            app = app_root / "SDK Native"
            executable = app / "runtime/bin/application"
            assert sdk.sha256(executable) == expected_native_hash
            with zipfile.ZipFile(package) as archive:
                launcher_name = sdk.validate(package)["launcher"]
                original_script = archive.read(launcher_name)
            launcher = app / launcher_name
            result["launcher"] = launcher_name
            assert launcher.read_bytes() == original_script
            result["unaltered_native_package_inspect_install_passed"] = True
            # Do not depend on or modify system binfmt. The only fixture change
            # is this disposable installed launcher; the real native ELF is kept.
            try:
                shutil.copyfile(HERE / "tests/recording-app.sh", launcher)
                result["recording_launcher_sha256"] = sdk.sha256(launcher)
                ordinary = run("card-mips-run-recording", [*prefix, "run", "SDK Native"],
                               env, bounded_run=True).splitlines()
                assert ordinary == ["org.example.native-c", "SDK Native", str(app), str(app), "argc=0"], ordinary
                logical = "nested/ABI Name ; '$literal'.gba"
                rom = rom_root / "gba" / logical
                rom.parent.mkdir(parents=True)
                rom.write_bytes(b"SDK ABI fixture, not game data or emulator content")
                opened = run("card-mips-run-core-recording",
                             [*prefix, "run-core", "org.example.native-c", "gba", rom],
                             env, bounded_run=True).splitlines()
                assert opened == ["org.example.native-c", "SDK Native", str(app), str(app),
                                  "argc=2", "arg=core=gba", "arg=rom=" + logical,
                                  "gba", logical, "gba", logical], opened
                run("card-mips-reject-core", [*prefix, "run-core", "org.example.native-c", "snes", rom],
                    env, expected=1, bounded_run=True)
                assert sdk.sha256(executable) == expected_native_hash
                result.update(run_argv_env_passed=True, run_core_argv_env_passed=True,
                              undeclared_core_rejected=True, installed_native_binary_unchanged=True)
            finally:
                launcher.write_bytes(original_script)
                result["original_launcher_restored"] = launcher.read_bytes() == original_script
            result["passed"] = True
    finally:
        result["input_helper_unchanged"] = sdk.sha256(helper) == before and helper.stat().st_size == size
        result["passed"] = result["passed"] and result["input_helper_unchanged"]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--sdk", type=Path, help="Existing SDK output, explicit opt-in")
    parser.add_argument("--existing-mips-helper", type=Path,
                        help="Offline host-staged helper; --sdk required, recording-shell handoff only")
    args = parser.parse_args()
    if args.existing_mips_helper and not args.sdk:
        parser.error("--existing-mips-helper requires --sdk")
    require_posix = os.name == "posix"
    if not require_posix:
        parser.error("Integration compilation runs on Linux/WSL; pure tests are cross-platform")
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=False)
    inputs = [P / "package/lilsmoke-apps/src/lilsmoke-apps.cpp",
              P / "package/lilsmoke-scripts/src/AppsProtocol.h",
              P / "package/lilsmoke-scripts/src/EmulatorSystems.h",
              P / "package/lilsmoke-ui/src/apps/keyboard.cpp",
              P / "docs/lilsmoke-applications-sdk.md"]
    inputs += [p for p in HERE.rglob("*") if p.is_file() and "_build" not in p.relative_to(HERE).parts
               and "__pycache__" not in p.parts]
    before = {str(path.relative_to(P)): sdk.sha256(path) for path in inputs}
    report = dict(passed=False, sdk_version=sdk.VERSION, cpus=1, nice=10, vm=False, sd_access=False,
                  sdk_modified=False, fixture_engine_only=True, commands=[], source_sha256=before,
                  mips_opted_in=args.sdk is not None,
                  existing_mips_helper_opted_in=args.existing_mips_helper is not None)
    environment = dict(os.environ)
    for key in ("LD_PRELOAD", "LD_LIBRARY_PATH", "LILSMOKE_HCFB"):
        environment.pop(key, None)

    def run(label, command, env=None, expected=0, compile=False, bounded_run=False):
        report["commands"].append(dict(label=label, argv=[str(v) for v in command]))
        result = subprocess.run(list(map(str, command)), text=True, capture_output=True,
                                timeout=180, env=env or environment,
                                preexec_fn=bounded if compile else bounded_user_emulation if bounded_run else None)
        (args.out / (label + ".log")).write_text(result.stdout + result.stderr)
        assert result.returncode == expected, (label, result.returncode, result.stdout[-2000:], result.stderr[-2000:])
        return result.stdout

    try:
        include = P / "package/lilsmoke-scripts/src"
        helper = args.out / "actual-apps-helper"
        run("compile-actual-helper", ["g++", "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror",
            "-I" + str(include), inputs[0], "-larchive", "-ljson-c", "-lsodium", "-o", helper], compile=True)
        probe = args.out / "registry-probe"
        run("compile-registry-probe", ["g++", "-std=c++17", "-O1", "-I" + str(include),
            HERE / "tests/registry_probe.cpp", "-ljson-c", "-o", probe], compile=True)
        assert json.loads(run("actual-registry", [probe])) == sdk.SYSTEMS
        report["actual_registry_matches_sdk"] = True
        for language, compiler in (("c", "gcc"), ("cpp", "g++")):
            executable = args.out / ("header-" + language)
            run("compile-header-" + language, [compiler, "-x", "c" if language == "c" else "c++",
                "-std=c11" if language == "c" else "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                "-I" + str(HERE / "include"), HERE / "tests/app_header_test.c", "-o", executable], compile=True)
            run("run-header-" + language, [executable])
        report["host_c_cpp_header_sanitizers_passed"] = True
        with tempfile.TemporaryDirectory(prefix="lilsmoke-sdk-protocol-") as temporary:
            work = Path(temporary)
            app_root = work / "independent-apps"
            rom_root = work / "rom fixtures"
            env = dict(environment, LILSMOKE_APPS_ROOT=str(app_root), LILSMOKE_ROM_ROOT=str(rom_root))
            project = work / "project"
            launcher_name="SDK Studio.SH"
            sdk.new_project(project, "org.example.fixture", "SDK Fixture", cores=["gbadvance", "genesis"],
                            launcher=launcher_name)
            shutil.copyfile(HERE / "tests/recording-app.sh", project / "payload" / launcher_name)
            nested=project/"payload/runtime/helpers"; nested.mkdir(parents=True)
            shutil.copyfile(HERE/"tests/recording-app.sh",nested/"second.sh")
            meta=sdk.parse_json((project/"payload/meta.json").read_bytes())
            meta["entrypoint"]="ignored-metadata.sh"
            (project/"payload/meta.json").write_bytes(sdk.json_bytes(meta))
            package = work / "fixture.lilpck"
            sdk.package(project, package)
            preview = json.loads(run("actual-inspect", [helper, "inspect", package, work / "preview"], env).splitlines()[-1])
            assert preview["id"] == "org.example.fixture" and preview["relation"] == "new"
            run("actual-install", [helper, "install", package], env)
            ordinary = run("actual-run-normal", [helper, "run", "SDK Fixture"], env)
            assert ordinary.splitlines() == ["org.example.fixture", "SDK Fixture",
                str(app_root / "SDK Fixture"), str(app_root / "SDK Fixture"), "argc=0"]
            logical = "nested/Game ; '$literal'.gba"
            rom = rom_root / "gba" / logical
            rom.parent.mkdir(parents=True)
            rom.write_bytes(b"labeled protocol fixture, not game data")
            opened = run("actual-run-core", [helper, "run-core", "org.example.fixture", "gba", rom], env)
            lines = opened.splitlines()
            assert lines[4:7] == ["argc=2", "arg=core=gba", "arg=rom=" + logical], lines
            assert lines[7:] == ["gba", logical, "gba", logical], lines
            systems = run("actual-emulators", [helper, "emulators"], env).split("\t")
            assert systems[:3] == ["org.example.fixture", "SDK Fixture", "gba,md"]
            run("actual-rename", [helper, "rename", "SDK Fixture", "Renamed SDK Fixture"], env)
            saved = app_root / "Renamed SDK Fixture/userdata/keep.txt"
            saved.parent.mkdir()
            saved.write_text("test-only user-created sentinel")
            meta = sdk.parse_json((project / "payload/meta.json").read_bytes())
            meta["version"] = "0.2.0"
            (project / "payload/meta.json").write_bytes(sdk.json_bytes(meta))
            (project / "payload" / launcher_name).rename(project / "payload/Changed Launcher.sh")
            update = work / "update.lilpck"
            sdk.package(project, update)
            run("actual-update", [helper, "install", update], env)
            assert saved.read_text() == "test-only user-created sentinel"
            assert (saved.parents[1] / "Changed Launcher.sh").exists() and not (saved.parents[1] / launcher_name).exists()
            assert sdk.parse_json((saved.parents[1] / "meta.json").read_bytes())["name"] == "Renamed SDK Fixture"
            run("actual-uninstall", [helper, "uninstall", "Renamed SDK Fixture"], env)
            hostile = [
                ("traversal", {"../outside": b"x"}, None),
                ("absolute", {"/outside": b"x"}, None),
                ("two-scripts", {"second.sh": b"#!/bin/sh\n"}, None),
                ("case-collision", {"DATA/x": b"x", "data/X": b"x"}, None),
                ("file-dir", {"thing": b"x", "thing/child": b"x"}, None),
                ("unknown-core", {}, {"open_core": "not-a-system"}),
                ("empty-core", {}, {"open_core": []}),
                ("null-core", {}, {"open_core": None}),
                ("invalid-id", {}, {"id": "unsafe/id"}),
                ("invalid-version", {}, {"version": "1.2.3.4"}),
            ]
            for label, extra, changed in hostile:
                path = work / (label + ".lilpck")
                fields = dict(id="org.example.bad", name="Bad Fixture", version="1")
                fields.update(changed or {})
                with zipfile.ZipFile(path, "w") as archive:
                    archive.writestr("meta.json", sdk.json_bytes(fields))
                    archive.writestr("run.sh", b"#!/bin/sh\nexit 0\n")
                    for key, data in extra.items():
                        archive.writestr(key, data)
                try:
                    sdk.validate(path)
                except sdk.SDKError:
                    pass
                else:
                    raise AssertionError("SDK accepted " + label)
                run("actual-reject-" + label, [helper, "inspect", path, work / "bad-preview"], env, expected=1)
            assert not (work / "outside").exists()
            report.update(actual_helper_lifecycle_passed=True, actual_open_core_argv_env_passed=True,
                          actual_rename_update_userdata_passed=True, hostile_parity_cases=len(hostile),
                          arbitrary_root_launcher_passed=launcher_name, entrypoint_metadata_not_authority=True)
            if args.sdk:
                for template in ("native-c", "native-cpp"):
                    native = work / template
                    sdk.new_project(native, "org.example." + template, "SDK Native", template=template,
                                    cores=["gbadvance"] if args.existing_mips_helper and template == "native-c" else None,
                                    launcher="Native Example.SH")
                    built = sdk.build(native, args.sdk)
                    binary = native / "payload/runtime/bin/application"
                    copy = args.out / (template + ".mipsel")
                    shutil.copyfile(binary, copy)
                    copy.chmod(0o755)
                    example_env = dict(environment, LILSMOKE_APP_ID="org.example." + template,
                                       LILSMOKE_APP_NAME="SDK Native", LILSMOKE_APP_DIR=str(native / "payload"))
                    for key in ("LILSMOKE_OPEN_CORE", "LILSMOKE_OPEN_ROM"):
                        example_env.pop(key, None)
                    output = run("run-" + template + "-mips", ["qemu-mipsel", "-cpu", "74Kf", copy], example_env)
                    assert "Normal application launch; no ROM arguments." in output
                    native_package = work / (template + ".lilpck")
                    sdk.package(native, native_package)
                    run("actual-inspect-" + template, [helper, "inspect", native_package, work / "native-preview"], env)
                    report[template] = built
                    if args.existing_mips_helper and template == "native-c":
                        report["existing_mips_helper"] = check_existing_mips_helper(
                            args.existing_mips_helper, args.sdk, native_package, built["sha256"], run, environment)
                        assert report["existing_mips_helper"]["passed"]
                mips_header = args.out / "header-mips"
                cc = args.sdk / "host/bin/mipsel-buildroot-linux-musl-gcc"
                run("compile-header-mips", [cc, "-std=c11", "-Os", "-static", "-Wall", "-Wextra", "-Werror",
                    "-mips32r2", "-mtune=74kc", "-mabi=32", "-mfp32", "-mhard-float",
                    "-I" + str(HERE / "include"), HERE / "tests/app_header_test.c", "-o", mips_header], compile=True)
                run("run-header-mips", ["qemu-mipsel", "-cpu", "74Kf", mips_header])
                report["mips_static_examples_and_header_passed"] = True
        report.update(passed=True, helper_sha256=sdk.sha256(helper))
    except Exception as error:
        report["error"] = str(error)
        raise
    finally:
        report["sources_unchanged"] = before == {str(path.relative_to(P)): sdk.sha256(path) for path in inputs}
        report["passed"] = report["passed"] and report["sources_unchanged"]
        (args.out / "report.json").write_bytes(sdk.json_bytes(report))
    assert report["passed"]
    print(json.dumps({key: value for key, value in report.items() if key not in ("commands", "source_sha256")}, indent=2))


if __name__ == "__main__":
    main()
