#!/usr/bin/env python3
"""Hash-bound console menu and production runtime staging manifest."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import shutil
import struct
import subprocess
from build import CACHE, DEFAULT_BR, DEFAULT_REPO

ENGINES = ("regba", "fceu320", "atari800", "gnuboy", "prosystem", "oswan")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage-runtime", action="store_true")
    args = parser.parse_args()
    tools = Path(__file__).resolve().parent
    stage = CACHE / "od-console-stage"
    sysroot = DEFAULT_BR / "host/mipsel-buildroot-linux-musl/sysroot"
    readelf = str(DEFAULT_BR / "host/bin/mipsel-linux-readelf")
    sources = json.loads((DEFAULT_REPO / "package/lilsmoke-od-consoles/sources.json").read_text())["engines"]
    menus = json.loads((tools / "qa-menu/results.json").read_text())["engines"]
    browsers = json.loads((tools / "qa-browser/results.json").read_text())["engines"]
    libraries = {}

    def needed(path):
        data = subprocess.check_output([readelf, "-d", str(path)], text=True)
        return re.findall(r"\(NEEDED\).*\[([^\]]+)\]", data)

    def library(name):
        if name in libraries:
            return
        path = next((directory / name for directory in (sysroot / "usr/lib", sysroot / "lib")
                     if (directory / name).is_file()), None)
        if path is None:
            raise RuntimeError("missing production runtime dependency: " + name)
        header = path.read_bytes()[:52]
        if header[:6] != b"\x7fELF\x01\x01" or struct.unpack_from("<H", header, 18)[0] != 8:
            raise RuntimeError("runtime is not little-endian ELF32 MIPS: " + str(path))
        required = needed(path)
        record = dict(source=str(path), sha256=digest(path), bytes=path.stat().st_size, needed=required,
                      stagePath=None, baseRuntime=name == "libc.so", qaOnly=False)
        if args.stage_runtime and name != "libc.so":
            output = stage / "storage/emulators/lib" / name
            output.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, output)
            record["stagePath"] = str(output)
        libraries[name] = record
        for item in required:
            library(item)

    # SDL 1.2 compatibility dynamically loads SDL2, so DT_NEEDED alone is insufficient.
    library("libSDL2-2.0.so.0")
    report = dict(schema=1, scope="six native no-ROM menus; not a gameplay/hardware certification",
                  stageRoot=str(stage), menuIDs=[], engines={}, runtimeLibraries=libraries,
                  productionGraphics="SDK SDL2 DirectFB software backend; NEVER QA dummy SDL2",
                  requiredBaseRuntime=["/lib/ld-musl-mipsel.so.1", "/lib/libc.so",
                                       "SDK DirectFB input/system modules already owned by the base image"],
                  qaRuntimeMustNotBeCopied=str(CACHE / "od-console-qa-runtime"),
                  exclusions=["temper", "picodrive", "stella", "handy", "race", "pokemini", "fba"],
                  archiveHeaderTests=json.loads((CACHE / "od-console-qa/content-tests.json").read_text()),
                  hardwareAudioTested=False, hardwarePerformanceTested=False, hardwareInputTested=False)
    for engine in ENGINES:
        binary = stage / "storage/emulators/opendingux" / engine
        sha = digest(binary)
        required = needed(binary)
        for item in required:
            library(item)
        abi = subprocess.check_output([readelf, "-h", "-A", str(binary)], text=True).split("Primary GOT:")[0]
        abi_ok = all(token in abi for token in ("ELF32", "little endian", "MIPS", "o32", "MIPS32r2",
                                                "CPR1 size: 32", "Hard float (double precision)"))
        assert abi_ok, abi
        evidence = dict(menu=menus[engine], browser=browsers[engine])
        menu_ok = all(item["binary_sha256"] == sha and item["renders_menu"] and
                      item["navigation_changes_pixels"] and item["handles_sdl_quit"]
                      for item in evidence.values()) and browsers[engine]["opens_rom_directory"]
        assets = json.loads((stage / "storage/emulators/share" / engine / "assets-manifest.json").read_text())
        for item in assets["files"]:
            assert digest(Path(assets["directory"]) / item["path"]) == item["sha256"]
        command = ["qemu-mipsel", "-cpu", "74Kf", "-L", str(sysroot), "-E",
                   "LD_LIBRARY_PATH=" + str(stage / "storage/emulators/lib") + ":" + str(sysroot / "usr/lib") + ":" + str(sysroot / "lib"),
                   str(sysroot / "lib/ld-musl-mipsel.so.1"), "--list", str(binary)]
        loaded = subprocess.run(command, capture_output=True, text=True, timeout=10)
        runtime_ok = loaded.returncode == 0 and "Error" not in loaded.stderr
        record = dict(menuID=engine, systems=sources[engine]["systems"], qualifiedSystems=[menus[engine]["system"]],
                      binary=str(binary), deploymentPath="/storage/emulators/opendingux/" + engine,
                      bytes=binary.stat().st_size, sha256=sha, abi=abi, abiPass=abi_ok,
                      directNeeded=required, runtimeLinkPass=runtime_ok,
                      runtimeLinkOutput=loaded.stdout + loaded.stderr,
                      assets=assets, menuQA=evidence, menuQualified=menu_ok,
                      copyCandidate=menu_ok and runtime_ok, gameplayTested=False,
                      HOME="/persist/od-engines/" + engine, workingDirectory="/storage/emulators/share/" + engine,
                      noROMArguments=[], ROMArguments=["{absoluteAlreadyResolvedROM}"],
                      environment=dict(LILSMOKE_OD_ENGINE=engine, LILSMOKE_OD_SYSTEM="{canonicalSystem}",
                                       LILSMOKE_OD_ROM_DIR="{profileROMDirectory}",
                                       LILSMOKE_OD_SHARE_DIR="{workingDirectory}", BIOS_DIR="{userBIOSDirectory}",
                                       LILSMOKE_ROM_RESOLVER="/usr/lib/lilsmoke/lilsmoke-rom"))
        report["engines"][engine] = record
        if record["copyCandidate"]:
            report["menuIDs"].append(engine)
        print(json.dumps(dict(engine=engine, menu=menu_ok, abi=abi_ok, runtime=runtime_ok,
                              dependencies=required)), flush=True)
    report["remainingFailures"] = {
        "all": "No HC16xx audio, physical input, performance, or gameplay verification in this console task.",
        "regba": "GBA gameplay requires user gba_bios.bin; no BIOS is bundled.",
        "atari800": "Atari OS/BASIC/5200 BIOS and real-ROM launch not verified; 5200 system routing remains parent-owned.",
        "fceu320": "FDS BIOS lookup integration is still unverified.",
        "excluded": "Other engines are source-preserved, not qualified or current-contract copy candidates."
    }
    output = tools / "QUALIFICATION.json"
    output.write_text(json.dumps(report, indent=2) + "\n")
    (stage / "QUALIFICATION.json").write_text(json.dumps(report, indent=2) + "\n")
    print(str(output))
    return 0 if len(report["menuIDs"]) == len(ENGINES) else 1


if __name__ == "__main__":
    raise SystemExit(main())
