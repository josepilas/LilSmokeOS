#!/usr/bin/env python3
"""BusyBox launcher regression tests with an isolated fake RetroArch boundary."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
checks = 0
def check(value, name):
    global checks
    if not value: raise AssertionError(name)
    checks += 1
    print("PASS", name)

with tempfile.TemporaryDirectory(prefix="lilsmoke-perf-config-") as tmp:
    t = Path(tmp)
    (t / "bin").mkdir(); (t / "cores").mkdir()
    (t / "cores/snes9x_2005_libretro.so").touch()
    fake = t / "bin/retroarch"
    fake.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$ARGS"\nprintf "%s\\n" "$SDL_VIDEODRIVER:$SDL_RENDER_DRIVER:$SDL_AUDIODRIVER:${DISPLAY-unset}" > "$ENV_LOG"\nprintf "%s\\n" "$DFBARGS" > "$DFB_LOG"\n')
    fake.chmod(0o755)
    (t / "user.cfg").write_text('video_driver = "gl"\nrewind_enable = true\nrun_ahead_enabled = true\npreemptive_frames_enable = true\nvideo_filter = "expensive.so"\naudio_latency = 256\naudio_volume = -6\n')
    (t / "lilsmoke.conf").write_text('DISPLAY_WIDTH=640\nDISPLAY_HEIGHT=480\nSHOW_FPS=on\n')
    rom = t / "Test game.sfc"; rom.write_bytes(b"test fixture")
    env = os.environ | {
        "PATH": str(t / "bin") + ":" + os.environ["PATH"], "ARGS": str(t/"args"), "ENV_LOG": str(t/"env"), "DFB_LOG": str(t/"dfb"),
        "LILSMOKE_CORES_DIR": str(t/"cores"), "LILSMOKE_RETROARCH_BASE_CONFIG": str(ROOT/"package/retroarch-mips/retroarch.cfg"),
        "LILSMOKE_RETROARCH_USER_CONFIG": str(t/"user.cfg"), "LILSMOKE_RETROARCH_CORE_OPTIONS": str(t/"core.cfg"),
        "LILSMOKE_RETROARCH_RUN_DIR": str(t/"run"), "LILSMOKE_LOG_DIR": str(t/"logs"),
        "LILSMOKE_SAVE_ROOT": str(t/"saves"), "LILSMOKE_STATE_ROOT": str(t/"states"),
        "LILSMOKE_SCREENSHOT_DIR": str(t/"screens"), "LILSMOKE_PLAYLIST_DIR": str(t/"playlists"),
        "LILSMOKE_THUMBNAIL_DIR": str(t/"thumbs"), "LILSMOKE_ARCHIVE_CACHE": str(t/"cache"),
        "LILSMOKE_CONFIG": str(t/"lilsmoke.conf"), "DISPLAY": ":99",
    }
    script = ROOT/"package/lilsmoke-scripts/src/lilsmoke-retroarch"
    def launch(path=rom, mode="run"):
        args = ["busybox", "sh", str(script), mode]
        if mode == "run": args += ["snes", str(path)]
        subprocess.run(args, env=env, check=True, capture_output=True)
    launch()
    entries = re.findall(r'^\s*([\w]+)\s*=\s*(.*)$', (t/"run/retroarch.cfg").read_text(), re.M)
    cfg = dict(entries)
    check(cfg['input_driver'] == '"sdl2"', 'DirectFB keyboard uses SDL2 without a competing tty reader')
    check(cfg['input_toggle_fast_forward'] == '"nul"' and cfg['input_hold_fast_forward'] == '"nul"', 'game buttons cannot enable fast-forward')
    check(cfg['input_exit_emulator'] == '"f12"', 'Select/Escape cannot accidentally quit a game')
    check(cfg['libretro_info_path'] == '"/usr/share/libretro/info"' and cfg['core_info_cache_enable'] == 'false',
          'installed core metadata used without a stale capability cache')
    for row in (ROOT/'package/retroarch-mips/cores.lock').read_text().splitlines():
        if not row or row.startswith('#'): continue
        fields = row.split('|')
        info = ROOT/'package/retroarch-mips/core-info'/Path(fields[4]).with_suffix('.info')
        check(info.is_file() and 'savestate = "true"' in info.read_text(), fields[0] + ' has pinned save capability metadata')
    check('savestate_features = "basic"' in (ROOT/'package/retroarch-mips/core-info/snes9x2005_libretro.info').read_text(),
          'legacy SNES serializer does not claim bit-exact audio restoration')
    check(len(cfg) == len(entries), "runtime config has no duplicate keys")
    check(cfg["video_driver"] == '"sdl2"', "mandatory video driver wins over user and base")
    for key in ["rewind_enable", "run_ahead_enabled", "preemptive_frames_enable", "video_shader_enable"]:
        check(cfg[key] == "false", key + " cannot consume extra frame work")
    check(cfg["video_filter"] == '""', "software postprocessing disabled in constrained profile")
    check(cfg["audio_latency"] == "64", "mandatory latency overrides stale duplicate")
    check(cfg["audio_volume"] == "-6", "unconstrained user audio preference preserved")
    check(cfg["video_fullscreen_x"] == "640" and cfg["fps_show"] == "true", "UI display settings applied")
    check(cfg["core_options_path"] == '"'+str(t/"core.cfg")+'"', "persistent core options path applied")
    check((t/"env").read_text().strip() == "directfb:software:alsa:unset", "CPU-only environment and no host DISPLAY")
    check({'vt', 'no-vt-switch', 'no-vt-switching', 'disable-module=keyboard', 'linux-input-force'} <= set((t/'dfb').read_text().strip().split(',')),
          'evdev input retains raw VT ownership without console switching')
    for board in ('qemu-mipsel', 'sf3000', 'gb350', 'vbox-x86_64'):
        options = {(line.split('#', 1)[0]).strip() for line in (ROOT/'board'/board/'directfbrc').read_text().splitlines()}
        check({'vt', 'no-vt-switch', 'no-vt-switching'} <= options and 'no-vt' not in options,
              board + ' owns keyboard mode while preventing VT shortcuts')
    check(str(rom) in (t/"args").read_text().splitlines(), "ROM path with spaces passed as one argument")
    check('frameskip = "disabled"' in (t/"core.cfg").read_text(), "new installs default to every rendered frame")
    (t/"core.cfg").write_text('snes9x_2005_frameskip = "auto"\n')
    launch()
    check('"auto"' in (t/"core.cfg").read_text(), "existing user core options never silently overwritten")
    tool = shutil.which("7z") or shutil.which("7za")
    if not tool: raise RuntimeError("7z or 7za required for real archive test")
    archives = []
    for n in [1, 2]:
        folder = t / str(n); folder.mkdir()
        (folder/"same.sfc").write_bytes(bytes([n])*64)
        archive = folder / "same.7z"
        subprocess.run([tool, "a", str(archive), "same.sfc"], cwd=folder, check=True, stdout=subprocess.DEVNULL)
        launch(archive)
        selected = Path((t/"args").read_text().splitlines()[-1])
        check(selected.read_bytes() == bytes([n])*64, "7z extracted content for archive " + str(n))
        archives.append(selected)
    check(archives[0] != archives[1], "same-named archives have separate cache entries")
    stamp = archives[1].stat().st_mtime_ns
    launch(t/"2/same.7z")
    check(archives[1].stat().st_mtime_ns == stamp, "unchanged archive launch reuses extracted data")
    (t/"cores/parallel_n64_libretro.so").touch()
    cartridge = t/"Test game.z64"; cartridge.write_bytes(b"N64 fixture")
    def n64_config():
        subprocess.run(["busybox", "sh", str(script), "run", "n64", str(cartridge)],
                       env=env, check=True, capture_output=True)
        return dict(re.findall(r'^\s*([\w]+)\s*=\s*(.*)$',
                               (t/"run/retroarch.cfg").read_text(), re.M))
    n64 = n64_config()
    check(n64["input_player1_l_x_plus"] == '"right"' and
          n64["input_player1_l_y_minus"] == '"up"', "N64 keyboard supplies analog movement")
    with (t/"user.cfg").open("a") as f:
        f.write('input_player1_l_x_plus = "d"\n')
    n64 = n64_config()
    check(n64["input_player1_l_x_plus"] == '"d"', "N64 user analog binding remains configurable")
    check(n64["video_driver"] == '"sdl2"' and n64["rewind_enable"] == "false",
          "N64 bindings cannot override hardware limits")
    launch(mode="menu")
    check("-L" not in (t/"args").read_text().splitlines(), "RetroArch menu still launches without core")
    result = subprocess.run(["busybox", "sh", str(script), "run"], env=env, capture_output=True)
    check(result.returncode == 1 and b"usage:" in result.stderr, "missing arguments fail cleanly")
print(f"EMULATION_CONFIG_SMOKE_OK checks={checks}")
