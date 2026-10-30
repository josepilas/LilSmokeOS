#!/usr/bin/env python3
"""
Hardware-faithful smoke tests for LilSmokeOS.

This is not a replacement for booting on an SF3000/GB350. It is a host-side
test harness that exercises the board-critical contracts without needing root:

* MIPS32r2/74Kc Buildroot config
* no-GPU DirectFB/SDL2 framebuffer stack
* real-hardware BSP capability gating
* one-vCPU QEMU framebuffer/input configuration
* zram sizing under the 128-256 MB RAM budget
* WiFi/SSH boolean settings written by LilSmokeUI
* guarded CPUFreq/thermal overclock behavior
"""

from __future__ import annotations

import os
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WORKSPACE_ROOT = ROOT.parents[2]
REPORT = WORKSPACE_ROOT / "work" / "hardware-test-report.md"
BASH_CANDIDATES = [
    Path(r"C:\Program Files\Git\bin\bash.exe"),
    Path(r"C:\Program Files\Git\usr\bin\bash.exe"),
]


@dataclass
class Result:
    name: str
    ok: bool
    detail: str = ""


RESULTS: list[Result] = []


def add(name: str, ok: bool, detail: str = "") -> None:
    RESULTS.append(Result(name, ok, detail.strip()))


def rel(path: str) -> Path:
    return ROOT / path


def read(path: str) -> str:
    return rel(path).read_text(encoding="utf-8")


def find_bash() -> Path | None:
    env_bash = os.environ.get("BASH")
    if env_bash:
        candidate = Path(env_bash)
        if candidate.exists() and candidate.is_file():
            return candidate
    for candidate in BASH_CANDIDATES:
        if candidate.exists() and candidate.is_file():
            return candidate
    found = shutil.which("bash")
    return Path(found) if found else None


BASH = find_bash()


def posix(path: Path) -> str:
    p = str(path.resolve()).replace("\\", "/")
    m = re.match(r"^([A-Za-z]):/(.*)$", p)
    if m:
        return f"/{m.group(1).lower()}/{m.group(2)}"
    return p


def shq(value: str) -> str:
    return "'" + value.replace("'", "'\"'\"'") + "'"


def run_bash(command: str, timeout: int = 30, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    if not BASH:
        raise RuntimeError("Git Bash/bash is not available")
    merged = os.environ.copy()
    if env:
        merged.update(env)
    return subprocess.run(
        [str(BASH), "-lc", command],
        cwd=str(ROOT),
        env=merged,
        text=True,
        capture_output=True,
        timeout=timeout,
    )


def write_executable(path: Path, body: str) -> None:
    path.write_text(body, encoding="utf-8", newline="\n")
    path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def require_contains(name: str, path: str, required: list[str], forbidden: list[str] | None = None) -> None:
    text = read(path)
    missing = [item for item in required if item not in text]
    present_forbidden = [item for item in (forbidden or []) if item in text]
    ok = not missing and not present_forbidden
    detail = []
    if missing:
        detail.append("missing: " + ", ".join(missing))
    if present_forbidden:
        detail.append("forbidden: " + ", ".join(present_forbidden))
    add(name, ok, "; ".join(detail) or path)


def test_built_snes_core_elf() -> None:
    path = rel("test-results/emulation-performance/pinned-cores/snes9x_2005_libretro.so")
    if not path.exists():
        add("built SNES core is present (run build_pinned_cores.sh first)", False, str(path))
        return

    data = path.read_bytes()
    if len(data) < 52:
        add("built SNES core is MIPS32r2 shared object", False, f"{path}: too small")
        return

    magic = data[:4]
    elf_class = data[4]
    endian = data[5]
    byteorder = "little" if endian == 1 else "big"
    e_type = int.from_bytes(data[16:18], byteorder)
    e_machine = int.from_bytes(data[18:20], byteorder)
    e_flags = int.from_bytes(data[36:40], byteorder)
    ok = (
        magic == b"\x7fELF"
        and elf_class == 1
        and endian == 1
        and e_type == 3
        and e_machine == 8
        and (e_flags & 0x70000000) == 0x70000000
        and path.stat().st_size > 512 * 1024
    )
    add(
        "built SNES core is MIPS32r2 shared object",
        ok,
        f"{path} size={path.stat().st_size} e_type={e_type} e_machine={e_machine} e_flags=0x{e_flags:08x}",
    )


def test_static_contracts() -> None:
    real_defconfigs = [
        "configs/lilsmoke_sf3000_defconfig",
        "configs/lilsmoke_gb350_defconfig",
    ]
    for cfg in real_defconfigs:
        require_contains(
            f"{cfg}: MIPS32r2/74Kc ABI stays inside target limits",
            cfg,
            [
                "BR2_mipsel=y",
                "BR2_mips_32r2=y",
                "# BR2_MIPS_NAN_2008 is not set",
                "# BR2_MIPS_SOFT_FLOAT is not set",
                "BR2_MIPS_DSP=y",
                "BR2_MIPS_DSPR2=y",
                'BR2_TARGET_OPTIMIZATION="-mtune=74kc -mdspr2 -mfp32 -mhard-float -mplt -mlong-calls"',
                "BR2_TOOLCHAIN_BUILDROOT_MUSL=y",
                "BR2_INIT_BUSYBOX=y",
                "BR2_ROOTFS_DEVICE_CREATION_DYNAMIC_MDEV=y",
                "BR2_PACKAGE_E2FSPROGS_MKE2FS=y",
                "BR2_PACKAGE_P7ZIP=y",
                "BR2_PACKAGE_P7ZIP_7ZA=y",
                "BR2_PACKAGE_UNRAR=y",
                "BR2_PACKAGE_WEBP=y",
                "BR2_PACKAGE_WEBP_DEMUX=y",
                "BR2_PACKAGE_PORTSMOKE=y",
            ],
            ["BR2_mips_xburst", "-mno-fused-madd", "BR2_SOFTFP", "BR2_MIPS_NAN_2008=y", "BR2_TARGET_UBOOT=y", "BR2_LINUX_KERNEL=y"],
        )
        require_contains(
            f"{cfg}: no GPU/window-system backend selected",
            cfg,
            [
                "BR2_PACKAGE_DIRECTFB=y",
                "BR2_PACKAGE_SDL2_DIRECTFB=y",
                "BR2_PACKAGE_DIRECTFB_X11=n",
                "BR2_PACKAGE_SDL2_OPENGLES=n",
                "BR2_PACKAGE_SDL2_OPENGL=n",
                "BR2_PACKAGE_SDL2_X11=n",
                "BR2_PACKAGE_SDL2_WAYLAND=n",
                "BR2_PACKAGE_SDL2_KMS=n",
            ],
        )

    require_contains(
        "SF3000 BSP manifest pins the researched HC16xx kernel and blockers",
        "board/sf3000/bsp-status.conf",
        [
            "SOC_FAMILY=hichip-hc16xx",
            "CPU=mips74kc-single-core",
            "STOCK_CPU_KHZ=1188000",
            "KERNEL_BRANCH=hc16xx-7.2",
            "KERNEL_COMMIT=da5f1a22ef2ff5c46aa3f750fa10825813970b09",
            "CPU_VALIDATED=yes",
            "INPUT_VALIDATED=yes",
            "DISPLAY_VALIDATED=no",
            "AUDIO_VALIDATED=no",
            "THERMAL_VALIDATED=no",
            "BOOT_VALIDATED=no",
        ],
    )
    require_contains(
        "GB350 remains explicitly unverified",
        "board/gb350/bsp-status.conf",
        ["SOC_FAMILY=unverified", "CPU_VALIDATED=no", "INPUT_VALIDATED=no", "BOOT_VALIDATED=no"],
    )

    for board in ("sf3000", "gb350"):
        panel_mode = "mode=854x480" if board == "sf3000" else "mode=640x480"
        require_contains(
            f"{board}: DirectFB uses the native panel framebuffer",
            f"board/{board}/directfbrc",
            ["fbdev=/dev/fb0", panel_mode, "depth=32"],
        )
        panel_width = "DISPLAY_WIDTH=854" if board == "sf3000" else "DISPLAY_WIDTH=640"
        require_contains(
            f"{board}: runtime defaults fit RAM/display budget",
            f"board/{board}/lilsmoke.conf",
            [
                panel_width,
                "DISPLAY_HEIGHT=480",
                'COVER_ART_SIZE="320x240"',
                "ZRAM_ENABLED=on",
                "ZRAM_SIZE_MB=48",
                "CPU_OVERCLOCK=stock",
                "CPU_STOCK_MAX_KHZ=1188000",
            ],
        )

    require_contains(
        "PCSX4ALL is MIPS-only and cannot leak into x86 test builds",
        "package/pcsx4all/Config.in",
        ["depends on BR2_mipsel", "default n"],
    )
    require_contains(
        "RetroArch disables GPU/window backends and gates MIPS flags",
        "package/retroarch-mips/retroarch-mips.mk",
        [
            "ifeq ($(BR2_mipsel),y)",
            "RETROARCH_MIPS_DEPENDENCIES = alsa-lib freetype libpng zlib libevdev sdl2",
            "-DHAVE_MIPS_DSP",
            "RETROARCH_MIPS_ARCH_CFLAGS += -mips32r2 -mtune=74kc -mdspr2 -mfp32",
            "package/retroarch-mips/build-cores.sh",
            "package/retroarch-mips/cores.lock",
            "--disable-opengl",
            "--disable-opengles",
            "--disable-vulkan",
            "--disable-x11",
            "--disable-wayland",
            "--disable-kms",
        ],
        ["LILSMOKE_PREBUILT_SNES9X2005_CORE"],
    )
    require_contains(
        "SNES core has a pinned revision and canonical installed name",
        "package/retroarch-mips/cores.lock",
        ["snes9x2005|https://github.com/libretro/snes9x2005.git|a79dfe9047e7fec58808aefe48ad2bf499c7af11|Makefile|snes9x2005_libretro.so|snes9x_2005_libretro.so"],
    )
    require_contains(
        "Settings exposes overclock profiles but defaults to stock",
        "package/lilsmoke-ui/src/src/SettingsView.cpp",
        ['{"CPU_OVERCLOCK",    "Overclocking",         {"stock", "1300MHz", "1400MHz"}}'],
    )
    require_contains(
        "7z and WebP support is wired through packages and UI code",
        "package/lilsmoke-ui/Config.in",
        [
            "select BR2_PACKAGE_SDL2_IMAGE",
            "select BR2_PACKAGE_WEBP",
            "select BR2_PACKAGE_WEBP_DEMUX",
            "select BR2_PACKAGE_P7ZIP_7ZA",
            "select BR2_PACKAGE_UNRAR",
            "PNG/WebP/JPEG",
        ],
    )
    require_contains(
        "Renderer requests SDL_image WebP decoding",
        "package/lilsmoke-ui/src/src/Renderer.cpp",
        ["IMG_INIT_WEBP", "loadImage", "WebP support unavailable"],
        ["loadPng"],
    )
    require_contains(
        "ROM scanner accepts 7z ROMs and WebP cover art",
        "package/lilsmoke-ui/src/src/ROMScanner.cpp",
        ['".7z"', '".webp"', "coverPathFor"],
    )
    require_contains(
        "launcher extracts non-arcade 7z ROMs to storage cache",
        "package/lilsmoke-scripts/src/lilsmoke-launch",
        ["ARCHIVE_CACHE_ROOT", "find_7z_tool", "extract_7z_rom", "archive_passthrough"],
    )
    require_contains(
        "LilSmoke Explorer is wired into the UI root menu",
        "package/lilsmoke-ui/src/src/main.cpp",
        ['#include "FileExplorerView.h"', '"LilSmoke Explorer"', 'std::make_unique<FileExplorerView>("/storage")'],
    )
    require_contains(
        "PortSmoke runtime is registered as a Buildroot package",
        "Config.in",
        [
            'source "$BR2_EXTERNAL_LILSMOKE_PATH/package/portsmoke/Config.in"',
            "select BR2_PACKAGE_PORTSMOKE",
        ],
    )
    require_contains(
        "PortSmoke package installs router and storage layout",
        "package/portsmoke/portsmoke.mk",
        [
            "$(TARGET_DIR)/usr/bin/portsmoke-launch",
            "$(TARGET_DIR)/storage/portsmoke/engines",
            "$(TARGET_DIR)/storage/portsmoke/ports",
            "$(TARGET_DIR)/storage/portsmoke/saves",
            "$(TARGET_DIR)/mnt/portsmoke",
        ],
    )
    require_contains(
        "PortSmoke router mounts .peng SquashFS engines and cleans up",
        "package/portsmoke/src/portsmoke-launch",
        [
            "conf_get EngineFile",
            'MOUNT_CMD="${PORTSMOKE_MOUNT_CMD:-mount}"',
            '"$MOUNT_CMD" -t squashfs -o loop,ro,nosuid,nodev',
            "trap cleanup EXIT",
            "trap 'exit 130' INT",
            "PORTSMOKE_ENGINE_DIR",
            'UMOUNT_CMD="${PORTSMOKE_UMOUNT_CMD:-umount}"',
            '"$UMOUNT_CMD" "$mount_dir"',
            'conf_get ABI "$conf_file"',
            'conf_get Libc "$conf_file"',
            'unsupported Libc: $libc',
        ],
    )
    require_contains(
        "PortSmoke recreates its layout after the FAT storage mount",
        "package/portsmoke/portsmoke.mk",
        ["S11portsmoke", "$(TARGET_DIR)/etc/init.d/S11portsmoke"],
    )
    require_contains(
        "PortSmoke game template supports FAT filesystems and software rendering",
        "package/portsmoke/examples/game_template.sh",
        ["SDL_RENDER_DRIVER", "mount --bind", "LIBGL_ALWAYS_SOFTWARE", "XDG_CONFIG_HOME"],
    )
    require_contains(
        "LilSmokeUI bounds decoded artwork and supports stock controller input",
        "package/lilsmoke-ui/src/src/Renderer.cpp",
        ["setTextureCacheLimitMB", "evictTextureCache", "textureCacheLimitBytes_"],
    )
    require_contains(
        "LilSmokeUI supports cubevol shared-memory input when evdev is absent",
        "package/lilsmoke-ui/src/src/Input.cpp",
        ["/tmp/joy_key", "shmget", "sharedBitButton"],
    )
    require_contains(
        "PortSmoke UI scanner uses C++17 filesystem and returns script/cover entries",
        "package/lilsmoke-ui/src/src/ROMScanner.cpp",
        [
            "#include <filesystem>",
            "std::vector<GameEntry> scanPorts",
            'rom.system = "portsmoke"',
            "game.scriptPath",
            "game.coverPath",
        ],
    )
    require_contains(
        "Launcher queues the selected backend for post-SDL teardown exec",
        "package/lilsmoke-ui/src/src/Launcher.cpp",
        [
            'rom_.system == "portsmoke"',
            'requestProcessLaunch({"/usr/bin/portsmoke-launch"',
            'requestProcessLaunch({"/usr/lib/lilsmoke/lilsmoke-launch"',
        ],
        ["fork()", "waitpid("],
    )
    require_contains(
        "LilSmoke Explorer source covers file operations and archive formats",
        "package/lilsmoke-ui/src/src/FileExplorerView.cpp",
        [
            "copyPath",
            "deletePath",
            "renameCurrent",
            "createFile",
            "createDir",
            "extractArchive",
            '".zip"',
            '".rar"',
            '".tar.gz"',
            '".7z"',
            "unrar x",
            "7za x",
        ],
    )
    require_contains(
        "LilSmoke Explorer source is included in standalone UI builds",
        "package/lilsmoke-ui/src/Makefile",
        ["src/FileExplorerView.cpp"],
    )
    require_contains(
        "QEMU profile models the single-core 74Kf framebuffer target",
        "scripts/run-qemu.sh",
        [
            'QEMU_CPU="${LILSMOKE_QEMU_CPU:-74Kf}"',
            '-cpu "$QEMU_CPU"',
            "-accel tcg,thread=single",
            "-smp 1",
            'QEMU_RAM="${LILSMOKE_QEMU_RAM:-256}"',
            '128|256)',
            '-m "$QEMU_RAM"',
            '-device usb-kbd',
            "-vga cirrus",
            "-device virtio-net-pci,netdev=net0",
            "-device AC97,audiodev=lilsmoke_audio",
            "video=cirrusfb:856x480M-24@60",
            "-display sdl,gl=off",
        ],
    )
    require_contains(
        "QEMU kernel exposes Cirrus framebuffer and keyboard input",
        "board/qemu-mipsel/linux.config",
        [
            "CONFIG_MIPS_MALTA=y",
            "CONFIG_FB_CIRRUS=y",
            "CONFIG_INPUT_KEYBOARD=y",
            "CONFIG_KEYBOARD_ATKBD=y",
            "CONFIG_SERIO_I8042=y",
        ],
        ["CONFIG_MACH_MALTA=y", "CONFIG_FB_VT8623=y"],
    )
    require_contains(
        "RetroArch QA path uses framebuffer-safe software/input drivers",
        "package/retroarch-mips/retroarch.cfg",
        [
            'video_driver = "sdl2"',
            'input_driver = "sdl2"',
            'input_joypad_driver = "linuxraw"',
            "video_threaded = false",
        ],
    )
    require_contains(
        "real-device builds are gated on verified HC16xx board support",
        "scripts/build.sh",
        ["check-real-hardware-bsp.sh", "LILSMOKE_ALLOW_UNVERIFIED_BSP"],
    )


def test_shell_syntax() -> None:
    if not BASH:
        add("shell syntax: skipped", False, "bash/Git Bash not found")
        return
    scripts = [
        "scripts/mk-sdcard.sh",
        "scripts/check-real-hardware-bsp.sh",
        "package/lilsmoke-scripts/src/lilsmoke-update",
        "package/lilsmoke-scripts/src/lilsmoke-scrape",
        "package/lilsmoke-scripts/src/lilsmoke-launch",
        "package/lilsmoke-scripts/src/lilsmoke-overclock",
        "package/lilsmoke-scripts/src/lilsmoke-zram",
        "package/lilsmoke-scripts/src/lilsmoke-hardware-probe",
        "package/portsmoke/src/portsmoke-launch",
        "package/portsmoke/src/S11portsmoke",
        "package/portsmoke/examples/game_template.sh",
    ]
    for init_dir in [
        rel("board/sf3000/init.d"),
        rel("board/gb350/init.d"),
        rel("board/qemu-mipsel/init.d"),
        rel("board/vbox-x86_64/init.d"),
    ]:
        scripts.extend(str(p.relative_to(ROOT)).replace("\\", "/") for p in sorted(init_dir.iterdir()) if p.is_file())
    failures = []
    for script in scripts:
        cp = run_bash(f"bash -n {shq(script)}", timeout=15)
        if cp.returncode != 0:
            failures.append(f"{script}: {cp.stderr or cp.stdout}")
    add("all boot/update shell scripts parse", not failures, "\n".join(failures) or f"{len(scripts)} scripts")


def test_sdcard_layout_dry_run() -> None:
    require_contains("two-partition SD builder and production gate", "scripts/mk-sdcard.sh",
                     ["--development", "mkfs.vfat -F 32 -n LILOS", "mkfs.exfat -L LILROMS", "size=260096"])
    require_contains("storage expansion preserves original filesystem through a verified backup",
                     "package/lilsmoke-scripts/src/lilsmoke-storage",
                     ["backup.sha256", "sha256sum -c", "journal.new", "diff -qr", "LILROMS belongs to USB host"])


def transformed_script(src: str, replacements: dict[str, str], dst: Path) -> Path:
    text = read(src)
    for old, new in replacements.items():
        text = text.replace(old, new)
    dst.write_text(text, encoding="utf-8", newline="\n")
    dst.chmod(dst.stat().st_mode | stat.S_IXUSR)
    return dst


def test_overclock_cpufreq_guard() -> None:
    if not BASH:
        add("overclock CPUFreq guard", False, "bash/Git Bash not found")
        return
    with tempfile.TemporaryDirectory(prefix="lilsmoke-oc-") as td_s:
        td = Path(td_s)
        cpu = td / "sys" / "devices" / "system" / "cpu" / "cpu0" / "cpufreq"
        thermal = td / "sys" / "class" / "thermal" / "thermal_zone0"
        cpu.mkdir(parents=True)
        thermal.mkdir(parents=True)
        for name, value in {
            "scaling_available_frequencies": "600000 1000000 1188000 1300000 1400000\n",
            "scaling_available_governors": "powersave ondemand performance\n",
            "scaling_governor": "powersave\n",
            "scaling_min_freq": "600000\n",
            "scaling_max_freq": "1188000\n",
        }.items():
            (cpu / name).write_text(value, encoding="utf-8")
        (thermal / "temp").write_text("45000\n", encoding="utf-8")
        default_cfg = td / "default.conf"
        persist_cfg = td / "persist.conf"
        default_cfg.write_text("", encoding="utf-8")
        persist_cfg.write_text(
            "CPU_GOVERNOR=ondemand\nCPU_OVERCLOCK=1400MHz\nCPU_STOCK_MAX_KHZ=1188000\n",
            encoding="utf-8",
        )
        script = transformed_script(
            "package/lilsmoke-scripts/src/lilsmoke-overclock",
            {
                'CPUFREQ_DIR="/sys/devices/system/cpu/cpu0/cpufreq"': f'CPUFREQ_DIR="{posix(cpu)}"',
                'THERMAL_DIR="/sys/class/thermal"': f'THERMAL_DIR="{posix(td / "sys" / "class" / "thermal")}"',
                'DEFAULT_CONFIG="/etc/lilsmoke/lilsmoke.conf.default"': f'DEFAULT_CONFIG="{posix(default_cfg)}"',
                'PERSIST_CONFIG="/persist/config/lilsmoke.conf"': f'PERSIST_CONFIG="{posix(persist_cfg)}"',
                'PIDFILE="/var/run/lilsmoke-overclock.pid"': f'PIDFILE="{posix(td / "overclock.pid")}"',
            },
            td / "overclock.sh",
        )
        cp_cool = run_bash(f"{shq(posix(script))} apply", timeout=60)
        cool_max = (cpu / "scaling_max_freq").read_text(encoding="utf-8").strip()
        cool_gov = (cpu / "scaling_governor").read_text(encoding="utf-8").strip()

        (thermal / "temp").write_text("83000\n", encoding="utf-8")
        cp_hot = run_bash(f"{shq(posix(script))} apply", timeout=60)
        hot_max = (cpu / "scaling_max_freq").read_text(encoding="utf-8").strip()

        ok = (
            cp_cool.returncode == 0
            and cp_hot.returncode == 0
            and cool_max == "1400000"
            and cool_gov == "ondemand"
            and hot_max == "1188000"
            and "above throttle limit" in cp_hot.stdout
        )
        add(
            "overclock applies only when CPUFreq table and thermal budget allow it",
            ok,
            f"cool stdout:\n{cp_cool.stdout}\nhot stdout:\n{cp_hot.stdout}\nmax cool={cool_max}, max hot={hot_max}, governor={cool_gov}",
        )


def test_zram_budget() -> None:
    if not BASH:
        add("zram budget script", False, "bash/Git Bash not found")
        return
    with tempfile.TemporaryDirectory(prefix="lilsmoke-zram-") as td_s:
        td = Path(td_s)
        sys_zram = td / "sys" / "block" / "zram0"
        sys_zram.mkdir(parents=True)
        (sys_zram / "comp_algorithm").write_text("lzo [lz4] zstd\n", encoding="utf-8")
        (sys_zram / "disksize").write_text("", encoding="utf-8")
        (sys_zram / "reset").write_text("", encoding="utf-8")
        dev_zram = td / "dev-zram0"
        dev_zram.write_text("", encoding="utf-8")
        cfg = td / "lilsmoke.conf"
        cfg.write_text("ZRAM_ENABLED=on\nZRAM_SIZE_MB=80\n", encoding="utf-8")
        default_cfg = td / "default.conf"
        default_cfg.write_text("", encoding="utf-8")
        swaps = td / "swaps"
        swaps.write_text("Filename Type Size Used Priority\n", encoding="utf-8")
        meminfo = td / "meminfo"
        meminfo.write_text("MemTotal:         131072 kB\n", encoding="utf-8")
        fakebin = td / "fakebin"
        fakebin.mkdir()
        log = td / "zram.log"
        for tool in ("mkswap", "swapon", "swapoff"):
            write_executable(fakebin / tool, f"#!/bin/sh\nprintf '%s\\n' \"{tool} $*\" >> {shq(posix(log))}\nexit 0\n")
        script = rel("package/lilsmoke-scripts/src/lilsmoke-zram")
        env = " ".join(
            [
                f"LILSMOKE_CONFIG={shq(posix(cfg))}",
                f"LILSMOKE_DEFAULT_CONFIG={shq(posix(default_cfg))}",
                f"LILSMOKE_ZRAM_DEVICE={shq(posix(dev_zram))}",
                f"LILSMOKE_ZRAM_SYS={shq(posix(sys_zram))}",
                f"LILSMOKE_PROC_SWAPS={shq(posix(swaps))}",
                f"LILSMOKE_PROC_MEMINFO={shq(posix(meminfo))}",
            ]
        )
        cp = run_bash(
            f"export PATH={shq(posix(fakebin))}:$PATH; {env} {shq(posix(script))} start",
            timeout=15,
        )
        algo = (sys_zram / "comp_algorithm").read_text(encoding="utf-8").strip()
        disksize = (sys_zram / "disksize").read_text(encoding="utf-8").strip()
        tool_log = log.read_text(encoding="utf-8") if log.exists() else ""
        ok = cp.returncode == 0 and algo == "lz4" and disksize == str(64 * 1024 * 1024) and "swapon -p 100" in tool_log
        add("optional zram clamps to half of 128 MB RAM and uses LZ4", ok, cp.stdout + cp.stderr + tool_log)


def test_real_bsp_gate() -> None:
    if not BASH:
        add("real-hardware BSP safety gate", False, "bash/Git Bash not found")
        return
    script = rel("scripts/check-real-hardware-bsp.sh")
    cp = run_bash(f"sh {shq(posix(script))} sf3000", timeout=15)
    ok = cp.returncode != 0 and "Refusing to create or advertise a flashable image" in cp.stderr
    add("real-hardware build refuses incomplete HC16xx capabilities", ok, cp.stdout + cp.stderr)


def test_portsmoke_router() -> None:
    if not BASH:
        add("PortSmoke router lifecycle", False, "bash/Git Bash not found")
        return
    with tempfile.TemporaryDirectory(prefix="lilsmoke-portsmoke-") as td_s:
        td = Path(td_s)
        root = td / "portsmoke"
        engine_dir = root / "engines"
        game_dir = root / "ports" / "demo"
        save_dir = root / "saves" / "demo"
        mount_root = td / "mnt"
        fakebin = td / "fakebin"
        for path in (engine_dir, game_dir, save_dir, mount_root, fakebin):
            path.mkdir(parents=True, exist_ok=True)

        (engine_dir / "demo.peng").write_bytes(b"hsqs-test")
        (game_dir / "portsmoke.conf").write_text(
            "[PortSmoke]\nName=Demo\nEngineFile=demo.peng\nScript=Demo.sh\nCover=cover.webp\n",
            encoding="utf-8",
        )
        result = td / "result.txt"
        write_executable(
            game_dir / "Demo.sh",
            "#!/bin/sh\n"
            "[ \"$PORTSMOKE_GAME\" = demo ] || exit 20\n"
            "[ -d \"$PORTSMOKE_ENGINE_DIR/bin\" ] || exit 21\n"
            "\"$PORTSMOKE_ENGINE_DIR/bin/demo-engine\"\n",
        )

        mounts = td / "mounts"
        mounts.write_text("", encoding="utf-8")
        log = td / "mount.log"
        engine_program = td / "demo-engine"
        write_executable(
            engine_program,
            f"#!/bin/sh\nprintf '%s\\n' \"$PORTSMOKE_SAVE_DIR\" > {shq(posix(result))}\n",
        )
        write_executable(
            fakebin / "mount",
            "#!/bin/sh\n"
            f"printf '%s\\n' \"mount $*\" >> {shq(posix(log))}\n"
            "mkdir -p \"$6/bin\" || exit 1\n"
            f"cp {shq(posix(engine_program))} \"$6/bin/demo-engine\" || exit 1\n"
            "chmod +x \"$6/bin/demo-engine\"\n"
            f"printf '%s\\n' \"loop0 $6 squashfs ro,nosuid,nodev 0 0\" > {shq(posix(mounts))}\n",
        )
        write_executable(
            fakebin / "umount",
            "#!/bin/sh\n"
            f"printf '%s\\n' \"umount $*\" >> {shq(posix(log))}\n"
            "rm -f \"$1/bin/demo-engine\"\n"
            "rmdir \"$1/bin\" 2>/dev/null || true\n"
            f": > {shq(posix(mounts))}\n",
        )

        router = rel("package/portsmoke/src/portsmoke-launch")
        env = " ".join(
            [
                f"PORTSMOKE_ROOT={shq(posix(root))}",
                f"PORTSMOKE_MOUNT_ROOT={shq(posix(mount_root))}",
                f"PORTSMOKE_MOUNT_CMD={shq(posix(fakebin / 'mount'))}",
                f"PORTSMOKE_UMOUNT_CMD={shq(posix(fakebin / 'umount'))}",
                f"PORTSMOKE_MOUNTS_FILE={shq(posix(mounts))}",
            ]
        )
        cp = run_bash(f"{env} sh {shq(posix(router))} demo", timeout=20)
        tool_log = log.read_text(encoding="utf-8") if log.exists() else ""
        result_text = result.read_text(encoding="utf-8").strip() if result.exists() else ""
        ok = (
            cp.returncode == 0
            and "-o loop,ro,nosuid,nodev" in tool_log
            and "umount " in tool_log
            and result_text == posix(save_dir)
            and not (mount_root / "demo").exists()
        )
        add("PortSmoke mounts read-only, runs the port, and always unmounts", ok, cp.stdout + cp.stderr + tool_log)


def test_portsmoke_fat_save_fallback() -> None:
    if not BASH:
        add("PortSmoke FAT save fallback", False, "bash/Git Bash not found")
        return
    with tempfile.TemporaryDirectory(prefix="lilsmoke-port-save-") as td_s:
        td = Path(td_s)
        game = td / "game"
        engine = td / "engine"
        saves = td / "saves"
        runtime = td / "runtime"
        fakebin = td / "fakebin"
        for path in (game, engine / "bin", saves, runtime, fakebin):
            path.mkdir(parents=True, exist_ok=True)
        log = td / "bind.log"
        result = td / "result.txt"
        write_executable(fakebin / "ln", "#!/bin/sh\nexit 1\n")
        write_executable(
            fakebin / "mount",
            f"#!/bin/sh\nprintf '%s\\n' \"mount $*\" >> {shq(posix(log))}\nexit 0\n",
        )
        write_executable(
            fakebin / "umount",
            f"#!/bin/sh\nprintf '%s\\n' \"umount $*\" >> {shq(posix(log))}\nexit 0\n",
        )
        write_executable(
            engine / "bin" / "example-engine",
            f"#!/bin/sh\nprintf '%s\\n' \"$HOME|$XDG_CONFIG_HOME|$SDL_RENDER_DRIVER\" > {shq(posix(result))}\n",
        )
        template = rel("package/portsmoke/examples/game_template.sh")
        env = " ".join(
            [
                f"PORTSMOKE_GAME_DIR={shq(posix(game))}",
                f"PORTSMOKE_ENGINE_DIR={shq(posix(engine))}",
                f"PORTSMOKE_SAVE_DIR={shq(posix(saves))}",
                f"PORTSMOKE_RUNTIME_DIR={shq(posix(runtime))}",
            ]
        )
        cp = run_bash(
            f"export PATH={shq(posix(fakebin))}:$PATH; {env} sh {shq(posix(template))}",
            timeout=20,
        )
        tool_log = log.read_text(encoding="utf-8") if log.exists() else ""
        result_text = result.read_text(encoding="utf-8").strip() if result.exists() else ""
        ok = (
            cp.returncode == 0
            and "mount --bind" in tool_log
            and "umount " in tool_log
            and result_text == f"{posix(saves)}/home|{posix(saves)}/config|software"
        )
        add("PortSmoke uses and cleans a bind mount when FAT rejects symlinks", ok, cp.stdout + cp.stderr + tool_log)


def test_network_boolean_compat() -> None:
    if not BASH:
        add("WiFi/SSH boolean compatibility", False, "bash/Git Bash not found")
        return
    with tempfile.TemporaryDirectory(prefix="lilsmoke-net-") as td_s:
        td = Path(td_s)
        conf = td / "lilsmoke.conf"
        wifi = td / "wifi.conf"
        netdir = td / "net"
        varlog = td / "varlog"
        fakebin = td / "fakebin"
        fakebin.mkdir()
        netdir.mkdir()
        varlog.mkdir()
        (netdir / "wlan0" / "wireless").mkdir(parents=True)
        conf.write_text("WIFI_ENABLED=on\nSSH_ENABLED=on\n", encoding="utf-8")
        wifi.write_text("network={}\n", encoding="utf-8")
        log = td / "net.log"
        write_executable(
            fakebin / "ip",
            f"#!/bin/sh\nprintf '%s\\n' \"ip $*\" >> {shq(posix(log))}\n"
            "if [ \"$1 $2 $3\" = \"link show eth0\" ]; then exit 1; fi\n"
            "if [ \"$1 $2\" = \"addr show\" ]; then echo 'inet 192.168.1.7/24'; exit 0; fi\n"
            "exit 0\n",
        )
        for tool in ("wpa_supplicant", "udhcpc", "start-stop-daemon", "id", "chown"):
            write_executable(fakebin / tool, f"#!/bin/sh\nprintf '%s\\n' \"{tool} $*\" >> {shq(posix(log))}\nexit 0\n")
        s20 = transformed_script(
            "package/lilsmoke-scripts/src/lilsmoke-network",
            {
                "/persist/config/lilsmoke.conf": posix(conf),
                "/persist/config/wifi.conf": posix(wifi),
                "/sys/class/net": posix(netdir),
            },
            td / "S20network",
        )
        s30 = transformed_script(
            "package/lilsmoke-scripts/src/lilsmoke-sshd",
            {
                "/persist/config/lilsmoke.conf": posix(conf),
                "/var/log": posix(varlog),
            },
            td / "S30dropbear",
        )
        write_executable(fakebin / "dropbearkey",
            '#!/bin/sh\nwhile [ "$#" -gt 0 ]; do\n'
            'if [ "$1" = -f ]; then shift; printf test > "$1"; exit 0; fi\n'
            'shift\ndone\nexit 1\n')
        reader = ROOT / "package/lilsmoke-scripts/src/lilsmoke-config-read"
        env = {
            "LILSMOKE_CONFIG_READER": posix(reader),
            "LILSMOKE_CONFIG": posix(conf),
            "LILSMOKE_DEFAULT_CONFIG": posix(td / "absent"),
            "LILSMOKE_PERSIST_ROOT": posix(td / "persist"),
            "LILSMOKE_NETWORK_RUN_DIR": posix(td / "run-network"),
            "LILSMOKE_SSH_RUN_DIR": posix(td / "run-sshd"),
        }
        exports = " ".join(key + "=" + shq(value) for key, value in env.items())
        cmd = f"export PATH={shq(posix(fakebin))}:$PATH {exports}; {shq(posix(s20))} start && {shq(posix(s30))} start"
        cp = run_bash(cmd, timeout=15)
        tool_log = log.read_text(encoding="utf-8") if log.exists() else ""
        ok = (
            cp.returncode == 0
            and "wpa_supplicant -B -P" in tool_log
            and "udhcpc -i wlan0" in tool_log
            and "/usr/sbin/dropbear -- -E -w -p 22" in tool_log
        )
        add("settings value 'on' enables WiFi and SSH init scripts", ok, cp.stdout + cp.stderr + tool_log)


def test_7z_launcher_cache() -> None:
    if not BASH:
        add("7z launcher cache", False, "bash/Git Bash not found")
        return
    with tempfile.TemporaryDirectory(prefix="lilsmoke-7z-") as td_s:
        td = Path(td_s)
        fakebin = td / "fakebin"
        fakebin.mkdir()
        cores = td / "cores"
        cores.mkdir()
        saves = td / "saves"
        states = td / "states"
        cache = td / "archive-cache"
        roms = td / "roms"
        roms.mkdir()
        archive = roms / "Test Game.7z"
        archive.write_text("fake archive\n", encoding="utf-8")
        (cores / "snes9x_2005_libretro.so").write_text("core\n", encoding="utf-8")
        log = td / "launch.log"

        write_executable(
            fakebin / "7zr",
            f"""#!/bin/sh
printf '%s\\n' "7zr $*" >> {shq(posix(log))}
out=""
for arg in "$@"; do
    case "$arg" in
        -o*) out="${{arg#-o}}" ;;
    esac
done
[ -n "$out" ] || exit 9
mkdir -p "$out"
printf 'rom\\n' > "$out/Test Game.sfc"
exit 0
""",
        )
        write_executable(
            fakebin / "7za",
            f"""#!/bin/sh
printf '%s\\n' "7za $*" >> {shq(posix(log))}
out=""
for arg in "$@"; do
    case "$arg" in
        -o*) out="${{arg#-o}}" ;;
    esac
done
[ -n "$out" ] || exit 9
mkdir -p "$out"
printf 'rom\\n' > "$out/Test Game.sfc"
exit 0
""",
        )
        backend = fakebin / "lilsmoke-retroarch"
        write_executable(
            backend,
            f"""#!/bin/sh
printf 'backend' >> {shq(posix(log))}
for arg in "$@"; do
    printf ' [%s]' "$arg" >> {shq(posix(log))}
done
printf '\\n' >> {shq(posix(log))}
exit 0
""",
        )

        script = transformed_script(
            "package/lilsmoke-scripts/src/lilsmoke-launch",
            {
                'SAVE_DIR="/persist/saves"': f'SAVE_DIR="{posix(saves)}"',
                'STATE_DIR="/persist/states"': f'STATE_DIR="{posix(states)}"',
                'exec /usr/lib/lilsmoke/lilsmoke-retroarch run "$SYSTEM" "$ROM_PATH"':
                    f'exec {shq(posix(backend))} run "$SYSTEM" "$ROM_PATH"',
            },
            td / "lilsmoke-launch",
        )
        cmd = (
            f"export PATH={shq(posix(fakebin))}:$PATH; "
            f"export LILSMOKE_ARCHIVE_CACHE={shq(posix(cache))}; "
            f"{shq(posix(script))} snes {shq(posix(archive))}; "
            f"{shq(posix(script))} snes {shq(posix(archive))}"
        )
        cp = run_bash(cmd, timeout=30)
        tool_log = log.read_text(encoding="utf-8") if log.exists() else ""
        backend_lines = [line for line in tool_log.splitlines() if line.startswith("backend")]
        ok = (
            cp.returncode == 0
            and tool_log.count("7za x -y -o") == 0
            and len(backend_lines) == 2
            and "Test Game.7z" in tool_log
            and "[run] [snes]" in tool_log
        )
        add("launcher delegates original 7z path without duplicate extraction", ok, cp.stdout + cp.stderr + tool_log)


def test_ota_install_flow() -> None:
    script = read("package/lilsmoke-scripts/src/lilsmoke-update")
    add("legacy updater cannot format LILROMS or activate an unverified boot slot",
        "mke2fs" not in script and "TARGET_PART" not in script and
        "OTA installation disabled" in script and "Boot-slot activation disabled" in script,
        "Old partition-based installer removed; safe failure until a boot adapter exists.")


def write_report() -> None:
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    passed = sum(1 for r in RESULTS if r.ok)
    failed = len(RESULTS) - passed
    lines = [
        "# LilSmokeOS hardware-faithful smoke test",
        "",
        f"Project: `{ROOT}`",
        f"Git Bash: `{BASH}`" if BASH else "Git Bash: not found",
        "",
        f"Result: {passed} passed, {failed} failed",
        "",
        "| Status | Test | Detail |",
        "|---|---|---|",
    ]
    for result in RESULTS:
        status_txt = "PASS" if result.ok else "FAIL"
        detail = result.detail.replace("\r", "").replace("\n", "<br>")
        if len(detail) > 1200:
            detail = detail[:1200] + "..."
        lines.append(f"| {status_txt} | {result.name} | {detail} |")
    lines.extend(
        [
            "",
            "## Fidelity notes",
            "",
            "- These tests use the real project scripts/configs, but mock block devices, CPUFreq, thermal, and network interfaces so they can run without root/admin.",
            "- These host-side checks complement the separate WSL Buildroot/QEMU boot test; they do not claim to emulate the still-missing HC16xx display/audio/thermal BSP drivers.",
            "- The QEMU profile uses one emulated MIPS 74Kf CPU, 256 MB RAM, Cirrus framebuffer, BusyBox, musl, DirectFB, and SDL2 software rendering.",
        ]
    )
    REPORT.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    test_built_snes_core_elf()
    test_static_contracts()
    test_shell_syntax()
    test_overclock_cpufreq_guard()
    test_zram_budget()
    test_real_bsp_gate()
    test_portsmoke_router()
    test_portsmoke_fat_save_fallback()
    test_network_boolean_compat()
    test_7z_launcher_cache()
    write_report()

    passed = sum(1 for r in RESULTS if r.ok)
    failed = len(RESULTS) - passed
    for result in RESULTS:
        print(("PASS" if result.ok else "FAIL") + " - " + result.name)
        if not result.ok and result.detail:
            print(result.detail)
    print(f"\n{passed} passed, {failed} failed")
    print(f"report: {REPORT}")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
