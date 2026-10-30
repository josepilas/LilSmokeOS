#!/usr/bin/env python3
"""Boot the production MIPS/musl input test in a 128-MiB, one-CPU Malta VM.
This is an ABI/kernel smoke test, not an SF3000 USB controller simulation.
"""
import gzip
from pathlib import Path
import subprocess
from mips_boot_smoke import Newc, find_qemu

ROOT=Path(__file__).resolve().parents[2]
WORK=ROOT.parents[2]/"work"
OUT=ROOT/"test-results/remote-input"
archive=Newc()
for directory in ["bin","sbin","usr","usr/bin","usr/lib","usr/lib/lilsmoke","proc","sys","dev","tmp","run","etc","etc/lilsmoke","persist","persist/config","storage"]:
    archive.dir(directory)
archive.chrdev("dev/console",5,1)
archive.chrdev("dev/null",1,3,0o666)
archive.file("bin/busybox",(WORK/"mipsboot/busybox-mipsel").read_bytes(),0o755)
for applet in ["sh","mount","mkdir","rmdir","cat","echo","uname","awk","grep","sleep","sync","poweroff","mv","rm","stat","tr","cksum","basename"]:
    archive.symlink("bin/"+applet,"busybox")
archive.file("input-smoke",(OUT/"input-smoke-mips").read_bytes(),0o755)
for name in ["lilsmoke-usb","lilsmoke-config-read"]:
    archive.file("usr/lib/lilsmoke/"+name,(ROOT/"package/lilsmoke-scripts/src"/name).read_bytes(),0o755)
archive.file("init", """#!/bin/sh
exec </dev/console >/dev/console 2>&1
export PATH=/bin:/usr/bin:/sbin
mount -t proc proc /proc
mount -t sysfs sysfs /sys
echo MIPS_REMOTE_TEST_BEGIN
uname -a
awk '/MemTotal/ {print}' /proc/meminfo
/input-smoke || { echo MIPS_INPUT_FAILED; poweroff -f; }
echo 'USB_MODE=network' >/persist/config/lilsmoke.conf
/usr/lib/lilsmoke/lilsmoke-usb apply
rc=$?
if [ "$rc" = 2 ] && grep -q state=unsupported /run/lilsmoke-usb/status; then
    echo PASS_MIPS_USB_CAPABILITY_GATE
else
    echo FAIL_MIPS_USB_CAPABILITY_GATE
fi
/usr/lib/lilsmoke/lilsmoke-config-read USB_MODE off
echo MIPS_REMOTE_TEST_END
sync
poweroff -f
""",0o755)
initrd=OUT/"remote-input.cpio.gz"
initrd.write_bytes(gzip.compress(archive.build()))
command=[str(find_qemu()),"-M","malta","-cpu","74Kf","-m","128","-smp","1",
         "-kernel",str(WORK/"mipsboot/vmlinuz-6.1.0-47-4kc-malta"),
         "-initrd",str(initrd),"-append","console=ttyS0 rdinit=/init panic=-1",
         "-display","none","-serial","stdio","-monitor","none","-no-reboot"]
result=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=150)
log=result.stdout.decode(errors="replace")
(OUT/"mips-boot.log").write_text(log)
print("\n".join(line for line in log.splitlines() if any(x in line for x in ["PASS","FAIL","SMOKE_OK","MemTotal","MIPS_REMOTE"])))
if "INPUT_SMOKE_OK checks=21" not in log or "PASS_MIPS_USB_CAPABILITY_GATE" not in log or "MIPS_REMOTE_TEST_END" not in log:
    raise SystemExit("MIPS smoke test failed; inspect mips-boot.log")
print("MIPS_REMOTE_SMOKE_OK")
