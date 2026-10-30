"""Native RA audio adapter mock and link QA. Never build RA/cores or open audio devices."""
import argparse
import difflib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile

P = Path(__file__).resolve().parents[2]
PKG = P / "package/retroarch-mips"
B = Path("/root/lilsmoke-os/build/buildroot/output")
ARCHIVE = B.parent / "dl/retroarch-mips/retroarch-mips-v1.19.1-br1.tar.gz"


def run(argv, **kw):
    return subprocess.run([str(a) for a in argv], check=True, timeout=60, **kw)


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
    parser.add_argument("--all-patches", action="store_true", help="also check parent-owned patches with strict fuzz=0")
    args = parser.parse_args()
    env = dict(os.environ, PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin")
    with tempfile.TemporaryDirectory(prefix="m8-ra-audio-", dir="/tmp") as folder:
        temp = Path(folder)
        with tarfile.open(ARCHIVE) as archive:
            archive.extractall(temp / "source", filter="data")
        ra = next((temp / "source").iterdir())
        paths = ["Makefile.common", "audio/audio_driver.c", "audio/audio_driver.h"]
        pattern = "[0-9][0-9][0-9][0-9]-*.patch" if args.all_patches else "000[1-5]*.patch"
        for patch in sorted(PKG.glob(pattern)):
            own = patch.name.startswith("0005")
            before = {path: (ra / path).read_text() for path in paths} if own else {}
            content = recount(patch.read_text()) if own and args.canonicalize else patch.read_text()
            run(["patch", "-p1", "--batch", "--fuzz=0"], cwd=ra, input=content, text=True, env=env)
            if own and args.canonicalize:
                canonical = content.split("--- a/", 1)[0]
                for path in paths:
                    canonical += "".join(difflib.unified_diff(before[path].splitlines(True),
                        (ra / path).read_text().splitlines(True), "a/" + path, "b/" + path))
                patch.write_text(canonical)
        for name in ("m8_lilsmoke_audio.c",):
            shutil.copyfile(PKG / name, ra / "audio/drivers" / name)
        for name in ("m8_pocketsnes_audio.c", "m8_pocketsnes_audio.h"):
            shutil.copyfile(P / "package/opendingux-emulators" / name, ra / "audio/drivers" / name)
        for name in ("m8-audio.c", "m8-audio.h"):
            shutil.copyfile(P / "board/segam-m8-v6-stock" / name, ra / "audio/drivers" / name)
        flags = ["-std=gnu99", "-O2", "-Wall", "-Wextra", "-Werror", "-I" + str(ra),
                 "-I" + str(ra / "libretro-common/include"), "-I" + str(ra / "audio/drivers")]
        test = P / "tools/m8/test-retroarch-input-audio.c"
        run(["cc", *flags, test, "-o", temp / "host"], env=env)
        run([temp / "host"], env=env)
        run(["cc", *flags, "-O1", "-g", "-fsanitize=address,undefined", test, "-o", temp / "sanitized"], env=env)
        run([temp / "sanitized"], env=env)
        cc = B / "host/bin/mipsel-buildroot-linux-musl-gcc"
        mips = ["-mips32r2", "-mtune=74kc", "-mdspr2", "-mfp32", "-mhard-float"]
        run([cc, *flags, *mips, "-static", test, "-o", temp / "mips"], env=env)
        run(["qemu-mipsel", "-cpu", "74Kf", temp / "mips"], env=env)
        objects = []
        for name in ("m8_lilsmoke_audio.c", "m8_pocketsnes_audio.c", "m8-audio.c"):
            obj = temp / (name + ".o")
            run([cc, *flags, *mips, "-c", ra / "audio/drivers" / name, "-o", obj], env=env)
            objects.append(obj)
        run([cc, *flags, *mips, "-Wno-unused-parameter", "-Wno-unused-variable", "-DHAVE_LILSMOKE_AUDIO",
             "-include", ra / "command.h",
             "-c", ra / "audio/audio_driver.c", "-o", temp / "registry.o"], env=env)
        linked = temp / "audio-combined.o"
        run([B / "host/bin/mipsel-buildroot-linux-musl-ld", "-r", *objects, "-o", linked], env=env)
        unresolved = run([B / "host/bin/mipsel-buildroot-linux-musl-nm", "-u", linked], env=env, capture_output=True, text=True)
        assert "m8_pcm_" not in unresolved.stdout and "m8_audio_" not in unresolved.stdout
        print("PASS: clean package patches --fuzz=0, host/sanitizers/MIPS adapter mocks, audio registry, authoritative bridge/backend target objects and relocation link. No full RA/core build or device ioctl.")


if __name__ == "__main__":
    main()
