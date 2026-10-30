"""Clean seven-patch check, isolated PCM mocks, and MIPS audio-object build.

No production backend is linked into the executed test binaries. --canonicalize
only mechanically regenerates 0007's unified diff against the exact 0001-6 stage.
"""
import argparse
import difflib
import os
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile

P = Path(__file__).resolve().parents[2]
PACKAGE = P / "package/opendingux-emulators"
B = Path("/root/lilsmoke-os/build/buildroot/output")
TARBALL = B.parent / "dl/opendingux-emulators/opendingux-emulators-6fff59072821a68859d0d4d5caeec36d02ea264f-br1.tar.gz"
PATCH = PACKAGE / "0007-m8-native-pcm-audio.patch"


def run(args, **kw):
    result = subprocess.run([str(a) for a in args], check=True, timeout=30, **kw)
    return result


def recount(text):
    lines = text.splitlines(keepends=True)
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
    parser.add_argument("--patch-only", action="store_true")
    args = parser.parse_args()
    env = dict(os.environ, PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin")
    with tempfile.TemporaryDirectory(prefix="m8-pcm-", dir="/tmp") as temp:
        root = Path(temp)
        with tarfile.open(TARBALL) as archive:
            archive.extractall(root / "source", filter="data")
        stage = next((root / "source").iterdir())
        for patch in sorted(PACKAGE.glob("000[1-6]-*.patch")):
            run(["patch", "-p1", "--batch", "--fuzz=0"], cwd=stage,
                input=patch.read_text(), text=True, env=env)
        paths = ["menu/main.cpp", "sal/linux/sal_sound.c"]
        before = {path: (stage / path).read_text() for path in paths}
        payload = PATCH.read_text()
        if args.canonicalize:
            payload = recount(payload)
        run(["patch", "-p1", "--batch", "--fuzz=0"], cwd=stage,
            input=payload, text=True, env=env)
        if args.canonicalize:
            canonical = ("From: LilSmokeOS <dev@lilsmokeos.local>\n"
                         "Subject: [PATCH] PocketSNES: opt-in PCM sink in isolated bounded audio worker\n\n")
            for path in paths:
                canonical += "".join(difflib.unified_diff(before[path].splitlines(True),
                                     (stage / path).read_text().splitlines(True),
                                     fromfile="a/" + path, tofile="b/" + path))
            PATCH.write_text(canonical)
        print("PASS: 0001-0007 apply sequentially to clean tarball with --fuzz=0.", flush=True)
        if args.patch_only:
            return
        batch_flags = ["-std=gnu99", "-Wall", "-Wextra", "-Werror", "-g", "-O1",
                       "-Wno-unused-parameter", "-Wno-old-style-declaration",
                       "-fsanitize=address,undefined", "-I" + str(PACKAGE),
                       "-I" + str(B / "staging/usr/include/SDL"),
                       "-I" + str(stage / "sal/linux"),
                       "-I" + str(stage / "sal/linux/include"), "-I" + str(stage / "sal/include")]
        run(["cc", *batch_flags, P / "tools/m8/test_m8_pcm_batch.c", "-o", root / "batch-test"], env=env)
        run([root / "batch-test"], env=env)
        flags = ["-std=gnu99", "-Wall", "-Wextra", "-Werror", "-O2",
                 "-I" + str(P / "board/segam-m8-v6-stock")]
        test = P / "tools/m8/test_m8_pcm.c"
        cc = B / "host/bin/mipsel-buildroot-linux-musl-gcc"
        for credits in (2, 16):
            profile_flags = [*flags, '-DM8_PCM_INFLIGHT_LIMIT=' + str(credits) + 'U']
            run(["cc", *profile_flags, test, "-o", root / "host-test"], env=env)
            run([cc, *profile_flags, "-static", test, "-o", root / "mips-test"], env=env)
            for target, command in (("host", [root / "host-test"]),
                                    ("MIPS", ["qemu-mipsel", "-cpu", "74Kf", root / "mips-test"])):
                for mode in ([], ["pipe"], ["classic"]):
                    result = run([*command, *mode], env=env, capture_output=True, text=True)
                    for diagnostic in ("stage=volume", "stage=pcm-format", "stage=pipe-data",
                                       "stage=fd-enumeration", "stage=fork", "stage=backend-open"):
                        if diagnostic not in result.stderr:
                            raise AssertionError(f"{target}/{mode}: missing {diagnostic}")
                    if mode and "ipc-fallback stage=socketpair" not in result.stderr:
                        raise AssertionError("missing UNIX socket fallback evidence")
                    print(target + ": " + result.stdout.strip(), flush=True)
        run([cc, *flags, "-c", PACKAGE / "m8_pocketsnes_audio.c", "-o", root / "worker.o"], env=env)
        run([cc, *flags, "-c", P / "board/segam-m8-v6-stock/m8-audio.c", "-o", root / "backend.o"], env=env)
        salflags = ["-std=gnu99", "-O2", "-DRC_OPTIMIZED", "-DGCW_ZERO", "-D__LINUX__",
                    "-D__DINGUX__", "-DFOREVER_16_BIT", "-I" + str(PACKAGE),
                    "-I" + str(B / "staging/usr/include/SDL")]
        for include in ["src", "sal/linux/include", "sal/include", "src/include", "menu", "src/linux", "src/snes9x"]:
            salflags.append("-I" + str(stage / include))
        run([cc, *salflags, "-c", stage / "sal/linux/sal_sound.c", "-o", root / "sal_sound.o"], env=env)
        print("PASS: host and MIPS isolated worker tests; target worker/backend/patched SAL objects built. No hardware ioctl linked into executed tests.", flush=True)


if __name__ == "__main__":
    main()
