# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Jose Pilas.
"""Read-only prerequisite probes. No compile outputs, installs or downloads."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

import lilsmoke_sdk as sdk
from runtime_audit import elf_info, identity_errors, ELFError, base_file


def doctor(target="host", sdk_root=None, template="sdl2-c", gui=False):
    report = dict(schema_version=1, command="doctor", sdk_version=sdk.VERSION, valid=False,
                  target=target, template=template, checks=[], errors=[], warnings=[], writes=False,
                  downloads=False, qualification="Prerequisites only; no link/run, display, board or VM qualification")

    def check(code, ok, message, action, evidence=None):
        item = dict(code=code, passed=bool(ok), message=message, action=action)
        if evidence is not None:
            item["evidence"] = evidence
        report["checks"].append(item)
        if not ok:
            report["errors"].append({k: v for k, v in item.items() if k != "passed"})
        return bool(ok)

    def probe(code, argv, action, text=""):
        try:
            from graphical import limited
            result = subprocess.run(list(map(str, argv)), input=text, capture_output=True, text=True,
                                    timeout=20, preexec_fn=limited if os.name == "posix" else None)
            evidence = (result.stdout + result.stderr)[-8192:]
            check(code, result.returncode == 0, "Probe: " + str(argv[0]), action, evidence)
            return result.stdout if result.returncode == 0 else None
        except (OSError, subprocess.SubprocessError) as error:
            check(code, False, str(error), action)
            return None

    def tool(name, executable, action):
        found = shutil.which(str(executable))
        check(name, found is not None, "Executable: " + str(executable), action, found)
        return found

    check("PYTHON_VERSION", sys.version_info >= (3, 10), "Python 3.10+ required", "Use an existing Python 3.10+ interpreter.", sys.version)
    linux = check("BUILD_PLATFORM", sys.platform.startswith("linux"), "Build helpers require Linux/WSL",
                  "Run doctor/build inside Linux or WSL. Windows can still scaffold, validate, audit and package.", sys.platform)
    sdl = template == "sdl2-c"
    if target in ("host", "all"):
        if not sdl:
            check("HOST_TEMPLATE", False, "Host preview is implemented only for sdl2-c", "Select --template sdl2-c or --target mips.")
        cc = tool("HOST_CC", os.environ.get("CC", "cc"), "Provide an existing C compiler via CC (one executable) or PATH.")
        pkg = tool("HOST_PKG_CONFIG", "pkg-config", "Provide existing pkg-config and SDL2 development files (Ubuntu: pkg-config, libsdl2-dev).") if sdl else None
        if linux and cc:
            probe("HOST_CC_RUN", [cc, "--version"], "Repair CC/PATH; the compiler must run on this host.")
        if linux and pkg:
            flags = probe("HOST_SDL2_FLAGS", [pkg, "--cflags", "--libs", "sdl2"],
                          "Provide existing SDL2 headers/link libraries; inspect PKG_CONFIG_PATH and PKG_CONFIG_LIBDIR.")
            if flags is not None and cc:
                try:
                    parsed_flags = shlex.split(flags)
                except ValueError as error:
                    check("HOST_SDL2_FLAGS_PARSE", False, str(error), "Repair malformed SDL2 pkg-config compiler flags.")
                    parsed_flags = None
                # Syntax-only stdin: no executable, object, temporary file or system write.
                if parsed_flags is not None:
                    probe("HOST_SDL2_HEADERS", [cc, *parsed_flags, "-x", "c", "-fsyntax-only", "-"],
                          "Use SDL2 2.0.14+ development headers compatible with CC.",
                          "#include <SDL.h>\n#if !SDL_VERSION_ATLEAST(2,0,14)\n#error SDL2_2_0_14_required\n#endif\nint main(void){return 0;}\n")
            libdir = probe("HOST_SDL2_LIBDIR", [pkg, "--variable=libdir", "sdl2"],
                           "Repair the host SDL2 pkg-config file and matching library directory.")
            if libdir is not None:
                check("HOST_SDL2_LIBRARY", bool(libdir.strip()) and (Path(libdir.strip()) / "libSDL2.so").is_file(),
                      "Host SDL2 link library must exist", "Provide the matching existing SDL2 development/runtime libraries.", libdir.strip())
        if not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
            report["warnings"].append(dict(code="HOST_DISPLAY_UNSET", path="environment",
                message="No DISPLAY/WAYLAND_DISPLAY; headless builds may work but interactive preview needs a display.",
                action="Use a desktop/WSLg display, or SDL_VIDEODRIVER=dummy for headless tests."))
    if gui:
        for executable in ("Xvfb", "xdotool"):
            tool("GUI_" + executable.upper(), executable, "Provide existing " + executable + " for optional GUI QA; no package is installed by doctor.")
    if target in ("mips", "all"):
        if not check("SDK_ROOT", sdk_root is not None and Path(sdk_root).is_dir(),
                     "Existing Buildroot output root required", "Pass --sdk OUTPUT or set LILSMOKE_SDK_OUTPUT; no SDK is bundled/downloaded."):
            report["valid"] = False
            return report
        root = Path(sdk_root).resolve()
        report["sdk_root"] = str(root)
        compilers = ["gcc", "g++"] if target == "all" or template == "native-cpp" else ["gcc"]
        flags = ["-mips32r2", "-mabi=32", "-mfp32", "-mhard-float"]
        for name in (*compilers, "readelf"):
            executable = root / ("host/bin/mipsel-buildroot-linux-musl-" + name)
            found = tool("MIPS_" + name.upper(), executable, "Supply an intact matching Linux-hosted musl cross SDK, including " + name + ".")
            if not linux or not found:
                continue
            if name == "readelf":
                probe("MIPS_READELF_RUN", [found, "--version"], "Repair this SDK's binutils host prerequisites.")
                continue
            triple = probe("MIPS_TRIPLE_" + name, [found, "-dumpmachine"], "Repair the matching compiler and its host dependencies.")
            if triple is not None:
                check("MIPS_TARGET_" + name, triple.strip().startswith("mipsel") and "musl" in triple,
                      "Compiler must target little-endian MIPS musl", "Do not use ARM, glibc or host compilers.", triple.strip())
            sysroot = probe("MIPS_SYSROOT_" + name, [found, "-print-sysroot"], "Supply an intact relocated cross SDK.")
            if sysroot is not None:
                check("MIPS_SYSROOT_EXISTS_" + name, Path(sysroot.strip()).is_dir() if sysroot.strip() else False,
                      "Compiler sysroot must exist", "Restore the matching sysroot; do not point at host /usr.", sysroot.strip())
            language = "c++" if name == "g++" else "c"
            code = "#include <stdio.h>\n#include <stdlib.h>\n"
            if name == "g++":
                code += "#include <iostream>\n"
            if sdl:
                code += "#include <SDL.h>\n"
            code += ("#if !defined(__MIPSEL__) || !defined(__mips_hard_float) || __mips_fpr != 32 || __mips_isa_rev != 2\n"
                     "#error incompatible_MIPS_profile\n#endif\nint main(void){return 0;}\n")
            probe("MIPS_HEADERS_" + name, [found, *flags, "-I" + str(root / "staging/usr/include/SDL2"),
                                           "-x", language, "-fsyntax-only", "-"],
                  "Restore matching libc/SDL2 development headers and FP32 hard-float compiler support.", code)
            libraries = ["libc.a", "libgcc.a"]
            if sdl:
                libraries.append("libSDL2.so")
            if name == "g++":
                libraries.append("libstdc++.a")
            for library in libraries:
                located = probe("MIPS_LINK_SEARCH_" + name + "_" + library, [found, "-print-file-name=" + library],
                                "Restore matching SDK link libraries; do not copy host libraries.")
                if located is not None:
                    check("MIPS_LINK_FILE_" + name + "_" + library, Path(located.strip()).is_file(),
                          "Compiler must locate " + library, "Restore this SDK's staging/link libraries.", located.strip())
        for relative in (("lib/ld-musl-mipsel.so.1", "usr/lib/libSDL2-2.0.so.0") if sdl else ("lib/ld-musl-mipsel.so.1",)):
            try:
                library = base_file(root / "target", relative)
                sdk.regular_file(library)
                info = elf_info(library.read_bytes())
                errors = identity_errors(info)
                check("MIPS_RUNTIME_" + relative, not errors, "Target runtime: " + relative,
                      "Supply the matching ELF32 MIPS32r2 O32 hard-float FP32 musl runtime.", dict(elf=info, errors=errors))
            except (OSError, sdk.SDKError, ELFError, UnicodeError) as error:
                check("MIPS_RUNTIME_" + relative, False, str(error), "Restore SDK target/ runtime libraries; no dependency is downloaded.")
        if sdl:
            licenses = list((root / "build").glob("sdl2-*/LICENSE.txt"))
            good = False
            if len(licenses) == 1:
                try:
                    sdk.regular_file(licenses[0], sdk.MAX_META)
                    text = licenses[0].read_text()
                    good = "Sam Lantinga" in text and "freely" in text
                except (OSError, sdk.SDKError, UnicodeError):
                    pass
            check("MIPS_SDL2_LICENSE", good, "One matching existing SDL2 license required",
                  "Restore build/sdl2-*/LICENSE.txt from this SDK's SDL2 source distribution.", list(map(str, licenses)))
    report["valid"] = not report["errors"]
    return report
