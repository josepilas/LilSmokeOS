#!/usr/bin/env python3
"""
Native LilSmokeUI functional smoke test.

This is a host-rendered companion to the mipsel QEMU boot test. It compiles a
small executable against the real LilSmokeUI Renderer/Font/ListView/GridView/
Settings/FileExplorer/ROMScanner sources, drives controller-like inputs, and
saves framebuffer captures. It intentionally uses SDL's software renderer.
"""

from __future__ import annotations

import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WORKSPACE_ROOT = ROOT.parents[2]
WORK = WORKSPACE_ROOT / "work" / "ui-functional-smoke"
MSYS = Path(r"C:/path/to/local-input/msys64")
UCRT = MSYS / "ucrt64"
GXX = UCRT / "bin" / "g++.exe"
SCREEN_DIR = WORK / "screens"
REPORT = WORKSPACE_ROOT / "work" / "ui-functional-smoke-report.md"


def tool_env() -> dict[str, str]:
    env = os.environ.copy()
    env["PATH"] = str(UCRT / "bin") + os.pathsep + str(MSYS / "usr" / "bin") + os.pathsep + env.get("PATH", "")
    return env


def run(cmd: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, cwd=str(ROOT), text=True, capture_output=True, **kwargs)


def write(path: Path, data: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(data, encoding="utf-8", newline="\n")


def prepare_storage() -> Path:
    storage = Path(r"C:\storage")
    persist = Path(r"C:\persist")
    for clean in [
        storage / "roms" / "snes",
        storage / "portsmoke" / "ports" / "diablo",
        storage / "portsmoke" / "engines",
        storage / "explorer-demo",
    ]:
        if clean.exists():
            shutil.rmtree(clean)

    for path in [
        storage / "roms" / "snes",
        storage / "portsmoke" / "engines",
        storage / "portsmoke" / "ports" / "diablo",
        storage / "portsmoke" / "saves" / "diablo",
        storage / "explorer-demo",
        persist / "config",
    ]:
        path.mkdir(parents=True, exist_ok=True)

    rom_7z = os.environ.get("LILSMOKE_SMOKE_ROM_7Z", "")
    if rom_7z and Path(rom_7z).is_file():
        shutil.copy2(rom_7z, storage / "roms" / "snes" / "Super Mario World PT-BR.7z")
    else:
        (storage / "roms" / "snes" / "PortSmoke Smoke Test.sfc").write_bytes(b"SNES")
    (storage / "portsmoke" / "engines" / "devilutionx.peng").write_bytes(b"fake-squashfs-placeholder")
    write(
        storage / "portsmoke" / "ports" / "diablo" / "portsmoke.conf",
        "[PortSmoke]\n"
        "Name=Diablo 1\n"
        "EngineFile=devilutionx.peng\n"
        "Script=Diablo.sh\n"
        "Cover=cover.webp\n",
    )
    write(
        storage / "portsmoke" / "ports" / "diablo" / "Diablo.sh",
        "#!/bin/sh\n"
        "echo Diablo smoke launch\n",
    )
    write(storage / "explorer-demo" / "notes.txt", "LilSmoke Explorer smoke file\n")
    write(
        persist / "config" / "lilsmoke.conf",
        "WIFI_ENABLED=off\n"
        "SSH_ENABLED=off\n"
        "DISPLAY_ROTATION=0\n"
        "SCALING_MODE=integer\n"
        "MASTER_VOLUME=50\n"
        "CPU_GOVERNOR=ondemand\n"
        "CPU_OVERCLOCK=stock\n"
        "SCAN_ROMS_ON_BOOT=on\n"
        "AUTO_SLEEP_MINUTES=0\n"
        "LCD_BRIGHTNESS=80\n",
    )
    return storage


def find_font() -> Path:
    candidates = [
        Path(r"C:\Windows\Fonts\arial.ttf"),
        Path(r"C:\Windows\Fonts\segoeui.ttf"),
        UCRT / "share" / "fonts" / "TTF" / "DejaVuSans.ttf",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    raise SystemExit("No usable TTF font found")


def compile_harness() -> Path:
    if not GXX.exists():
        raise SystemExit(f"g++ not found: {GXX}")

    exe = WORK / "ui_render_smoke.exe"
    obj_dir = WORK / "obj"
    obj_dir.mkdir(parents=True, exist_ok=True)
    src = ROOT / "package" / "lilsmoke-ui" / "src" / "src"
    common = [
        "-std=c++17",
        "-O0",
        "-g0",
        "-Wall",
        "-Wextra",
        "-I",
        str(src),
        "-I",
        str(UCRT / "include"),
    ]
    sources = [
        ROOT / "tools" / "hwtest" / "ui_render_smoke.cpp",
        src / "Renderer.cpp",
        src / "Font.cpp",
        src / "Log.cpp",
        src / "ListView.cpp",
        src / "GridView.cpp",
        src / "SettingsView.cpp",
        src / "FileExplorerView.cpp",
        src / "ROMScanner.cpp",
        src / "Config.cpp",
        src / "Theme.cpp",
    ]
    objects: list[Path] = []
    for source in sources:
        obj = obj_dir / (source.stem + ".o")
        cmd = [str(GXX), *common, "-c", str(source), "-o", str(obj)]
        cp = run(cmd, timeout=90, env=tool_env())
        if cp.returncode != 0:
            raise SystemExit(
                f"compile failed for {source}\nSTDOUT:\n{cp.stdout}\nSTDERR:\n{cp.stderr}"
            )
        objects.append(obj)

    cmd = [
        str(GXX),
        "-L",
        str(UCRT / "lib"),
        "-o",
        str(exe),
        *[str(obj) for obj in objects],
        "-lSDL2_image",
        "-lSDL2_ttf",
        "-lSDL2",
    ]
    cp = run(cmd, timeout=90, env=tool_env())
    if cp.returncode != 0:
        raise SystemExit("link failed\nSTDOUT:\n" + cp.stdout + "\nSTDERR:\n" + cp.stderr)
    return exe


def bmp_nonblank(path: Path) -> tuple[bool, int, int]:
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b"BM":
        return False, 0, 0
    offset = struct.unpack_from("<I", data, 10)[0]
    width = struct.unpack_from("<i", data, 18)[0]
    height = abs(struct.unpack_from("<i", data, 22)[0])
    pixels = data[offset:]
    if not pixels:
        return False, width, height
    first = pixels[:4]
    varied = any(pixels[i : i + 4] != first for i in range(0, min(len(pixels), 4 * 640 * 480), 4))
    return varied, width, height


def write_report(results: list[tuple[str, bool, str]]) -> None:
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    passed = sum(1 for _, ok, _ in results if ok)
    failed = len(results) - passed
    lines = [
        "# LilSmokeUI functional smoke test",
        "",
        f"Project: `{ROOT}`",
        f"Result: {passed} passed, {failed} failed",
        "",
        "| Status | Check | Detail |",
        "|---|---|---|",
    ]
    for name, ok, detail in results:
        lines.append(f"| {'PASS' if ok else 'FAIL'} | {name} | {detail.replace('|', '/')} |")
    lines.append("")
    lines.append(f"Screenshots: `{SCREEN_DIR}`")
    REPORT.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    storage = prepare_storage()
    SCREEN_DIR.mkdir(parents=True, exist_ok=True)
    font = find_font()
    exe = compile_harness()

    env = tool_env()
    env.pop("SDL_VIDEODRIVER", None)
    env["LILSMOKE_UI_WINDOWED"] = "1"
    env["LILSMOKE_UI_HIDDEN"] = "1"
    env["LILSMOKE_UI_SMOKE_STORAGE"] = str(storage).replace("\\", "/")

    cp = run([str(exe), str(SCREEN_DIR), str(font)], timeout=45, env=env)
    results: list[tuple[str, bool, str]] = []
    results.append(("native UI harness exits cleanly", cp.returncode == 0, (cp.stdout + cp.stderr).strip()))

    expected = [
        "01-root.bmp",
        "02-root-explorer-selected.bmp",
        "03-settings.bmp",
        "04-explorer.bmp",
        "05-explorer-menu.bmp",
        "06-portsmoke-grid.bmp",
        "07-root-snes-selected.bmp",
        "08-snes-grid.bmp",
    ]
    for name in expected:
        path = SCREEN_DIR / name
        ok = path.exists() and path.stat().st_size > 1000
        varied = False
        width = height = 0
        if ok:
            varied, width, height = bmp_nonblank(path)
            ok = varied and width == 640 and height == 480
        results.append((f"rendered {name}", ok, f"{path} {width}x{height} varied={varied}"))

    ok_all = all(ok for _, ok, _ in results)
    write_report(results)
    for name, ok, detail in results:
        print(("PASS" if ok else "FAIL") + " - " + name)
        if not ok or name == "native UI harness exits cleanly":
            print(detail)
    print(f"report: {REPORT}")
    print(f"screenshots: {SCREEN_DIR}")
    return 0 if ok_all else 1


if __name__ == "__main__":
    raise SystemExit(main())
