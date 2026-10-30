"""RA 1.19.1 patch, native joypad mocks and MIPS object QA; never rebuild cores."""
import argparse
import difflib
import os
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile

P = Path(__file__).resolve().parents[2]
PKG = P / "package/retroarch-mips"
B = Path("/root/lilsmoke-os/build/buildroot/output")
ARCHIVE = B.parent / "dl/retroarch-mips/retroarch-mips-v1.19.1-br1.tar.gz"


def run(argv, **kw):
    return subprocess.run([str(x) for x in argv], check=True, timeout=60, **kw)


def recount(text):
    lines = text.splitlines(True)
    for i, line in enumerate(lines):
        match = re.match(r"@@ -(\d+)(?:,\d+)? \+(\d+)(?:,\d+)? @@(.*)", line)
        if not match:
            continue
        old = new = 0
        for body in lines[i + 1:]:
            if body.startswith(("@@ ", "--- a/", "+++ b/")):
                break
            if body.startswith(" "): old += 1; new += 1
            elif body.startswith("-"): old += 1
            elif body.startswith("+"): new += 1
            else: break
        lines[i] = f"@@ -{match[1]},{old} +{match[2]},{new} @@{match[3]}\n"
    return "".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--canonicalize", action="store_true")
    args = parser.parse_args()
    env = dict(os.environ, PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin")
    with tempfile.TemporaryDirectory(prefix="m8-retroarch-input-", dir="/tmp") as folder:
        temp = Path(folder)
        with tarfile.open(ARCHIVE) as archive:
            archive.extractall(temp / "source", filter="data")
        ra = next((temp / "source").iterdir())
        owned = {"0003": ["Makefile.common", "input/input_driver.c", "input/input_driver.h"],
                 "0004": ["gfx/drivers/sdl2_gfx.c"]}
        for patch in sorted(PKG.glob("000[1-4]-*.patch")):
            paths = owned.get(patch.name[:4], [])
            before = {path: (ra / path).read_text() for path in paths}
            content = recount(patch.read_text()) if args.canonicalize and paths else patch.read_text()
            run(["patch", "-p1", "--batch", "--fuzz=0"], cwd=ra, input=content, text=True, env=env)
            if args.canonicalize and paths:
                prefix = content.split("--- a/", 1)[0]
                for path in paths:
                    prefix += "".join(difflib.unified_diff(before[path].splitlines(True),
                        (ra / path).read_text().splitlines(True), "a/" + path, "b/" + path))
                patch.write_text(prefix)
        fixture = temp / "maps"
        for name in ("builtin", "user", "gamepads"):
            (fixture / name).mkdir(parents=True)
        (fixture / "builtin/default.map").write_text("evdev 29 a\nevdev 28 b\n")
        (fixture / "user/user.map").write_text("evdev 28 start\n")
        manifest = temp / "manifest.mk"
        manifest.write_text((PKG / "retroarch-mips.mk").read_text().split("# --- Build RetroArch itself ---", 1)[0] +
                            "\nreport:\n\t@echo $(LILSMOKE_CORES)\n")
        release = run(["make", "-s", "-f", manifest, "report", "BR2_PACKAGE_RETROARCH_MIPS_SNES_QA=y"],
                      env=env, capture_output=True, text=True)
        assert set(release.stdout.split()) == {"fceumm", "gambatte", "mgba", "snes9x2005", "genesis_plus_gx", "fbneo", "mednafen_pce_fast", "handy"}
        includes = ["-I" + str(ra), "-I" + str(ra / "libretro-common/include")]
        flags = ["-std=gnu99", "-O2", "-Wall", "-Wextra", "-Werror", *includes]
        source = P / "tools/m8/test-retroarch-input.c"
        run(["cc", *flags, source, "-o", temp / "host"], env=env)
        run([temp / "host", fixture], env=env)
        run(["cc", *flags, "-O1", "-g", "-fsanitize=address,undefined", source, "-o", temp / "asan"], env=env)
        run([temp / "asan", fixture], env=env)
        cc = B / "host/bin/mipsel-buildroot-linux-musl-gcc"
        mips = ["-mips32r2", "-mtune=74kc", "-mdspr2", "-mfp32", "-mhard-float"]
        run([cc, *flags, *mips, "-static", source, "-o", temp / "mips"], env=env)
        run(["qemu-mipsel", "-cpu", "74Kf", temp / "mips", fixture], env=env)
        run([cc, *flags, *mips, "-c", PKG / "m8_lilsmoke_input.c", "-o", temp / "joypad.o"], env=env)
        run([cc, *flags, *mips, "-Wno-unused-parameter", "-Wno-unused-variable", "-DHAVE_LILSMOKE_EVDEV", "-c", ra / "input/input_driver.c", "-o", temp / "registry.o"], env=env)
        window = P / "tools/m8/test-retroarch-input-window.c"
        run(["cc", *flags, window, "-o", temp / "window"], env=env)
        run([temp / "window"], env=env)
        run([cc, *flags, *mips, "-static", window, "-o", temp / "window-mips"], env=env)
        run(["qemu-mipsel", "-cpu", "74Kf", temp / "window-mips"], env=env)
        print("PASS: fresh RA 1.19.1 patches --fuzz=0, actual joypad ABI, host/sanitizers/MIPS mocks, registry and driver objects. No cores rebuilt, device ioctl or card access.")


if __name__ == "__main__":
    main()
