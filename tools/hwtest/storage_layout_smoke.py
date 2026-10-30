#!/usr/bin/env python3
"""Real disposable loopback tests; invoke under unshare --mount as root."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "test-results/storage-layout"
OUT.mkdir(parents=True, exist_ok=True)
checks = []

def run(args, env=None, okay=True):
    p = subprocess.run(list(map(str, args)), env=env, text=True,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=240)
    print(p.stdout, end="", flush=True)
    if okay and p.returncode:
        raise RuntimeError(f"{args}: exit {p.returncode}")
    return p

def check(value, label):
    if not value:
        raise AssertionError(label)
    checks.append(label)
    print("PASS", label, flush=True)

with tempfile.TemporaryDirectory(prefix="lilsmoke-storage-") as directory:
    base = Path(directory)
    helper = base / "lilsmoke-disk"
    run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
         ROOT / "package/lilsmoke-scripts/src/lilsmoke-disk.c", "-o", helper])

    def fixture(name, filesystem, fault=None):
        work = base / name
        work.mkdir()
        payload = work / "payload.itb"
        payload.write_bytes(b"STORAGE TEST PAYLOAD - NOT A BOOT BSP")
        image = work / "card.img"
        loop = None
        osdir, romdir, persist = (work / n for n in ("os", "roms", "persist"))
        for p in (osdir, romdir, persist, work / "sys", work / "run", work / "bin"):
            p.mkdir()
        env = dict(os.environ, LILSMOKE_OS_MOUNT=str(osdir),
                   LILSMOKE_STORAGE_ROOT=str(romdir), LILSMOKE_PERSIST_ROOT=str(persist),
                   LILSMOKE_STORAGE_RUN=str(work / "run"),
                   LILSMOKE_SYS_CLASS_BLOCK=str(work / "sys"),
                   LILSMOKE_DISK_TOOL=str(helper),
                   LILSMOKE_USB_BACKING_FILE=str(work / "usb-file"))
        service = ROOT / "package/lilsmoke-scripts/src/lilsmoke-storage"
        def boot(okay=True, useenv=None):
            return run(["busybox", "sh", service, "boot"], useenv or env, okay)
        def unmount(p):
            if subprocess.run(["mountpoint", "-q", str(p)]).returncode == 0:
                run(["umount", p])
        try:
            run(["bash", ROOT / "scripts/mk-sdcard.sh", "--development",
                 "--output", image, "--board", "qemu-mipsel", "--rootfs", payload,
                 "--roms-fs", "exfat"])
            loop = run(["losetup", "--find", "--show", "--partscan", image]).stdout.strip()
            (work / "sys" / Path(loop + "p1").name).symlink_to("/sys/class/block/" + Path(loop + "p1").name)
            if filesystem == "vfat":
                # User manually replaces the default exFAT filesystem; no label required.
                run(["mkfs.vfat", "-F", "32", loop + "p2"])
                check(run(["sfdisk", "--part-type", image, "2"]).stdout.strip() == "7",
                      name + ": manual FAT32 conversion retains original exFAT partition type")
            run(["mount", loop + "p2", romdir])
            expected = b"ROM DATA\x00\xff" * 8192
            (romdir / "ROM with spaces.sfc").write_bytes(expected)
            (romdir / "empty folder").mkdir()
            (romdir / ".hidden").write_text("retain hidden files")
            unmount(romdir)
            for child in ("engines", "ports", "saves"):
                (romdir / "portsmoke" / child).mkdir(parents=True, exist_ok=True)
            with image.open("rb") as f:
                before = f.read(512)
            original_roms = subprocess.check_output(["sha256sum", loop + "p2"]).split()[0]
            run(["losetup", "-d", loop])
            loop = None
            with image.open("r+b") as f:
                f.truncate(896 * 1024 * 1024)
            loop = run(["losetup", "--find", "--show", "--partscan", image]).stdout.strip()
            for link in (work / "sys").iterdir():
                link.unlink()
            (work / "sys" / Path(loop + "p1").name).symlink_to("/sys/class/block/" + Path(loop + "p1").name)
            if fault:
                wrapper = work / "bin" / ("cp" if fault == "restore" else "df")
                if fault == "restore":
                    wrapper.write_text("#!/bin/sh\n/bin/cp \"$@\"\nexit 42\n")
                else:
                    wrapper.write_text("#!/bin/sh\nprintf 'Filesystem 1K-blocks Used Available Use%% Mounted\\n/dev/test 1 1 1 100%% /test\\n'\n")
                wrapper.chmod(0o755)
                failed_env = dict(env, PATH=str(work / "bin") + ":" + os.environ["PATH"])
                check(boot(False, failed_env).returncode != 0, name + ": injected failure is reported")
                if fault == "space":
                    check("not enough LILOS space" in
                          boot(False, failed_env).stdout, name + ": failure specifically exercises free-space guard")
                    check(subprocess.check_output(["sha256sum", loop + "p2"]).split()[0] == original_roms,
                          name + ": insufficient backup space preserves LILROMS byte-for-byte")
                    geometry = run([helper, "info", loop]).stdout
                    check("p2_sectors=260096" in geometry, name + ": insufficient space does not resize")
                else:
                    check((osdir / "lilsmoke/grow/journal").exists() and
                          (osdir / "lilsmoke/grow/backup.img").exists(),
                          name + ": recovery journal and full backup retained")
                wrapper.unlink()
            boot()
            check(subprocess.run(["mountpoint", "-q", str(romdir)]).returncode == 0,
                  name + ": installed empty PortSmoke skeleton permits storage mount")
            check((romdir / "ROM with spaces.sfc").read_bytes() == expected,
                  name + ": existing ROM bytes survive growth")
            check((romdir / ".hidden").read_text() == "retain hidden files" and
                  (romdir / "empty folder").is_dir(), name + ": hidden files and empty directories preserved")
            with image.open("rb") as f:
                after = f.read(512)
            check(before[:462] == after[:462] and before[478:] == after[478:],
                  name + ": only partition 2 MBR entry changed")
            geometry = run([helper, "info", loop]).stdout
            values = dict(line.split("=", 1) for line in geometry.splitlines())
            check(values["p2_sectors"] == values["p2_target_sectors"],
                  name + ": partition reaches final sector")
            fs = subprocess.check_output(["blkid", "-s", "TYPE", "-o", "value", loop + "p2"], text=True).strip()
            check(fs == filesystem, name + ": filesystem type retained")
            stat = os.statvfs(romdir)
            check(stat.f_blocks * stat.f_frsize > 375 * 1024 * 1024,
                  name + ": filesystem, not just partition, enlarged")
            (persist / "survives").write_text(name)
            (persist / "symlink").symlink_to("survives")
            check((persist / "symlink").read_text() == name,
                  name + ": ext4 persistence supports POSIX symlinks")
            unmount(romdir)
            unmount(persist)
            unmount(osdir)
            boot()
            check((persist / "survives").read_text() == name and
                  not (osdir / "lilsmoke/grow/journal").exists(),
                  name + ": reboot is idempotent and persistence survives")
            unmount(romdir)
            marker = romdir / "portsmoke" / "saves" / "must-not-hide"
            marker.write_text("local save must survive")
            rejected = run(["busybox", "sh", service, "mount-roms"], env, False)
            check(rejected.returncode != 0 and "storage mount would hide files" in rejected.stdout
                  and marker.read_text() == "local save must survive",
                  name + ": nonempty rootfs storage is rejected without hiding data")
            marker.unlink()
            marker.symlink_to(work)
            rejected = run(["busybox", "sh", service, "mount-roms"], env, False)
            check(rejected.returncode != 0 and "storage mount would hide files" in rejected.stdout
                  and marker.is_symlink(),
                  name + ": storage symlinks are rejected without following them")
            marker.unlink()
            run(["busybox", "sh", service, "mount-roms"], env)
            (work / "usb-file").write_text(loop + "p2\n")
            unmount(romdir)
            check(run(["busybox", "sh", service, "mount-roms"], env, False).returncode != 0,
                  name + ": exported partition cannot be locally mounted")
            (work / "usb-file").unlink()
            run(["busybox", "sh", service, "mount-roms"], env)
            alias = work / "alias"
            alias.mkdir()
            run(["mount", "--bind", romdir, alias])
            try:
                check(run(["busybox", "sh", service, "unmount-roms"], env, False).returncode != 0,
                      name + ": alternate mount path also prevents USB export")
            finally:
                unmount(alias)
            run(["busybox", "sh", service, "mount-roms"], env)
            (work / "run/exporting").touch()
            check(run(["busybox", "sh", service, "mount-roms"], env, False).returncode != 0,
                  name + ": transfer marker prevents new local access")
        finally:
            for p in (work / "run/restore", work / "run/source", romdir, persist, osdir):
                unmount(p)
            if loop:
                run(["losetup", "-d", loop])

    for name, fs, fault in [("exfat", "exfat", None), ("fat32", "vfat", None),
                            ("recovery", "exfat", "restore"), ("no-space", "exfat", "space")]:
        fixture(name, fs, fault)
    invalid = base / "invalid.img"
    invalid.write_bytes(bytes(4096 * 512))
    check(run([helper, "grow", invalid], okay=False).returncode != 0,
          "unrecognised MBR is rejected")
    check(run(["bash", ROOT / "scripts/mk-sdcard.sh", "--device", "/dev/sda"], okay=False).returncode != 0,
          "image builder refuses raw device writes")
    check(run(["bash", ROOT / "scripts/mk-sdcard.sh"], okay=False).returncode != 0,
          "unverified production image is gated")

(OUT / "checks.json").write_text(json.dumps({"passed": len(checks), "checks": checks}, indent=2) + "\n")
print("STORAGE_LAYOUT_OK checks=" + str(len(checks)))
