#!/usr/bin/env python3
"""Network/config-reader tests with isolated commands, no host network changes."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
SRC=ROOT/"package/lilsmoke-scripts/src"
checks=0
def check(ok,label):
    global checks
    if not ok: raise AssertionError(label)
    checks+=1; print("PASS "+label)
with tempfile.TemporaryDirectory(prefix="lilsmoke-network-") as directory:
    root=Path(directory)
    (root/"net/wlx-adapter/wireless").mkdir(parents=True)
    (root/"bin").mkdir()
    (root/"wifi.conf").write_text("network={}\n")
    for name in ["ip","wpa_supplicant","udhcpc","start-stop-daemon"]:
        script=root/"bin"/name
        script.write_text('#!/bin/sh\nprintf "%s\\n" "'+name+' $*" >> "$CALLS"\n')
        script.chmod(0o755)
    env=dict(os.environ,PATH=str(root/"bin")+os.pathsep+os.environ["PATH"],
        CALLS=str(root/"calls"),LILSMOKE_CONFIG=str(root/"config"),
        LILSMOKE_DEFAULT_CONFIG=str(root/"default"),LILSMOKE_CONFIG_READER=str(SRC/"lilsmoke-config-read"),
        LILSMOKE_NETWORK_RUN_DIR=str(root/"run"),LILSMOKE_NET_CLASS=str(root/"net"),
        LILSMOKE_WIFI_CONFIG=str(root/"wifi.conf"))
    def run():
        (root/"calls").write_text("")
        result=subprocess.run(["busybox","sh",str(SRC/"lilsmoke-network"),"restart"],env=env,
                              capture_output=True,text=True,timeout=10)
        return result,(root/"calls").read_text()
    def config(text): (root/"config").write_text(text)
    (root/"default").write_text('WIFI_ENABLED=off\nWIFI_IPV4=dhcp\nTEST="A # B" # outside\n')
    config('WIFI_ENABLED="on" # UI boolean\n')
    result,calls=run()
    check(result.returncode==0 and "wpa_supplicant" in calls and "wlx-adapter" in calls,
          "wireless capability detection accepts non-wlan adapter names")
    check("udhcpc -i wlx-adapter" in calls and " -b -t 5" in calls and " -q" not in calls,
          "DHCP client remains available for lease renewals")
    config('WIFI_ENABLED=on\nWIFI_IPV4=192.168.50.10/24\n')
    result,calls=run()
    check(result.returncode==0 and "addr add 192.168.50.10/24" in calls and "udhcpc" not in calls,
          "static Wi-Fi address applied without DHCP")
    config('WIFI_ENABLED=on\nWIFI_IPV4=999.0.0.1/24\n')
    result,calls=run()
    check(result.returncode!=0 and "addr add" not in calls,"invalid static IPv4 rejected")
    config('WIFI_ENABLED=off\n')
    result,calls=run()
    check(result.returncode==0 and "wpa_supplicant" not in calls,"Wi-Fi off does not start a supplicant")
    result=subprocess.run(["busybox","sh",str(SRC/"lilsmoke-config-read"),"TEST",""],env=env,
                          capture_output=True,text=True,check=True)
    check(result.stdout.strip()=="A # B","quoted config hash preserved and inline comment removed")
    config("TEST='single quoted value' # comment\n")
    result=subprocess.run(["busybox","sh",str(SRC/"lilsmoke-config-read"),"TEST",""],env=env,
                          capture_output=True,text=True,check=True)
    check(result.stdout.strip()=="single quoted value","single-quoted config parsed as data")
    config('WIFI_ENABLED=on\nBAD=$(touch "'+str(root/"executed")+'")\n')
    run()
    check(not (root/"executed").exists(),"writable config cannot execute shell code")
print("NETWORK_SMOKE_OK checks="+str(checks))
