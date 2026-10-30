#!/usr/bin/env python3
"""Real SSH authentication against the project's MIPS/musl Dropbear binary.
The guest uses a diagnostic Malta kernel, not a completed console firmware.
"""
import gzip
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time
from mips_boot_smoke import Newc, find_qemu

ROOT=Path(__file__).resolve().parents[2]
WORK=ROOT.parents[2]/"work"
OUT=ROOT/"test-results/remote-input"
SYSROOT=ROOT/"build/buildroot/output/host/mipsel-buildroot-linux-musl/sysroot"
DROPBEAR=ROOT/"build/buildroot/output/build/dropbear-2022.83/dropbearmulti"
KERNEL=WORK/"mipsboot/vmlinuz-6.1.0-50-4kc-malta"
INSTALLER=WORK/"mipsboot/debian-6.1.0-50-initrd.gz"

def network_modules():
    data=gzip.decompress(INSTALLER.read_bytes())
    offset=0
    found={}
    while offset+110<=len(data):
        header=data[offset:offset+110]
        if header[:6] not in [b"070701",b"070702"]:
            raise RuntimeError("Unexpected installer archive format")
        size=int(header[54:62],16)
        length=int(header[94:102],16)
        name=data[offset+110:offset+110+length-1].decode()
        offset=(offset+110+length+3)&~3
        payload=data[offset:offset+size]
        offset=(offset+size+3)&~3
        if name=="TRAILER!!!": break
        for module in ["mii.ko","pcnet32.ko"]:
            if name.endswith("/"+module):
                found[module]=payload
    if len(found)!=2: raise RuntimeError("Matching PCnet modules not found in installer")
    return found
native=Path(os.environ.get("WINDIR","C:/Windows"))/"System32/OpenSSH"
ssh=str(native/"ssh.exe") if (native/"ssh.exe").exists() else shutil.which("ssh")
keygen=str(native/"ssh-keygen.exe") if (native/"ssh-keygen.exe").exists() else shutil.which("ssh-keygen")
with tempfile.TemporaryDirectory(prefix="lilsmoke-ssh-test-") as directory:
    temporary=Path(directory)
    key=temporary/"key"
    subprocess.run([keygen,"-q","-t","ed25519","-N","","-f",str(key)],check=True)
    a=Newc()
    for name in ["bin","sbin","usr","usr/bin","usr/sbin","usr/lib","usr/lib/lilsmoke","lib",
                 "proc","sys","dev","dev/pts","tmp","run","etc","etc/lilsmoke","persist","persist/config",
                 "persist/home","persist/home/lil","persist/home/lil/.ssh","storage"]:
        a.dir(name,0o700 if name.endswith("lil") or name.endswith(".ssh") else 0o755)
        if name in ["storage","persist/home/lil","persist/home/lil/.ssh"]:
            a.entries[-1].uid=a.entries[-1].gid=1000
    a.chrdev("dev/console",5,1); a.chrdev("dev/null",1,3,0o666)
    a.chrdev("dev/urandom",1,9,0o666); a.chrdev("dev/random",1,8,0o666)
    a.file("bin/busybox",(WORK/"mipsboot/busybox-mipsel").read_bytes(),0o755)
    for app in ["sh","mount","mkdir","chmod","chown","cat","echo","uname","awk","grep","sleep","sync",
                "poweroff","mv","rm","id","ip","insmod","start-stop-daemon","sha256sum"]:
        a.symlink("bin/"+app,"busybox")
    a.file("usr/sbin/dropbearmulti",DROPBEAR.read_bytes(),0o755)
    a.symlink("usr/sbin/dropbear","dropbearmulti")
    a.symlink("usr/bin/dropbearkey","/usr/sbin/dropbearmulti")
    a.file("lib/libc.so",(SYSROOT/"lib/libc.so").read_bytes(),0o755)
    for name,payload in network_modules().items():
        a.file("lib/"+name,payload)
    a.symlink("lib/ld-musl-mipsel.so.1","libc.so")
    a.file("etc/passwd","root:x:0:0:root:/root:/bin/sh\nlil:x:1000:1000:LilSmokeOS:/persist/home/lil:/bin/sh\n")
    a.file("etc/group","root:x:0:\nlil:x:1000:\n")
    a.file("etc/shadow","root:*:20000:0:99999:7:::\nlil:$1$smoketest$3JVqi3qm17jeSXNhPHaYl/:20000:0:99999:7:::\n",0o600)
    a.file("etc/shells","/bin/sh\n")
    a.file("persist/home/lil/.ssh/authorized_keys",key.with_suffix(".pub").read_bytes(),0o600)
    a.entries[-1].uid=a.entries[-1].gid=1000
    a.file("persist/config/lilsmoke.conf","SSH_ENABLED=on # config written by UI\n")
    for name in ["lilsmoke-config-read","lilsmoke-sshd"]:
        a.file("usr/lib/lilsmoke/"+name,(ROOT/"package/lilsmoke-scripts/src"/name).read_bytes(),0o755)
    a.file("init", """#!/bin/sh
exec </dev/console >/dev/console 2>&1
export PATH=/bin:/usr/bin:/usr/sbin:/sbin
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devpts devpts /dev/pts
insmod /lib/mii.ko
insmod /lib/pcnet32.ko
ip link set lo up
ip link set eth0 up
ip addr add 10.0.2.15/24 dev eth0
ip route add default via 10.0.2.2
/usr/lib/lilsmoke/lilsmoke-sshd start || echo SSH_START_FAILED
echo SSH_GUEST_READY
while [ ! -f /storage/finish ]; do sleep 1; done
sha256sum /persist/ssh/* > /tmp/keys.before
/usr/lib/lilsmoke/lilsmoke-sshd restart
sha256sum /persist/ssh/* > /tmp/keys.after
if /bin/busybox cmp /tmp/keys.before /tmp/keys.after; then echo SSH_KEYS_PERSIST_RESTART; fi
echo SSH_ENABLED=off >/persist/config/lilsmoke.conf
/usr/lib/lilsmoke/lilsmoke-sshd restart
if [ ! -e /run/lilsmoke-sshd/dropbear.pid ]; then echo SSH_DISABLED_CONFIRMED; fi
echo SSH_GUEST_DONE
poweroff -f
""",0o755)
    initrd=temporary/"ssh.cpio.gz"; initrd.write_bytes(gzip.compress(a.build()))
    with socket.socket() as sock:
        sock.bind(("127.0.0.1",0)); port=sock.getsockname()[1]
    cmd=[str(find_qemu()),"-M","malta","-cpu","74Kf","-m","128","-smp","1",
         "-kernel",str(KERNEL),
         "-initrd",str(initrd),"-append","console=ttyS0 rdinit=/init panic=-1",
         "-display","none","-serial","stdio","-monitor","none","-no-reboot",
         "-nic",f"user,model=pcnet,hostfwd=tcp:127.0.0.1:{port}-:22"]
    with (OUT/"mips-ssh-boot.log").open("w") as log:
        process=subprocess.Popen(cmd,stdout=log,stderr=subprocess.STDOUT)
        try:
            deadline=time.monotonic()+120
            while time.monotonic()<deadline:
                if process.poll() is not None: raise RuntimeError("guest exited early")
                if "SSH_GUEST_READY" in (OUT/"mips-ssh-boot.log").read_text(errors="replace"): break
                time.sleep(1)
            else: raise RuntimeError("guest startup timeout")
            common=[ssh,"-T","-p",str(port),"-i",str(key),"-o","IdentitiesOnly=yes",
                    "-o","BatchMode=yes","-o","ConnectTimeout=10","-o","StrictHostKeyChecking=no",
                    "-o","UserKnownHostsFile="+str(temporary/"known-hosts")]
            run=subprocess.run(common+["lil@127.0.0.1","id -u; printf SSH_STORAGE_OK > /storage/proof; cat /storage/proof; uname -m"],
                               capture_output=True,text=True,timeout=40)
            (OUT/"mips-ssh-client.log").write_text(run.stdout+run.stderr)
            if run.returncode or "1000" not in run.stdout or "SSH_STORAGE_OK" not in run.stdout:
                raise RuntimeError("non-root SSH login/storage write failed: "+run.stdout+run.stderr)
            print("PASS real MIPS/musl SSH login as UID 1000 and storage write")
            rejected=subprocess.run(common+["root@127.0.0.1","id"],capture_output=True,text=True,timeout=30)
            if rejected.returncode==0: raise RuntimeError("root login unexpectedly accepted")
            print("PASS root SSH login rejected")
            subprocess.run(common+["lil@127.0.0.1","echo done > /storage/finish"],check=True,capture_output=True,timeout=30)
            process.wait(timeout=60)
            text=(OUT/"mips-ssh-boot.log").read_text(errors="replace")
            for marker in ["SSH_KEYS_PERSIST_RESTART","SSH_DISABLED_CONFIRMED","SSH_GUEST_DONE"]:
                if marker not in text: raise RuntimeError(marker+" missing")
                print("PASS "+marker)
            print("MIPS_SSH_SMOKE_OK")
        finally:
            if process.poll() is None:
                process.terminate()
                try: process.wait(timeout=10)
                except subprocess.TimeoutExpired: process.kill(); process.wait()
