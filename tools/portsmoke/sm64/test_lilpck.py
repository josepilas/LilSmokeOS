#!/usr/bin/env python3
"""Use the real Applications helper read-only binary against disposable app roots."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

from build_lilpck import build, validate_elf


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--helper", type=Path, required=True)
    parser.add_argument("--gcc-license-dir", type=Path, required=True)
    parser.add_argument("--sysroot", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--scene", type=Path, required=True)
    parser.add_argument("--sdl-library", type=Path, required=True)
    parser.add_argument("--sdl-license", type=Path, required=True)
    parser.add_argument("--qemu-helper", action="store_true")
    parser.add_argument("--work-dir", type=Path)
    args = parser.parse_args()
    original_rom = hashlib.sha256(args.rom.read_bytes()).hexdigest()
    helper = [str(args.helper)]
    if args.qemu_helper:
        helper = ["qemu-mipsel", "-cpu", "74Kf", "-L", str(args.sysroot), *helper]
    checks = 0
    with tempfile.TemporaryDirectory(prefix="sm64-lilpck-private-", dir=args.work_dir) as tmp:
        base = Path(tmp); root = base / "independent-apps"
        env = dict(os.environ, LILSMOKE_APPS_ROOT=str(root))
        if args.qemu_helper:
            env["LD_LIBRARY_PATH"] = str(args.sysroot / "usr/lib") + ":" + str(args.sysroot / "lib")

        def call(*words, ok=True):
            nonlocal checks
            result = subprocess.run([*helper, *map(str, words)], env=env, text=True, capture_output=True, timeout=120)
            assert (result.returncode == 0) == ok, result.stdout + result.stderr
            checks += 1
            return [json.loads(s) for s in result.stdout.splitlines() if s.startswith("{")]

        extras = dict(scene=args.scene, sdl_library=args.sdl_library, sdl_license=args.sdl_license)
        one = build(args.binary, base / "one.lilpck", args.gcc_license_dir, **extras)
        two = build(args.binary, base / "two.lilpck", args.gcc_license_dir, version="0.4.1", launcher="launch.sh", **extras)
        with zipfile.ZipFile(one) as archive:
            names = archive.namelist()
            assert [n for n in names if "/" not in n and n.endswith(".sh")] == ["SM64.sh"]
            assert all(not n.endswith((".z64", ".v64", ".n64", ".pdat", ".peng")) for n in names)
            assert all((i.external_attr >> 16) & 0o170000 == (0o40000 if i.is_dir() else 0o100000)
                       for i in archive.infolist())
            assert archive.getinfo("original/").file_size == 0
            assert "runtime/bin/sm64-scene-dev" in names and "runtime/lib/libSDL2-2.0.so.0" in names
            assert "runtime/licenses/GCC-runtime-exception.txt" in names
        validate_elf(args.binary)
        preview = call("inspect", one, base / "preview")[-1]
        assert preview["relation"] == "new"
        call("install", one)
        name = preview["name"]; app = root / name
        assert name == "SM64 Native Prototype"
        rom = app / "original/Any filename with no extension"
        shutil.copyfile(args.rom, rom)
        (app / "saves").mkdir(); (app / "saves/private.sav").write_bytes(b"private save")
        (app / "empty-user-directory").mkdir()
        (app / ".sm64").mkdir()
        data = app / "runtime/bin/portsmoke-sm64-data"
        cmd = ["qemu-mipsel", "-cpu", "74Kf", "-L", str(args.sysroot), str(data)]
        cache = app / ".sm64/sm64-us-v1.pdat"
        first = subprocess.run([*cmd, str(rom), str(cache)], text=True, capture_output=True, timeout=120)
        assert first.returncode == 0, first.stderr
        # Retain old v1 lock files as private user data, even though unused now.
        (app / ".sm64/sm64-us-v1.pdat.lock").write_bytes(b"legacy v1 lock")
        saved = {p.relative_to(app): hashlib.sha256(p.read_bytes()).hexdigest()
                 for p in (rom, cache, app / ".sm64/sm64-us-v1.pdat.lock", app / "saves/private.sav")}
        # Launcher behavior on host, executing the real target data tool under QEMU.
        # A temporary binfmt-free wrapper is a fixture, never shipped in the app.
        viewer = app / "runtime/bin/sm64-scene-dev"
        backup = base / "target-scene"; shutil.copyfile(viewer, backup); backup.chmod(0o755)
        viewer.write_text("#!/bin/sh\nexec qemu-mipsel -cpu 74Kf -L '" + str(args.sysroot) +
                          "' '" + str(backup) + "' \"$@\" --headless --frames 1\n")
        viewer.chmod(0o755)
        launched = subprocess.run(["sh", str(app / "SM64.sh")], cwd=app,
                                  env=dict(os.environ, LILSMOKE_APP_DIR=str(app)),
                                  text=True, capture_output=True, timeout=120)
        assert launched.returncode == 0 and "TerrainTriangles=1101" in launched.stdout, (
            launched.returncode, launched.stdout, launched.stderr)
        assert "SM64_NATIVE_MOVEMENT_PROTOTYPE_NOT_FULL_GAME" in launched.stdout
        assert "OriginalRemoved=1" in launched.stdout and not rom.exists()
        saved.pop(rom.relative_to(app))
        bob_cache = app / ".sm64/bob-us-v1.pdat"
        saved[bob_cache.relative_to(app)] = hashlib.sha256(bob_cache.read_bytes()).hexdigest()
        mario_cache = app / ".sm64/mario-us-v1.pdat"
        saved[mario_cache.relative_to(app)] = hashlib.sha256(mario_cache.read_bytes()).hexdigest()
        segment_cache = app / ".sm64/segments-us-v1.pdat"
        saved[segment_cache.relative_to(app)] = hashlib.sha256(segment_cache.read_bytes()).hexdigest()
        private_rom = args.rom.read_bytes()
        corrupt = bytearray(private_rom); corrupt[4] ^= 1
        rom.write_bytes(corrupt)
        failed = subprocess.run(["sh", str(app / "SM64.sh")], cwd=app,
                                env=dict(os.environ, LILSMOKE_APP_DIR=str(app)),
                                text=True, capture_output=True, timeout=120)
        assert failed.returncode != 0 and "SM64_NATIVE_MOVEMENT_PROTOTYPE_NOT_FULL_GAME" not in failed.stdout
        assert "unsupported ROM checksum" in failed.stderr
        assert rom.exists() and rom.read_bytes() == corrupt
        rom.write_bytes(private_rom)
        saved[rom.relative_to(app)] = hashlib.sha256(private_rom).hexdigest()
        for path, digest in saved.items():
            assert hashlib.sha256((app / path).read_bytes()).hexdigest() == digest
        call("install", two)
        assert [p.name for p in app.glob("*.sh")] == ["launch.sh"]
        assert (app / "empty-user-directory").is_dir()
        for path, digest in saved.items():
            assert hashlib.sha256((app / path).read_bytes()).hexdigest() == digest, path
        call("rename", name, "My SM64 Development")
        app = root / "My SM64 Development"
        call("install", one)
        assert [p.name for p in app.glob("*.sh")] == ["SM64.sh"]
        assert json.loads((app / "meta.json").read_text())["name"] == "My SM64 Development"
        for path, digest in saved.items():
            assert hashlib.sha256((app / path).read_bytes()).hexdigest() == digest
        call("install", one, ok=False)
        call("uninstall", "My SM64 Development")
        assert not list(root.iterdir())
    assert hashlib.sha256(args.rom.read_bytes()).hexdigest() == original_rom
    print(f"PASS {checks} real helper operations; original/ and all private caches/saves preserved, one launcher; native movement prototype exits 0")


if __name__ == "__main__":
    main()
