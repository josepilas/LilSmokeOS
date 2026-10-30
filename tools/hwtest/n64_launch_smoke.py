#!/usr/bin/env python3
"""Real BusyBox launch wrapper and actual 7z extraction; frontend argv is recorded."""
from pathlib import Path
import json
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
script = root / "package/lilsmoke-scripts/src/lilsmoke-retroarch"
fixture = root / "test-results/n64/lilsmoke-test.z64"
checks = []
def check(ok, label):
    assert ok, label
    checks.append(label)
    print("PASS", label)
with tempfile.TemporaryDirectory(prefix="lilsmoke-n64-launch-") as directory:
    work = Path(directory)
    bin_dir = work/"bin"
    bin_dir.mkdir()
    recorder = bin_dir/"retroarch"
    recorder.write_text("#!/usr/bin/python3\nimport json, os, sys\n"
                        "json.dump({'argv':sys.argv[1:],'video':os.environ.get('SDL_VIDEODRIVER'),"
                        "'display':os.environ.get('DISPLAY')},open(os.environ['QA_RECORD'],'w'))\n")
    recorder.chmod(0o755)
    cores = work/"cores"; cores.mkdir()
    (cores/"parallel_n64_libretro.so").touch()
    rom = work/"RDP SI test with spaces.z64"
    rom.write_bytes(fixture.read_bytes())
    archive = work/"test.7z"
    subprocess.run(["7z","a","-t7z",str(archive),str(rom)],check=True,capture_output=True)
    system = work/"system.conf"
    system.write_text("DISPLAY_WIDTH=640\nDISPLAY_HEIGHT=480\nGAMEPAD_BACKEND=linuxraw\n")
    options = work/"options.cfg"
    options.write_text('parallel-n64-gfxplugin = "gliden64"\nparallel-n64-angrylion-multithread = "all threads"\n')
    env = dict(os.environ, PATH=str(bin_dir)+":"+os.environ["PATH"],
               QA_RECORD=str(work/"record.json"), DISPLAY=":99",
               LILSMOKE_CORES_DIR=str(cores), LILSMOKE_CONFIG=str(system),
               LILSMOKE_RETROARCH_BASE_CONFIG=str(work/"base.cfg"),
               LILSMOKE_RETROARCH_USER_CONFIG=str(work/"user.cfg"),
               LILSMOKE_RETROARCH_CORE_OPTIONS=str(options),
               LILSMOKE_RETROARCH_AUTOCONFIG=str(work/"autoconfig"),
               LILSMOKE_RETROARCH_PROFILES=str(work/"profiles"),
               LILSMOKE_RETROARCH_RUN_DIR=str(work/"run"),
               LILSMOKE_LOG_DIR=str(work/"logs"), LILSMOKE_SAVE_ROOT=str(work/"saves"),
               LILSMOKE_STATE_ROOT=str(work/"states"), LILSMOKE_SCREENSHOT_DIR=str(work/"screens"),
               LILSMOKE_PLAYLIST_DIR=str(work/"playlists"), LILSMOKE_THUMBNAIL_DIR=str(work/"thumbs"),
               LILSMOKE_ARCHIVE_CACHE=str(work/"cache"))
    def call(path):
        return subprocess.run(["busybox","sh",str(script),"run","n64",str(path)],
                              env=env,capture_output=True,text=True,timeout=30)
    result = call(rom)
    check(result.returncode==0,"N64 raw ROM follows real BusyBox launcher path")
    recorded = json.loads((work/"record.json").read_text())
    check(recorded["argv"][-1]==str(rom) and str(cores/"parallel_n64_libretro.so") in recorded["argv"],
          "launcher selects CPU-only N64 core with quoted ROM argument")
    check(recorded["video"]=="directfb" and not recorded["display"],"runtime retains DirectFB and clears DISPLAY")
    runtime = (work/"run/retroarch.cfg").read_text()
    check('input_max_users = 4' in runtime,"runtime supports four controller ports")
    check(str(work/"saves") in runtime and (work/"saves").is_dir(),"N64 uses persistent save directory")
    effective = (work/"run/n64-core-options.cfg").read_text()
    check('parallel-n64-gfxplugin = "angrylion"' in effective and "gliden64" not in effective,
          "stale GPU plugin preference cannot override CPU-only renderer")
    check('parallel-n64-angrylion-multithread = "off"' in effective and "all threads" not in effective,
          "N64 runs synchronous renderer without oversized worker pool")
    result = call(archive)
    check(result.returncode==0,"actual renamed-independent 7z archive launches N64")
    extracted = Path(json.loads((work/"record.json").read_text())["argv"][-1])
    check(extracted.suffix==".z64" and extracted.read_bytes()==fixture.read_bytes(),
          "7z extraction preserves exact N64 ROM bytes")
    check(work/"cache" in extracted.parents,"archive extraction is disk-backed, not a RAM filesystem")
    system.write_text(system.read_text().replace("linuxraw","sdl2"))
    check(call(rom).returncode==0 and 'input_joypad_driver = "sdl2"' in (work/"run/retroarch.cfg").read_text(),
          "alternate SDL2 controller backend can be selected")
    (cores/"parallel_n64_libretro.so").unlink()
    check(call(rom).returncode==3,"uninstalled optional N64 core fails with clear missing-core status")
out = root/"test-results/n64"
(out/"launch-checks.json").write_text(json.dumps({"passed":len(checks),"checks":checks},indent=2)+"\n")
print("N64_LAUNCH_SMOKE_OK checks="+str(len(checks)))
