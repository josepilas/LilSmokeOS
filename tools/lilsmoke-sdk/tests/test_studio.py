#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Jose Pilas.
"""Real X11 software SDL host app, actual keyboard/mouse events and saved audio.

No VM or device access. Requires existing SDL2 dev tools, Xvfb and xdotool.
Only the explicit test hooks capture BMP/WAV/JSON; controls are real X events.
"""
import argparse
import json
import os
from pathlib import Path
import shlex
import socket
import struct
import subprocess
import sys
import time
import wave

HERE=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(HERE))
import lilsmoke_sdk as sdk
from integration import bounded


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project",type=Path,required=True)
    parser.add_argument("--out",type=Path,required=True)
    args=parser.parse_args()
    args.project=args.project.resolve(); args.out=args.out.resolve()
    sdk.require(not (args.project/"_build/host/userdata").exists(),
                "Use a fresh host test project; GUI QA never overwrites existing preview userdata")
    args.out.mkdir(parents=True,exist_ok=False)
    report=dict(passed=False,software=True,vm=False,physical_input=False,sd_access=False,events=[])
    sources=[HERE/"templates/sdl2/main.c",HERE/"src/lilsmoke_sdl.c",HERE/"include/lilsmoke/sdl.h",
             HERE/"include/lilsmoke/input.h",HERE/"include/lilsmoke/lifecycle.h",
             HERE/"lilsmoke_sdk.py",HERE/"graphical.py"]
    before={str(p.relative_to(HERE)):sdk.sha256(p) for p in sources}
    report["source_sha256"]=before
    xvfb=app=None
    logs=[]
    def text(name):
        path=args.out/name
        return path.read_text(errors="replace") if path.exists() else ""
    def wait(predicate,label,seconds=20):
        deadline=time.monotonic()+seconds
        while not predicate():
            if time.monotonic()>deadline or (app and app.poll() is not None):
                raise RuntimeError(label+"\n"+text("first.log")[-1800:])
            time.sleep(.05)
    def snapshots(name="first.log"):
        result=[]
        for line in text(name).splitlines():
            if line.startswith(("SDK_STUDIO_READY ","SDK_STUDIO_STATE ","SDK_STUDIO_EXIT ")):
                try: result.append(json.loads(line.split(" ",1)[1]))
                except json.JSONDecodeError: pass
        return result
    try:
        cflags=shlex.split(subprocess.check_output(["pkg-config","--cflags","sdl2"],text=True))
        libs=shlex.split(subprocess.check_output(["pkg-config","--libs","sdl2"],text=True))
        test=args.out/"api-test"
        command=["cc","-std=c11","-O1","-g","-Wall","-Wextra","-Werror",
                 "-fsanitize=address,undefined","-fno-pie","-no-pie",*cflags,
                 "-I"+str(HERE/"include"),str(HERE/"tests/sdl_api_test.c"),
                 str(HERE/"src/lilsmoke_sdl.c"),*libs,"-o",str(test)]
        subprocess.run(command,check=True,timeout=180,preexec_fn=bounded,capture_output=True)
        result=subprocess.run([str(test)],capture_output=True,text=True,timeout=30)
        (args.out/"api.log").write_text(result.stdout+result.stderr)
        assert result.returncode==0,(result.stdout,result.stderr)
        report["sdl_api_sanitizers_passed"]=True
        report["input_snapshots_sanitizers_passed"]=True
        # WSLg can own a read-only /tmp/.X11-unix. Use a short-lived authenticated
        # TCP X server instead of changing that shared directory or permissions.
        server_log=(args.out/"xvfb.log").open("wb"); logs.append(server_log)
        with socket.socket() as sock:
            sock.bind(("127.0.0.1",0)); display=str(sock.getsockname()[1]-6000)
        assert int(display)>0
        authority=args.out/".Xauthority"
        fields=[b"",display.encode(),b"MIT-MAGIC-COOKIE-1",os.urandom(16)]
        authority.write_bytes(struct.pack(">H",65535)+b"".join(struct.pack(">H",len(v))+v for v in fields))
        authority.chmod(0o600)
        xvfb=subprocess.Popen(["Xvfb",":"+display,"-screen","0","800x600x24","-nolisten","unix",
                              "-listen","tcp","-auth",str(authority)],stdout=server_log,stderr=subprocess.STDOUT)
        env=dict(os.environ,DISPLAY="127.0.0.1:"+display,XAUTHORITY=str(authority),
                 SDL_VIDEODRIVER="x11",SDL_AUDIODRIVER="dummy",LILSMOKE_SDK_TRACE="1")
        for key in ("LD_PRELOAD","LILSMOKE_OD_NATIVE","LILSMOKE_HCFB","WAYLAND_DISPLAY"):
            env.pop(key,None)
        def server_ready():
            if xvfb.poll() is not None:
                raise RuntimeError("Private Xvfb exited\n"+text("xvfb.log")[-1800:])
            try:
                return subprocess.run(["xdotool","getdisplaygeometry"],env=env,timeout=3,
                                      capture_output=True).returncode==0
            except subprocess.TimeoutExpired:
                return False
        wait(server_ready,"Private Xvfb not ready\n"+text("xvfb.log")[-1800:],seconds=45)
        preview=[sys.executable,"-B",str(HERE/"lilsmoke_sdk.py"),"run",str(args.project),"--"]
        log=(args.out/"first.log").open("wb"); logs.append(log)
        app=subprocess.Popen([*preview,"--test-report",str(args.out/"first.json"),
                              "--test-screenshot",str(args.out/"first.bmp"),
                              "--test-audio",str(args.out/"tone.wav"),"--test-frames","3000"],
                             env=env,stdout=log,stderr=subprocess.STDOUT)
        wait(lambda:bool(snapshots()),"Studio did not submit its first frame")
        window=subprocess.check_output(["xdotool","search","--onlyvisible","--name","^SDK Studio$"],env=env,text=True).splitlines()[0]
        subprocess.run(["xdotool","windowfocus",window],env=env,check=True)
        def key(name):
            subprocess.run(["xdotool","key","--clearmodifiers",name],env=env,check=True)
            report["events"].append(name); time.sleep(.15)
        def click(x,y):
            subprocess.run(["xdotool","mousemove","--window",window,str(x),str(y),"click","1"],env=env,check=True)
            report["events"].append(dict(mouse=[x,y])); time.sleep(.15)
        key("Right"); key("Down"); key("Control_L")
        wait(lambda:any(s["nonzero_samples"]>0 for s in snapshots()),"No nonzero queued PCM")
        key("Down"); key("Right"); key("Down"); key("Control_L")
        wait(lambda:any(s["saves"]>0 for s in snapshots()),"Explicit Save did not commit")
        click(480,126)
        wait(lambda:any(s["color"]==2 and s["focus"]==0 and s["pointer_x"]==480 and
                        s["pointer_y"]==126 for s in snapshots()),"Pointer click did not change Color")
        key("Return")
        wait(lambda:snapshots()[-1]["menu"]==1,"Pause menu did not open")
        key("Control_L")
        wait(lambda:snapshots()[-1]["menu"]==0,"Resume did not work")
        key("Return"); key("Down"); key("Down"); key("Control_L")
        wait(lambda:snapshots()[-1]["menu"]==2,"Quit confirmation did not open")
        key("Right"); key("Control_L")
        assert app.wait(timeout=15)==0
        first=json.loads(text("first.json"))
        assert first["clean_exit"] and first["color"]==2 and first["tone"]==1 and first["volume"]==40,first
        assert first["saves"]>=2 and first["mouse_events"] and first["keyboard_events"] and first["nonzero_samples"]
        with wave.open(str(args.out/"tone.wav"),"rb") as wav:
            assert (wav.getnchannels(),wav.getframerate(),wav.getsampwidth())==(2,44100,2)
            samples=wav.readframes(wav.getnframes())
            assert samples and any(value for value in struct.unpack("<"+"h"*(len(samples)//2),samples))
        image=(args.out/"first.bmp").read_bytes()
        assert image[:2]==b"BM" and struct.unpack_from("<ii",image,18)==(640,480)
        offset=struct.unpack_from("<I",image,10)[0]
        bpp=struct.unpack_from("<H",image,28)[0]
        assert bpp in (24,32)
        step=bpp//8
        colors={image[p:p+3] for p in range(offset,len(image)-step+1,step)}
        assert len(colors)>=8,"Blank software frame"
        report["screenshot_colors"]=len(colors)
        try:
            from PIL import Image
            with Image.open(args.out/"first.bmp") as im: im.save(args.out/"first.png")
        except ImportError:
            pass
        log=(args.out/"reload.log").open("wb"); logs.append(log)
        app=subprocess.Popen([*preview,"--test-frames","12","--test-report",str(args.out/"reload.json")],
                             env=env,stdout=log,stderr=subprocess.STDOUT)
        assert app.wait(timeout=20)==0
        reload=json.loads(text("reload.json"))
        assert reload["loaded"]==1 and (reload["color"],reload["tone"],reload["volume"])==(2,1,40),reload
        # Hold only on the authenticated private X server, never the host desktop.
        log=(args.out/"combo.log").open("wb"); logs.append(log)
        app=subprocess.Popen([*preview,"--test-report",str(args.out/"combo.json"),
                              "--test-frames","1000"],env=env,stdout=log,stderr=subprocess.STDOUT)
        wait(lambda:bool(snapshots("combo.log")),"Combo session did not start")
        window=subprocess.check_output(["xdotool","search","--onlyvisible","--name","^SDK Studio$"],
                                       env=env,text=True).splitlines()[0]
        subprocess.run(["xdotool","windowfocus",window],env=env,check=True)
        subprocess.run(["xdotool","keydown","Return","Shift_R"],env=env,check=True)
        try:
            assert app.wait(timeout=15)==0
        finally:
            subprocess.run(["xdotool","keyup","Return","Shift_R"],env=env,check=True)
        combo=json.loads(text("combo.json"))
        assert combo["clean_exit"] and combo["exit_reason"]==1 and combo["combo_held_ms"]==1500,combo
        assert combo["saves"]==1 and combo["loaded"]==1 and combo["average_ms"]>=30,combo
        report.update(held_combo_save_exit=True,combo=combo)
        host_manifest=json.loads((args.project/"_build/host/build-report.json").read_text())
        assert host_manifest["target"]=="host" and host_manifest["built"]=="_build/host/application"
        assert not (args.project/"payload/runtime/bin/application").exists(),"Host ELF leaked into payload"
        report.update(passed=True,actual_host_window=True,keyboard_mouse_navigation=True,audio_wav_nonzero=True,
                      explicit_save_and_reload=True,confirmed_quit=True,first=first,reload=reload,
                      package_target_separate_from_host=True)
    except Exception as error:
        report["error"]=str(error); raise
    finally:
        if app and app.poll() is None:
            app.terminate()
            try: app.wait(timeout=5)
            except subprocess.TimeoutExpired: app.kill(); app.wait(timeout=5)
        if xvfb and xvfb.poll() is None:
            xvfb.terminate(); xvfb.wait(timeout=5)
        for log in logs: log.close()
        (args.out/".Xauthority").unlink(missing_ok=True)
        report["sources_unchanged"]=before=={str(p.relative_to(HERE)):sdk.sha256(p) for p in sources}
        report["passed"]=report["passed"] and report["sources_unchanged"]
        (args.out/"report.json").write_bytes(sdk.json_bytes(report))
    assert report["passed"]
    print(json.dumps(report,indent=2))


if __name__=="__main__": main()
