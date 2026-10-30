#!/usr/bin/env python3
"""Render actual private US Bob data; retain metrics only unless --keep-private."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
from test_private_rom import mio0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--sysroot", type=Path)
    parser.add_argument("--keep-private", type=Path)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    command = [str(args.binary)]
    if args.sysroot:
        command = ["qemu-mipsel", "-cpu", "74Kf", "-L", str(args.sysroot), *command]
    original_bytes = args.rom.read_bytes(); original_hash = hashlib.sha256(original_bytes).hexdigest()
    temp = tempfile.TemporaryDirectory(prefix="sm64-private-scene-")
    try:
        base = Path(temp.name)
        original = base / "original"; original.mkdir()
        path = original / "arbitrary name without extension"; path.write_bytes(original_bytes)
        (original / "notes.txt").write_text("non-ROM files are ignored")
        pack = base / "bob.pdat"
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        if args.sysroot:
            env["LD_LIBRARY_PATH"] = str(args.sysroot / "usr/lib") + ":" + str(args.sysroot / "lib")

        def call(*words, ok=True):
            result = subprocess.run([*command, *map(str, words)], env=env, text=True, capture_output=True, timeout=120)
            assert (result.returncode == 0) == ok, (result.returncode, result.stdout, result.stderr)
            assert "runtime error:" not in result.stderr and "ERROR: AddressSanitizer" not in result.stderr, result.stderr
            return result.stdout

        first = call("--original", original, "--pack", pack, "--headless", "--frames", 1, "--snapshot", base / "scene.bmp")
        assert "1101 triangles" in first and "GAMEPLAY_UNIMPLEMENTED" in first
        data = pack.read_bytes(); index = 64
        for block, expected_size in [(4178608, 71618), (3330160, 47104)]:
            length, fmt, flags, w, h, start, size = struct.unpack_from("<HBBHHII", data, index)
            index += 16 + length
            assert (fmt, flags, w, h, size) == (0, 0, 0, 0, expected_size)
            assert data[start:start + size] == mio0(original_bytes, block)
        inputs = call("--pack", pack, "--headless", "--input-check")
        assert "InputCheck=PASS" in inputs and "SceneFrames=3" in inputs
        moved = call("--pack", pack, "--headless", "--frames", 1, "--yaw", 1.5, "--snapshot", base / "moved.bmp")
        assert hashlib.sha256((base / "scene.bmp").read_bytes()).digest() != hashlib.sha256((base / "moved.bmp").read_bytes()).digest()
        # All byte orders, arbitrary filenames, duplicate/header/hash/link guards.
        checks = 0
        for stride in (2, 4):
            swapped = bytearray(original_bytes)
            for i in range(0, len(swapped), stride): swapped[i:i+stride] = swapped[i:i+stride][::-1]
            path.write_bytes(swapped)
            call("--original", original, "--pack", pack, "--headless", "--frames", 1)
            checks += 1
        path.write_bytes(original_bytes)
        extra = original / "duplicate.random"; extra.write_bytes(original_bytes)
        call("--original", original, "--pack", pack, "--headless", "--frames", 1, ok=False); extra.unlink(); checks += 1
        corrupt = bytearray(original_bytes); corrupt[4] ^= 1; path.write_bytes(corrupt)
        call("--original", original, "--pack", pack, "--headless", "--frames", 1, ok=False); checks += 1
        path.unlink(); path.symlink_to(args.rom)
        call("--original", original, "--pack", pack, "--headless", "--frames", 1, ok=False); path.unlink(); checks += 1
        path.write_bytes(original_bytes)
        call("--pack", pack, "--headless", "--frames", 1, "--snapshot", path, ok=False); checks += 1
        assert path.read_bytes() == original_bytes
        report = dict(status="PASS", target="MIPS-user" if args.sysroot else "host", native_scene=True,
                      gameplay=False, segment_bytes=[71618, 47104], triangles=1101, discovery_checks=checks,
                      first_render=first.strip().splitlines(), input_test=inputs.strip().splitlines(),
                      moved_render=moved.strip().splitlines(), original_rom_unchanged=True)
        if args.keep_private:
            args.keep_private.mkdir(parents=True, exist_ok=True)
            for name in ("scene.bmp", "moved.bmp"):
                shutil.copyfile(base / name, args.keep_private / name)
        if args.out:
            args.out.parent.mkdir(parents=True, exist_ok=True); args.out.write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report, indent=2))
    finally:
        temp.cleanup()
    assert hashlib.sha256(args.rom.read_bytes()).hexdigest() == original_hash


if __name__ == "__main__":
    main()
