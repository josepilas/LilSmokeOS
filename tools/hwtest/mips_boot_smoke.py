#!/usr/bin/env python3
"""
Boot LilSmokeOS smoke checks inside a real mipsel Linux kernel under QEMU.

This is the closest runnable test on this Windows host without a full
Buildroot/WSL environment:

* QEMU machine: malta, mipsel, 256 MB RAM
* Kernel: Debian bookworm mipsel Malta netboot kernel
* Initramfs: generated locally with Debian mipsel busybox-static and the
  LilSmoke scripts/configs under test

The initramfs does not pretend to be the final SF3000 kernel. It verifies
that the project scripts execute under real MIPS little-endian Linux and
that the hardware-facing behaviors still match the SF3000/GB350 contract.
"""

from __future__ import annotations

import gzip
import io
import lzma
import os
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.request
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WORKSPACE_ROOT = ROOT.parents[2]
WORK = WORKSPACE_ROOT / "work" / "mipsboot"
REPORT = WORKSPACE_ROOT / "work" / "mips-boot-smoke.log"
DEBIAN_NETBOOT = "https://ftp.debian.org/debian/dists/bookworm/main/installer-mipsel/current/images/malta/netboot/"
BUSYBOX_DEB = "https://ftp.debian.org/debian/pool/main/b/busybox/busybox-static_1.35.0-4+deb12u1+b1_mipsel.deb"


def find_qemu() -> Path:
    candidates = [
        WORKSPACE_ROOT / "work" / "tools" / "qemu" / "qemu-system-mipsel.exe",
        *sorted((WORKSPACE_ROOT / "work" / "tools" / "qemu-portable").glob("**/qemu-system-mipsel.exe")),
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    found = shutil.which("qemu-system-mipsel")
    if found:
        return Path(found)
    raise SystemExit("qemu-system-mipsel.exe not found")


def download(url: str, path: Path) -> None:
    if path.exists() and path.stat().st_size > 0:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    print(f"download: {url}")
    with urllib.request.urlopen(url, timeout=120) as r, path.open("wb") as f:
        shutil.copyfileobj(r, f)


def netboot_kernel_name() -> str:
    with urllib.request.urlopen(DEBIAN_NETBOOT, timeout=60) as r:
        html = r.read().decode("utf-8", errors="replace")
    names = re.findall(r'href="(vmlinuz-[^"]+)"', html)
    if not names:
        raise SystemExit("could not find Debian mipsel Malta kernel in netboot index")
    return names[-1]


def extract_ar_member(ar_path: Path, wanted_prefix: bytes) -> bytes:
    data = ar_path.read_bytes()
    if not data.startswith(b"!<arch>\n"):
        raise ValueError(f"{ar_path} is not an ar archive")
    pos = 8
    while pos + 60 <= len(data):
        header = data[pos : pos + 60]
        pos += 60
        name = header[:16].decode("ascii", errors="replace").strip()
        size = int(header[48:58].decode("ascii").strip())
        payload = data[pos : pos + size]
        pos += size + (size % 2)
        clean_name = name.rstrip("/")
        if clean_name.encode("ascii", errors="ignore").startswith(wanted_prefix):
            return payload
    raise FileNotFoundError(wanted_prefix.decode())


def extract_busybox(deb_path: Path, out_path: Path) -> None:
    if out_path.exists() and out_path.stat().st_size > 0:
        return
    data_xz = extract_ar_member(deb_path, b"data.tar")
    with lzma.open(io.BytesIO(data_xz)) as xz:
        with tarfile.open(fileobj=xz, mode="r:*") as tf:
            member = next((m for m in tf.getmembers() if m.name in ("./bin/busybox", "bin/busybox")), None)
            if not member:
                raise FileNotFoundError("busybox not found in deb")
            extracted = tf.extractfile(member)
            if not extracted:
                raise FileNotFoundError("busybox member is not a file")
            out_path.parent.mkdir(parents=True, exist_ok=True)
            out_path.write_bytes(extracted.read())
    out_path.chmod(0o755)


@dataclass
class CpioEntry:
    name: str
    mode: int
    data: bytes = b""
    uid: int = 0
    gid: int = 0
    nlink: int = 1
    rdevmajor: int = 0
    rdevminor: int = 0
    symlink: str | None = None


class Newc:
    def __init__(self) -> None:
        self.entries: list[CpioEntry] = []
        self.ino = 721

    def dir(self, name: str, mode: int = 0o755) -> None:
        self.entries.append(CpioEntry(name.strip("/"), stat.S_IFDIR | mode, nlink=2))

    def file(self, name: str, data: bytes | str, mode: int = 0o644) -> None:
        if isinstance(data, str):
            data = data.encode("utf-8")
        self.entries.append(CpioEntry(name.strip("/"), stat.S_IFREG | mode, data=data))

    def symlink(self, name: str, target: str) -> None:
        self.entries.append(CpioEntry(name.strip("/"), stat.S_IFLNK | 0o777, data=target.encode(), symlink=target))

    def chrdev(self, name: str, major: int, minor: int, mode: int = 0o600) -> None:
        self.entries.append(CpioEntry(name.strip("/"), stat.S_IFCHR | mode, rdevmajor=major, rdevminor=minor))

    def blkdev(self, name: str, major: int, minor: int, mode: int = 0o600) -> None:
        self.entries.append(CpioEntry(name.strip("/"), stat.S_IFBLK | mode, rdevmajor=major, rdevminor=minor))

    def build(self) -> bytes:
        out = bytearray()
        for entry in self.entries + [CpioEntry("TRAILER!!!", 0)]:
            name = entry.name.encode() + b"\0"
            data = entry.data
            fields = [
                b"070701",
                f"{self.ino:08x}".encode(),
                f"{entry.mode:08x}".encode(),
                f"{entry.uid:08x}".encode(),
                f"{entry.gid:08x}".encode(),
                f"{entry.nlink:08x}".encode(),
                f"{int(time.time()):08x}".encode(),
                f"{len(data):08x}".encode(),
                b"00000000",
                b"00000000",
                f"{entry.rdevmajor:08x}".encode(),
                f"{entry.rdevminor:08x}".encode(),
                f"{len(name):08x}".encode(),
                b"00000000",
            ]
            header = b"".join(fields)
            out.extend(header)
            out.extend(name)
            while len(out) % 4:
                out.append(0)
            out.extend(data)
            while len(out) % 4:
                out.append(0)
            self.ino += 1
        return bytes(out)


def project_file(path: str) -> bytes:
    return (ROOT / path).read_bytes()


def make_update_bundle() -> bytes:
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as tf:
        manifest = b'{"version":"0.7.1-mipsboot","sha256":"","size":7}\n'
        info = tarfile.TarInfo("manifest.json")
        info.size = len(manifest)
        tf.addfile(info, io.BytesIO(manifest))
        fit = b"FITDATA"
        info = tarfile.TarInfo("lilsmoke.itb")
        info.size = len(fit)
        tf.addfile(info, io.BytesIO(fit))
    return buf.getvalue()


INIT = r"""#!/bin/sh
exec </dev/console >/dev/console 2>&1
export PATH=/usr/bin:/bin:/sbin:/usr/sbin:/usr/lib/lilsmoke

PASS=0
FAIL=0
pass() { echo "PASS: $*"; PASS=$((PASS + 1)); }
fail() { echo "FAIL: $*"; FAIL=$((FAIL + 1)); }
need() { if eval "$1"; then pass "$2"; else fail "$2"; fi; }

echo "LilSmokeOS MIPS boot smoke starting"
/bin/busybox mount -t proc proc /proc 2>/dev/null || true
mkdir -p /tmp /run /var/run /var/log /persist/config /persist/updates /persist/saves /persist/states /storage/roms /etc/lilsmoke

ARCH="$(uname -m 2>/dev/null || echo unknown)"
echo "arch=$ARCH"
case "$ARCH" in mips*) pass "running under real MIPS Linux";; *) fail "expected MIPS Linux";; esac
MEM="$(awk '/MemTotal/ { print $2 }' /proc/meminfo 2>/dev/null || echo 0)"
echo "mem_kb=$MEM"
need "[ \"$MEM\" -gt 200000 ] && [ \"$MEM\" -lt 330000 ]" "QEMU RAM is close to 256 MB hardware ceiling"

cp /etc/lilsmoke/lilsmoke.conf.default /persist/config/lilsmoke.conf
cat >> /persist/config/lilsmoke.conf <<'EOF'
WIFI_ENABLED=on
SSH_ENABLED=on
ZRAM_SIZE_MB=80
CPU_GOVERNOR=ondemand
CPU_OVERCLOCK=1400MHz
CPU_STOCK_MAX_KHZ=1188000
EOF

mkdir -p /sys/block/zram0
: > /dev/zram0
: > /sys/block/zram0/comp_algorithm
: > /sys/block/zram0/disksize
: > /sys/block/zram0/reset
/etc/init.d/S10zram start
need "grep -q lz4 /sys/block/zram0/comp_algorithm" "zram selects LZ4"
need "grep -q 67108864 /sys/block/zram0/disksize" "zram clamps to 64 MB when config exceeds budget"
need "grep -q 'swapon -p 100' /tmp/hwtest-tools.log" "zram enables high-priority swap"

mkdir -p /sys/devices/system/cpu/cpu0/cpufreq /sys/class/thermal/thermal_zone0
echo '600000 1000000 1188000 1300000 1400000' > /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies
echo 'powersave ondemand performance' > /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_governors
echo powersave > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
echo 600000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq
echo 1188000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq
echo 45000 > /sys/class/thermal/thermal_zone0/temp
lilsmoke-overclock apply
need "grep -q 1400000 /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq" "overclock reaches 1.4 GHz only when advertised and cool"
echo 83000 > /sys/class/thermal/thermal_zone0/temp
lilsmoke-overclock apply
need "grep -q 1188000 /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq" "overclock throttles back to stock when hot"

mkdir -p /sys/class/net
: > /sys/class/net/wlan0
echo 'network={}' > /persist/config/wifi.conf
/etc/init.d/S20network start
/etc/init.d/S30dropbear start
echo "--- hwtest-tools.log ---"
cat /tmp/hwtest-tools.log 2>/dev/null || true
echo "--- end hwtest-tools.log ---"
need "grep -q 'wpa_supplicant -B -i wlan0' /tmp/hwtest-tools.log" "WiFi starts when setting is on"
need "grep -q 'dropbear -R -B -E' /tmp/hwtest-tools.log" "SSH starts when setting is on and network has IP"

mkdir -p /storage/roms/snes
echo packed > "/storage/roms/snes/Mips Smoke.7z"
need "[ -s /usr/lib/libretro/snes9x_2005_libretro.so ]" "prebuilt MIPS SNES core is available in target root"
lilsmoke-launch snes "/storage/roms/snes/Mips Smoke.7z"
need "grep -q '7za x -y -o' /tmp/hwtest-tools.log" "7z ROMs extract through p7zip"
need "grep -q 'Mips Smoke.sfc' /tmp/hwtest-tools.log" "launcher passes extracted ROM to RetroArch"

echo A > /persist/active_slot
mknod /dev/mmcblk0p2 b 1 5 2>/dev/null || true
mknod /dev/mmcblk0p3 b 1 7 2>/dev/null || true
cp /fixtures/lilsmoke-update-0.7.1.tar.gz /persist/updates/
lilsmoke-update install
need "grep -q 'mke2fs -q -t ext4 -L slotB' /tmp/hwtest-tools.log" "OTA formats inactive ext4 FIT slot"
need "grep -q pending /persist/slot_B_status" "OTA marks inactive slot pending"

mkdir -p /storage/portsmoke/engines /storage/portsmoke/ports/diablo /storage/portsmoke/saves
echo fake-peng > /storage/portsmoke/engines/devilutionx.peng
cat > /storage/portsmoke/ports/diablo/portsmoke.conf <<'EOF'
[PortSmoke]
Name=Diablo 1
EngineFile=devilutionx.peng
Script=Diablo.sh
Cover=cover.webp
EOF
cat > /storage/portsmoke/ports/diablo/Diablo.sh <<'EOF'
#!/bin/sh
export SDL_VIDEODRIVER="directfb"
export SDL_AUDIODRIVER="alsa"
unset DISPLAY
mkdir -p "$PORTSMOKE_SAVE_DIR"
rm -f saves
ln -s "$PORTSMOKE_SAVE_DIR" saves
echo "portsmoke-game game=$PORTSMOKE_GAME engine=$PORTSMOKE_ENGINE engine_dir=$PORTSMOKE_ENGINE_DIR save=$PORTSMOKE_SAVE_DIR video=$SDL_VIDEODRIVER audio=$SDL_AUDIODRIVER display=${DISPLAY-unset}" >> /tmp/hwtest-tools.log
[ "$PORTSMOKE_GAME" = "diablo" ] || exit 21
[ "$PORTSMOKE_ENGINE" = "devilutionx" ] || exit 22
[ "$PORTSMOKE_ENGINE_DIR" = "/mnt/portsmoke/devilutionx" ] || exit 23
[ "$SDL_VIDEODRIVER" = "directfb" ] || exit 24
[ "$SDL_AUDIODRIVER" = "alsa" ] || exit 25
[ ! -e "$DISPLAY" ] || exit 26
[ -L saves ] || exit 27
exit 0
EOF
chmod +x /storage/portsmoke/ports/diablo/Diablo.sh
PORTSMOKE_MOUNT_CMD=hw-mount PORTSMOKE_UMOUNT_CMD=hw-umount portsmoke-launch diablo
need "grep -q 'mount -t squashfs -o loop /storage/portsmoke/engines/devilutionx.peng /mnt/portsmoke/devilutionx' /tmp/hwtest-tools.log" "PortSmoke mounts .peng engine image"
need "grep -q 'portsmoke-game game=diablo engine=devilutionx' /tmp/hwtest-tools.log" "PortSmoke executes game maestro script with DirectFB/ALSA env"
need "grep -q 'umount /mnt/portsmoke/devilutionx' /tmp/hwtest-tools.log" "PortSmoke unmounts engine after exit"

echo "LILSMOKE_MIPS_BOOT_RESULT pass=$PASS fail=$FAIL"
if [ "$FAIL" -eq 0 ]; then
    echo "LILSMOKE_MIPS_BOOT_TEST_DONE"
else
    echo "LILSMOKE_MIPS_BOOT_TEST_FAILED"
fi
sync
sleep 1
poweroff -f 2>/dev/null || halt -f 2>/dev/null || reboot -f 2>/dev/null
while true; do sleep 60; done
"""


FAKE_TOOL = """#!/bin/sh
name="$(basename "$0")"
name="${name#hw-}"
echo "$name $*" >> /tmp/hwtest-tools.log
case "$name" in
  ip)
    if [ "$1 $2 $3" = "link show eth0" ]; then exit 1; fi
    if [ "$1 $2" = "addr show" ]; then echo "inet 192.168.1.7/24"; exit 0; fi
    ;;
esac
exit 0
"""


FAKE_7ZA = """#!/bin/sh
echo "7za $*" >> /tmp/hwtest-tools.log
out=""
for arg in "$@"; do
  case "$arg" in
    -o*) out="${arg#-o}" ;;
  esac
done
[ -n "$out" ] || exit 9
mkdir -p "$out"
echo rom > "$out/Mips Smoke.sfc"
exit 0
"""


FAKE_RETROARCH = """#!/bin/sh
printf 'retroarch' >> /tmp/hwtest-tools.log
for arg in "$@"; do
  printf ' [%s]' "$arg" >> /tmp/hwtest-tools.log
done
printf '\\n' >> /tmp/hwtest-tools.log
exit 0
"""


def build_initramfs(busybox: Path, out_path: Path) -> None:
    cpio = Newc()
    for d in [
        "bin",
        "sbin",
        "usr",
        "usr/bin",
        "usr/sbin",
        "usr/lib",
        "usr/lib/lilsmoke",
        "usr/lib/libretro",
        "etc",
        "etc/init.d",
        "etc/lilsmoke",
        "dev",
        "proc",
        "sys",
        "tmp",
        "run",
        "var",
        "var/log",
        "fixtures",
        "persist",
        "storage",
    ]:
        cpio.dir(d)
    cpio.chrdev("dev/console", 5, 1, 0o600)
    cpio.chrdev("dev/null", 1, 3, 0o666)
    cpio.file("bin/busybox", busybox.read_bytes(), 0o755)
    for applet in [
        "sh",
        "ash",
        "cat",
        "cp",
        "echo",
        "grep",
        "sed",
        "awk",
        "head",
        "ls",
        "mkdir",
        "mknod",
        "ln",
        "rm",
        "rmdir",
        "sleep",
        "stat",
        "sync",
        "tar",
        "uname",
        "chmod",
        "poweroff",
        "halt",
        "reboot",
        "basename",
        "dirname",
        "find",
        "mv",
        "true",
        "false",
        "tr",
    ]:
        cpio.symlink(f"bin/{applet}", "busybox")
    for applet in ["mdev"]:
        cpio.symlink(f"sbin/{applet}", "../bin/busybox")
    for name in ["mkswap", "swapon", "swapoff", "ip", "wpa_supplicant", "udhcpc", "dropbear", "killall", "mke2fs", "mount", "umount"]:
        cpio.file(f"usr/bin/hw-{name}", FAKE_TOOL, 0o755)
    cpio.file("bin/mount", FAKE_TOOL, 0o755)
    cpio.file("bin/umount", FAKE_TOOL, 0o755)
    cpio.file("usr/bin/mount", FAKE_TOOL, 0o755)
    cpio.file("usr/bin/umount", FAKE_TOOL, 0o755)
    cpio.file("usr/bin/7za", FAKE_7ZA, 0o755)
    cpio.file("usr/bin/retroarch", FAKE_RETROARCH, 0o755)
    cpio.file(
        "usr/lib/libretro/snes9x_2005_libretro.so",
        project_file("package/retroarch-mips/prebuilt/mipsel-musleabihf/snes9x_2005_libretro.so"),
        0o644,
    )
    cpio.file("init", INIT, 0o755)
    cpio.file("etc/lilsmoke/lilsmoke.conf.default", project_file("board/sf3000/lilsmoke.conf"), 0o644)
    s10 = project_file("board/sf3000/init.d/S10zram").decode("utf-8")
    s10 = s10.replace("mkswap ", "hw-mkswap ").replace("swapon ", "hw-swapon ").replace("swapoff ", "hw-swapoff ")
    cpio.file("etc/init.d/S10zram", s10, 0o755)

    s20 = project_file("board/sf3000/init.d/S20network").decode("utf-8")
    s20 = (
        s20.replace("ip link", "hw-ip link")
        .replace("command -v wpa_supplicant >/dev/null 2>&1", "[ -x /usr/bin/hw-wpa_supplicant ]")
        .replace("wpa_supplicant ", "hw-wpa_supplicant ")
        .replace("udhcpc ", "hw-udhcpc ")
        .replace("killall ", "hw-killall ")
        .replace("WLAN_IF=$(ls /sys/class/net 2>/dev/null | grep -E '^wlan' | head -1)", "WLAN_IF=wlan0")
        .replace("hw-hw-wpa_supplicant", "hw-wpa_supplicant")
    )
    cpio.file("etc/init.d/S20network", s20, 0o755)

    s30 = project_file("board/sf3000/init.d/S30dropbear").decode("utf-8")
    s30 = s30.replace("ip addr", "hw-ip addr").replace("dropbear ", "hw-dropbear ")
    cpio.file("etc/init.d/S30dropbear", s30, 0o755)
    cpio.file("usr/lib/lilsmoke/lilsmoke-overclock", project_file("package/lilsmoke-scripts/src/lilsmoke-overclock"), 0o755)
    cpio.file("usr/lib/lilsmoke/lilsmoke-launch", project_file("package/lilsmoke-scripts/src/lilsmoke-launch"), 0o755)
    cpio.file("usr/bin/portsmoke-launch", project_file("package/portsmoke/src/portsmoke-launch"), 0o755)
    update = project_file("package/lilsmoke-scripts/src/lilsmoke-update").decode("utf-8")
    update = update.replace("mke2fs ", "hw-mke2fs ")
    cpio.file("usr/lib/lilsmoke/lilsmoke-update", update, 0o755)
    cpio.file("fixtures/lilsmoke-update-0.7.1.tar.gz", make_update_bundle(), 0o644)
    out_path.write_bytes(gzip.compress(cpio.build(), compresslevel=6))


def main() -> int:
    WORK.mkdir(parents=True, exist_ok=True)
    qemu = find_qemu()
    kernel_name = netboot_kernel_name()
    kernel = WORK / kernel_name
    deb = WORK / "busybox-static_mipsel.deb"
    busybox = WORK / "busybox-mipsel"
    initramfs = WORK / "lilsmoke-mips-smoke-initramfs.cpio.gz"

    download(DEBIAN_NETBOOT + kernel_name, kernel)
    download(BUSYBOX_DEB, deb)
    extract_busybox(deb, busybox)
    build_initramfs(busybox, initramfs)

    cmd = [
        str(qemu),
        "-M",
        "malta",
        "-m",
        "256M",
        "-nographic",
        "-nodefaults",
        "-serial",
        "stdio",
        "-monitor",
        "none",
        "-nic",
        "none",
        "-no-reboot",
        "-kernel",
        str(kernel),
        "-initrd",
        str(initramfs),
        "-append",
        "console=ttyS0 rdinit=/init panic=-1",
    ]
    print("running:", " ".join(cmd))
    try:
        cp = subprocess.run(cmd, cwd=str(ROOT), text=True, capture_output=True, timeout=180)
        output = (cp.stdout or "") + (cp.stderr or "")
    except subprocess.TimeoutExpired as exc:
        output = (exc.stdout or "") + (exc.stderr or "")
        if isinstance(output, bytes):
            output = output.decode("utf-8", errors="replace")
    REPORT.write_text(output, encoding="utf-8", errors="replace")
    print(output)
    print(f"log: {REPORT}")
    if "LILSMOKE_MIPS_BOOT_TEST_DONE" in output and "FAIL:" not in output:
        return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
