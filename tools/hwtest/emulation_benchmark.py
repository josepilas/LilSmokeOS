#!/usr/bin/env python3
"""Fixed-work MIPS diagnostics using the same Buildroot kernel/rootfs as captures.

Runs under Linux/WSL. This measures QEMU TCG throughput, not console MHz.
No input is sent to the host desktop. Existing ROMs/saves are never modified.
"""
import argparse
import gzip
import hashlib
import json
import re
import signal
from pathlib import Path
import shutil
import socket
import subprocess
import time
from mips_boot_smoke import Newc

ROOT = Path(__file__).resolve().parents[2]

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--build", type=Path, default=Path("/root/lilsmoke-os/build/buildroot/output"))
    p.add_argument("--qemu", default="qemu-system-mipsel")
    p.add_argument("--cpu", choices=["74Kf","74Kf-lilsmoke"], default="74Kf")
    p.add_argument("--rom", required=True, type=Path)
    p.add_argument("--out", required=True, type=Path)
    p.add_argument("--frames", type=int, default=600)
    p.add_argument("--core", type=Path)
    p.add_argument("--retroarch", type=Path)
    p.add_argument("--sdl", type=Path)
    p.add_argument("--directfb-input", type=Path)
    p.add_argument("--state", type=Path)
    p.add_argument("--ram", type=int, choices=[128, 256], default=256)
    p.add_argument("--bpp", type=int, choices=[16,24,32], default=24)
    p.add_argument("--tb-size", type=int, choices=[32,64,128,256], help="host JIT cache MiB, not guest RAM/clock")
    p.add_argument("--host-perf", type=Path, help="perf binary; diagnostic sampling invalidates timing comparisons")
    p.add_argument("--profile", action="store_true")
    p.add_argument("--verify", action="store_true")
    p.add_argument("--direct-core", action="store_true", help="skip diagnostic callback proxy")
    p.add_argument("--selftest", action="store_true", help="SNES title-screen save roundtrip and Start response")
    p.add_argument("--cases", default="null,software,directfb,software-audio,software-alsa")
    p.add_argument("--timeout", type=int, default=1500)
    a = p.parse_args()
    out = a.out.resolve(); out.mkdir(parents=True, exist_ok=True)
    if not 1 <= a.frames <= 100000: p.error("frames out of range")
    shutil.copyfile(a.rom, out / "game.sfc")
    if a.state: shutil.copyfile(a.state, out / "game.state.auto")
    core = a.core or a.build / "target/usr/lib/libretro/snes9x_2005_libretro.so"
    ra = a.retroarch or a.build / "target/usr/bin/retroarch"
    shutil.copyfile(core, out / "core.so")
    shutil.copyfile(ra, out / "retroarch")
    cc = a.build / "host/bin/mipsel-buildroot-linux-musl-gcc"
    subprocess.run([str(cc), "-O2", "-mips32r2", "-mfp32", "-mhard-float",
                    "-I" + str(ROOT / "package/retroarch-mips/cores/snes9x2005/libretro-common/include"),
                    str(ROOT / "tools/hwtest/core_benchmark.c"), "-ldl", "-o", str(out / "core-benchmark")], check=True)
    subprocess.run([str(cc), "-O2", "-fPIC", "-shared", "-I" + str(ROOT / "package/retroarch-mips/cores/snes9x2005/libretro-common/include"),
                    str(ROOT / "tools/hwtest/core_profile.c"), "-ldl", "-o", str(out / "core-profile.so")], check=True)
    if a.profile:
        subprocess.run([str(cc), "-O2", "-fPIC", "-shared", "-I" + str(a.build / "host/mipsel-buildroot-linux-musl/sysroot/usr/include/SDL2"),
                        str(ROOT / "tools/hwtest/sdl_profile.c"), "-lSDL2", "-ldl", "-o", str(out / "profile.so")], check=True)
    base = {
        "video_driver": "sdl2", "audio_driver": "null", "input_driver": "null",
        "input_joypad_driver": "null", "menu_driver": "rgui",
        "input_max_users": 1, "libretro_info_path": "/usr/share/libretro/info", "core_info_cache_enable": False,
        "video_fullscreen": True, "video_windowed_fullscreen": True,
        "video_fullscreen_x": 856, "video_fullscreen_y": 480,
        "video_scale_integer": True, "video_smooth": False,
        "video_vsync": False, "video_threaded": False, "audio_sync": False,
        "video_font_enable": False, "fps_show": False,
        "audio_enable": False, "audio_out_rate": 44100, "audio_latency": 64,
        "audio_resampler": "sinc", "audio_resampler_quality": 2,
        "rewind_enable": False, "run_ahead_enabled": False,
        "config_save_on_exit": False, "pause_nonactive": False,
        "savestate_auto_load": bool(a.state), "savestate_auto_save": False,
        "sort_savefiles_enable": False, "sort_savestates_enable": False,
        "savefile_directory": "/bench/saves", "savestate_directory": "/bench",
        "core_options_path": "/bench/core-options.cfg", "system_directory": "/bench/bios",
        "log_verbosity": True, "history_list_enable": False,
        "gamemode_enable": False, "menu_enable_widgets": False,
        "video_shader_enable": False, "video_filter": "", "audio_dsp_plugin": "",
        "preemptive_frames_enable": False, "slowmotion_ratio": 1.0,
        "game_specific_options": False, "auto_overrides_enable": False,
        "microphone_enable": False,
    }
    variants = {
        "null": {"video_driver": "null"}, "core": {},
        "software": {}, "directfb": {}, "dummy": {},
        "software-audio": {"audio_enable": True},
        "software-alsa": {"audio_enable": True, "audio_driver": "alsa", "audio_sync": True, "video_vsync": True},
        "software-alsa-input": {"audio_enable": True, "audio_driver": "alsa", "audio_sync": True, "video_vsync": True,
                                "input_driver": "sdl2", "input_joypad_driver": "linuxraw"},
        "software-alsa-ui": {"audio_enable": True, "audio_driver": "alsa", "audio_sync": True, "video_vsync": True,
                             "input_driver": "sdl2", "input_joypad_driver": "linuxraw", "video_font_enable": True, "fps_show": True},
        "software-alsa-low-sinc": {"audio_enable": True, "audio_driver": "alsa", "audio_sync": True, "video_vsync": True, "audio_resampler_quality": 0},
        "software-alsa-cc": {"audio_enable": True, "audio_driver": "alsa", "audio_sync": True, "video_vsync": True, "audio_resampler": "CC"},
        "null-alsa": {"video_driver": "null", "audio_enable": True, "audio_driver": "alsa", "audio_sync": True},
        "directfb-alsa": {"audio_enable": True, "audio_driver": "alsa", "audio_sync": True, "video_vsync": True},
        "software-low-sinc": {"audio_enable": True, "audio_resampler_quality": 0},
        "software-cc": {"audio_enable": True, "audio_resampler": "CC"},
    }
    cases = a.cases.split(",")
    if any(c not in variants for c in cases): p.error("unknown case")
    (out / "core-options.cfg").write_text('snes9x_2005_frameskip = "disabled"\n')
    def cfg_value(v):
        if isinstance(v, bool): return "true" if v else "false"
        return json.dumps(v)
    for case in cases:
        cfg = base | variants[case]
        (out / (case + ".cfg")).write_text("".join(f"{k} = {cfg_value(v)}\n" for k, v in cfg.items()))
    script = '''#!/bin/sh
exec </dev/console >/dev/console 2>&1
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs dev /dev
mount -t debugfs debug /sys/kernel/debug
mkdir -p /bench /tmp /persist /storage /dev/pts
mount -t devpts devpts /dev/pts
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || { echo BENCH_MOUNT_FAILED; poweroff -f; }
mkdir -p /bench/saves /bench/bios
cp /bench/retroarch /tmp/retroarch
cp /bench/core.so /tmp/actual-core.so
cp /bench/core-profile.so /tmp/core-profile.so
cp /bench/game.sfc /tmp/game.sfc
chmod +x /tmp/retroarch
export HOME=/bench SDL_VIDEODRIVER=directfb SDL_AUDIODRIVER=alsa
export DFBARGS=no-hardware,no-sighandler,quiet,vt,no-vt-switch,no-vt-switching,disable-module=keyboard,linux-input-force
unset DISPLAY
echo BENCH_BEGIN
cat /proc/cpuinfo
cat /proc/meminfo
ps
find /sys/kernel/debug/mips -type f -maxdepth 2 2>/dev/null
cd /bench
'''
    for case in cases:
        renderer = "directfb" if case.startswith("directfb") else "software"
        driver = "dummy" if case == "dummy" else "directfb"
        preload = "LD_PRELOAD=/bench/profile.so" if a.profile else ""
        verification = "LILSMOKE_VERIFY=1" if a.verify else ""
        run_core = "/tmp/actual-core.so" if a.direct_core else "/tmp/core-profile.so"
        if case == "core":
            script += f'''echo CASE_BEGIN_core
/bench/core-benchmark /tmp/actual-core.so /tmp/game.sfc {a.frames} {int(a.verify)} {('/bench/game.state.auto' if a.state else '-')} /bench/final.state {'selftest' if a.selftest else ''} >/bench/core.log 2>&1
rc=$?
cat /bench/core.log
echo CORE_EXIT=$rc
'''
            continue
        script += f'''echo CASE_BEGIN_{case}
start=$(cut -d' ' -f1 /proc/uptime)
{preload} {verification} SDL_VIDEODRIVER={driver} SDL_RENDER_DRIVER={renderer} /tmp/retroarch --verbose --max-frames={a.frames} -L {run_core} --config /bench/{case}.cfg /tmp/game.sfc >/bench/{case}.log 2>&1
rc=$?
end=$(cut -d' ' -f1 /proc/uptime)
awk -v start="$start" -v end="$end" -v rc="$rc" 'BEGIN {{rate = (rc == 0 && end > start) ? {a.frames}/(end-start) : 0; printf "CASE_RESULT name={case} frames={a.frames} seconds=%.3f fps=%.3f rc=%d\\n", end-start, rate, rc}}'
grep CORE_PROFILE /bench/{case}.log
cat /proc/meminfo | head -4
ps
cat /sys/kernel/debug/mips/unaligned_instructions 2>/dev/null
'''
    script += "echo BENCH_END\nsync\npoweroff -f\n"
    # This kernel appends rdinit=/init via CONFIG_CMDLINE. Replace that entry
    # in the diagnostic overlay instead of relying on command-line ordering.
    overlay = Newc(); overlay.file("init", script, 0o755)
    overlay.dir("usr/share/libretro")
    overlay.dir("usr/share/libretro/info")
    if a.sdl:
        # The production symlink resolves to this exact Buildroot SONAME file.
        overlay.file("usr/lib/libSDL2-2.0.so.0.2800.5", a.sdl.read_bytes(), 0o755)
    overlay.file("etc/directfbrc", (ROOT / "board/qemu-mipsel/directfbrc").read_bytes())
    overlay.file("usr/share/libretro/info/snes9x_2005_libretro.info",
                 (ROOT / "package/retroarch-mips/core-info/snes9x2005_libretro.info").read_bytes())
    if a.directfb_input:
        overlay.file("usr/lib/directfb-1.7-7/inputdrivers/libdirectfb_linux_input.so", a.directfb_input.read_bytes(), 0o755)
    initrd = out / "benchmark.cpio.gz"
    with initrd.open("wb") as f:
        f.write((a.build / "images/rootfs.cpio.gz").read_bytes())
        f.write(gzip.compress(overlay.build()))
    qmp_path=out/"benchmark-qmp.sock"
    accelerator="tcg,thread=single" + (f",tb-size={a.tb_size}" if a.tb_size else "")
    command = [a.qemu, "-M", "malta", "-cpu", a.cpu, "-smp", "1",
               "-accel", accelerator, "-m", str(a.ram), "-kernel", str(a.build / "images/vmlinux"),
               "-initrd", str(initrd), "-append", f"console=ttyS0 video=cirrusfb:856x480M-{a.bpp}@60 panic=-1",
               "-vga", "cirrus", "-display", "none", "-serial", "stdio", "-monitor", "none", "-no-reboot",
               "-qmp", f"unix:{qmp_path},server=on,wait=off", "-device", "usb-kbd",
               "-audiodev", "driver=none,id=audio", "-device", "AC97,audiodev=audio",
               "-virtfs", f"local,path={out},mount_tag=bench,security_model=none"]
    if a.host_perf: command.append("-perfmap")
    metadata = {"command": command, "rom_sha256": hashlib.sha256(a.rom.read_bytes()).hexdigest(),
                "qemu_sha256": hashlib.sha256(Path(shutil.which(a.qemu) or a.qemu).read_bytes()).hexdigest(),
                "qemu_version": subprocess.check_output([a.qemu,"--version"],text=True).splitlines()[0],
                "kernel_sha256": hashlib.sha256((a.build / "images/vmlinux").read_bytes()).hexdigest(),
                "rootfs_sha256": hashlib.sha256((a.build / "images/rootfs.cpio.gz").read_bytes()).hexdigest(),
                "core_sha256": hashlib.sha256(core.read_bytes()).hexdigest(),
                "retroarch_sha256": hashlib.sha256(ra.read_bytes()).hexdigest(), "frames": a.frames,
                "sdl_sha256": hashlib.sha256(a.sdl.read_bytes()).hexdigest() if a.sdl else None,
                "directfb_input_sha256": hashlib.sha256(a.directfb_input.read_bytes()).hexdigest() if a.directfb_input else None,
                "verify": a.verify, "sdl_profile": a.profile, "direct_core": a.direct_core,
                "host_perf": str(a.host_perf) if a.host_perf else None,
                "notice": "TCG diagnostic throughput, not physical console FPS. Process metrics include startup; steady metrics exclude it."}
    (out / "manifest.json").write_text(json.dumps(metadata, indent=2))
    def jit_stats():
        with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as s:
            s.settimeout(10); s.connect(str(qmp_path))
            with s.makefile("rwb",buffering=0) as f:
                json.loads(f.readline())
                for request in ({"execute":"qmp_capabilities"},{"execute":"human-monitor-command","arguments":{"command-line":"info jit"}}):
                    f.write(json.dumps(request).encode()+b"\n")
                    while True:
                        reply=json.loads(f.readline())
                        if "error" in reply: raise RuntimeError(reply)
                        if "return" in reply: break
                return reply["return"]
    started = time.monotonic()
    # Malta's poweroff can halt without terminating QEMU. Observe our own
    # completion marker, then reap only this test's VM.
    with (out / "serial.log").open("wb") as f:
        proc = subprocess.Popen(command, stdout=f, stderr=subprocess.STDOUT)
        perf = None
        try:
            while proc.poll() is None:
                try:
                    log = (out / "serial.log").read_text(errors="replace")
                except OSError:
                    # WSL's Windows-mounted filesystem can transiently return
                    # ENODATA while the VM appends. This is not a guest failure.
                    log = ""
                if a.host_perf and perf is None and "CASE_BEGIN_" in log:
                    # Attach after QEMU created its vCPU thread, not merely the
                    # initial I/O thread. Sampling only that thread misses TCG.
                    tids=",".join(t.name for t in Path(f"/proc/{proc.pid}/task").iterdir())
                    perf = subprocess.Popen([str(a.host_perf), "record", "-e", "cpu-clock", "-F", "99", "-g",
                        "-t", tids, "-o", str(out/"host-perf.data")], stdout=f, stderr=subprocess.STDOUT)
                if "BENCH_END" in log:
                    (out/"jit-stats.txt").write_text(jit_stats())
                    time.sleep(1)
                    break
                if "Kernel panic" in log or "Out of memory: Killed process" in log:
                    raise RuntimeError("guest boot/runtime failure; inspect serial.log")
                if time.monotonic() - started > a.timeout:
                    raise TimeoutError("benchmark timeout")
                time.sleep(1)
        finally:
            if perf:
                if perf.poll() is None: perf.send_signal(signal.SIGINT)
                try: perf.wait(timeout=10)
                except subprocess.TimeoutExpired: perf.kill(); perf.wait()
                perfmap=Path(f"/tmp/perf-{proc.pid}.map")
                if perfmap.exists(): shutil.copyfile(perfmap,out/perfmap.name)
                with (out/"host-perf-report.txt").open("w") as report:
                    subprocess.run([str(a.host_perf), "report", "-i", str(out/"host-perf.data"),
                        "--stdio", "--no-children", "--sort", "symbol,dso", "--percent-limit", "0.5"], stdout=report, stderr=subprocess.STDOUT)
            if proc.poll() is None: proc.terminate()
            try: proc.wait(timeout=10)
            except subprocess.TimeoutExpired: proc.kill(); proc.wait()
    log = (out / "serial.log").read_text(errors="replace")
    print("\n".join(line for line in log.splitlines() if "CASE_" in line or "BENCH_" in line), flush=True)
    print(f"Host elapsed: {time.monotonic()-started:.2f}s", flush=True)
    results = re.findall(r"CASE_RESULT name=(\S+) frames=(\d+) seconds=([\d.]+) fps=([\d.]+) rc=(\d+)", log)
    parsed=[]
    for n,f,s,fps,rc in results:
        row=dict(name=n, frames=int(f), process_seconds=float(s), process_fps=float(fps), rc=int(rc))
        case_log=(out/(n+".log")).read_text(errors="replace")
        for label in ("CORE_PROFILE", "CORE_USAGE", "CORE_HASH", "PROFILE"):
            matches=re.findall(r"^"+label+r" (.+)$",case_log,re.M)
            if matches:
                row[label.lower()]={key:value for match in matches for key,value in re.findall(r"(\w+)=(\S+)",match)}
        actual_frames = row.get('core_profile', {}).get('frames')
        row['completed_frames'] = int(actual_frames) if actual_frames is not None else None
        if int(rc) != 0 or (actual_frames is not None and int(actual_frames) != int(f)):
            # A failed startup did not emulate the requested workload. Never
            # publish requested-frames/failed-startup-time as throughput.
            row['process_fps'] = None
        parsed.append(row)
    if "core" in cases:
        parsed.append(dict(name="core",log=(out/"core.log").read_text(errors="replace")))
    (out / "results.json").write_text(json.dumps(parsed, indent=2))
    if "BENCH_END" not in log or len(results) != len([c for c in cases if c != "core"]) or any(int(r[-1]) for r in results) or ("core" in cases and "CORE_EXIT=0" not in log):
        raise SystemExit("benchmark incomplete; inspect logs")

if __name__ == "__main__": main()
