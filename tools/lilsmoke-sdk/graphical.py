#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Jose Pilas.
"""Explicit small SDL2 build/runtime profile. No OS adapters or sysroot copies."""
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
import lilsmoke_sdk as sdk


def limited():
    import resource
    os.nice(10)
    if hasattr(os, "sched_setaffinity"):
        os.sched_setaffinity(0, {min(os.sched_getaffinity(0))})
    resource.setrlimit(resource.RLIMIT_AS, (768 * 1024 * 1024,) * 2)


def build_graphical(project, sdk_output, target="mips"):
    sdk.require(os.name == "posix", "SDL builds run on Linux/WSL; host packaging remains cross-platform")
    project = project.resolve()
    sdk.require(target in ("host", "mips"), "Unknown target")
    spec = sdk.project_spec(project)
    sdk.require(spec.get("template") == "sdl2-c", "SDL starter project required")
    sources = [project / "src/main.c", project / "src/lilsmoke_sdl.c"]
    headers = [project / "include/lilsmoke/app.h", project / "include/lilsmoke/sdl.h"]
    if (project / "include/lilsmoke/input.h").exists():
        headers.append(project / "include/lilsmoke/input.h")
    if (project / "include/lilsmoke/lifecycle.h").exists():
        headers.append(project / "include/lilsmoke/lifecycle.h")
    for path in sources + headers:
        sdk.regular_file(path, 1024 * 1024)
    work = project / "_build" / target
    sdk.require(work.resolve().is_relative_to(project), "Build path escapes project")
    work.mkdir(parents=True, exist_ok=True)
    output = work / "application" if target == "host" else project / "payload/runtime/bin/application"
    sdk.require(output.parent.resolve().is_relative_to(project), "Output escapes project")
    sdk.require(not output.exists(), "Output exists; remove it explicitly to rebuild")
    flags = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-ffunction-sections", "-fdata-sections"]
    libs = ["-Wl,--gc-sections"]
    runtime = []
    sdk_root = None
    if target == "host":
        compiler = os.environ.get("CC", "cc")
        flags += shlex.split(subprocess.check_output(["pkg-config", "--cflags", "sdl2"], text=True))
        libs += shlex.split(subprocess.check_output(["pkg-config", "--libs", "sdl2"], text=True))
    else:
        sdk.require(sdk_output is not None, "Set LILSMOKE_SDK_OUTPUT or pass --sdk; no SDK is downloaded")
        sdk_root = Path(sdk_output).resolve()
        sdk.require(not project.is_relative_to(sdk_root) and not sdk_root.is_relative_to(project),
                    "Project and SDK must be disjoint")
        compiler = str(sdk_root / "host/bin/mipsel-buildroot-linux-musl-gcc")
        flags += ["-mips32r2", "-mtune=74kc", "-mabi=32", "-mfp32", "-mhard-float",
                  "-I" + str(sdk_root / "staging/usr/include/SDL2")]
        libs += ["-lSDL2", "-s"]
    with tempfile.TemporaryDirectory(prefix="sdl-build-", dir=work) as temporary:
        temporary = Path(temporary)
        objects = []
        with (work / "build.log").open("wb") as log:
            for source in sources:
                obj = temporary / (source.stem + ".o")
                subprocess.run([compiler, *flags, "-I" + str(project / "include"), "-c", str(source), "-o", str(obj)],
                               stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180, preexec_fn=limited)
                objects.append(str(obj))
            binary = temporary / "application"
            subprocess.run([compiler, *objects, *libs, "-o", str(binary)], stdout=log,
                           stderr=subprocess.STDOUT, check=True, timeout=180, preexec_fn=limited)
        if target == "mips":
            with binary.open("rb") as stream:
                sdk.require(sdk.elf_check(stream.read(sdk.CHUNK), "application"), "Not target ELF")
            readelf = sdk_root / "host/bin/mipsel-buildroot-linux-musl-readelf"
            needed = re.findall(r"\(NEEDED\).*\[([^]]+)\]",
                                subprocess.check_output([str(readelf), "-d", str(binary)], text=True))
            # This minimal profile has one explicitly licensed runtime dependency.
            sdk.require(set(needed) <= {"libSDL2-2.0.so.0", "libc.so"} and "libSDL2-2.0.so.0" in needed,
                        "Unexpected dependency; this profile bundles only SDL2 and uses OS musl")
            library = (sdk_root / "target/usr/lib/libSDL2-2.0.so.0").resolve()
            sdk.require(library.is_relative_to(sdk_root), "SDL library escaped SDK")
            sdk.regular_file(library)
            with library.open("rb") as stream:
                sdk.require(sdk.elf_check(stream.read(sdk.CHUNK), "SDL2"), "Invalid target SDL2")
            dependencies = re.findall(r"\(NEEDED\).*\[([^]]+)\]",
                                      subprocess.check_output([str(readelf), "-d", str(library)], text=True))
            sdk.require(set(dependencies) <= {"libc.so"}, "SDL2 has extra unbundled dependencies")
            licenses = sorted((sdk_root / "build").glob("sdl2-*/LICENSE.txt"))
            sdk.require(len(licenses) == 1, "Need one unambiguous existing SDL2 license in SDK/build")
            license_path = licenses[0]
            sdk.regular_file(license_path, sdk.MAX_META)
            license_text = license_path.read_text()
            sdk.require("Sam Lantinga" in license_text and "freely" in license_text, "Unexpected SDL2 license")
            for source, relative in [(library, "runtime/lib/libSDL2-2.0.so.0"),
                                     (license_path, "runtime/licenses/SDL2.txt")]:
                destination = project / "payload" / relative
                sdk.require(destination.parent.resolve().is_relative_to(project), "Runtime path escapes project")
                sdk.require(not destination.exists(), "Runtime exists; remove explicitly to rebuild")
                destination.parent.mkdir(parents=True, exist_ok=True)
                with source.open("rb") as incoming, destination.open("xb") as outgoing:
                    shutil.copyfileobj(incoming, outgoing, sdk.CHUNK)
                runtime.append(dict(path=relative, bytes=destination.stat().st_size, sha256=sdk.sha256(destination)))
        output.parent.mkdir(parents=True, exist_ok=True)
        with binary.open("rb") as incoming, output.open("xb") as outgoing:
            shutil.copyfileobj(incoming, outgoing, sdk.CHUNK)
        output.chmod(0o755)
    report = dict(target=target, built=str(output.relative_to(project)), sha256=sdk.sha256(output), cpus=1,
                  nice=10, compiler_address_space_mib=768, dynamic_sdl2=True, software_renderer=True,
                  sdk_modified=False, runtime_files=runtime, source_sha256={
                      str(p.relative_to(project)): sdk.sha256(p) for p in sources + headers})
    (work / "build-report.json").write_bytes(sdk.json_bytes(report))
    return report


def run_preview(project, extra):
    project = project.resolve()
    sdk.require(sdk.project_spec(project).get("template") == "sdl2-c", "SDL preview project required")
    binary = project / "_build/host/application"
    sdk.regular_file(binary)
    meta = sdk.metadata((project / "payload/meta.json").read_bytes())
    env = dict(os.environ, LILSMOKE_APP_DIR=str(binary.parent), HOME=str(binary.parent),
               LILSMOKE_APP_NAME=meta["name"], LILSMOKE_APP_ID=meta["id"])
    # Host preview is not an OS app launch. Never try to load a native MIPS DSO.
    for key in ("LD_PRELOAD", "LILSMOKE_HCFB", "LILSMOKE_OD_NATIVE"):
        env.pop(key, None)
    if extra and extra[0] == "--":
        extra = extra[1:]
    return subprocess.run([str(binary), *extra], cwd=binary.parent, env=env, check=False).returncode
