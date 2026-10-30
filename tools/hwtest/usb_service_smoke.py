#!/usr/bin/env python3
"""Isolated service tests. Fake ConfigFS attributes model kernel side effects.
These tests never mount a disk, configure a real interface or start a server.
"""
import json
import os
from pathlib import Path
import subprocess
import stat
import tempfile

ROOT = Path(__file__).resolve().parents[2]
USB = ROOT / "package/lilsmoke-scripts/src/lilsmoke-usb"
WRAPPER = r'''#!/usr/bin/python3
import json, os, pathlib, shutil, sys
root=pathlib.Path(os.environ["TEST_ROOT"])
name=pathlib.Path(sys.argv[0]).name
args=sys.argv[1:]
state=json.loads((root/"state.json").read_text())
with (root/"calls").open("a") as f: f.write(name+" "+" ".join(args)+"\n")
def save(): (root/"state.json").write_text(json.dumps(state))
def touch(p, text=""):
    p.parent.mkdir(parents=True,exist_ok=True); p.write_text(text)
g=root/"configfs/usb_gadget/lilsmoke"
if name=="mkdir":
    for arg in args:
        if arg.startswith("-"): continue
        p=pathlib.Path(arg)
        if p.exists() and "-p" not in args: sys.exit(1)
        p.mkdir(parents="-p" in args,exist_ok="-p" in args)
        if p==g:
            for attr in ["UDC","idVendor","idProduct","bcdUSB","bcdDevice","bDeviceClass","bDeviceSubClass","bDeviceProtocol"]:
                touch(p/attr)
            for d in ["functions","configs","strings","os_desc"]: (p/d).mkdir()
            for attr in ["use","b_vendor_code","qw_sign"]: touch(p/"os_desc"/attr)
        if p==g/"strings/0x409":
            for attr in ["serialnumber","manufacturer","product"]: touch(p/attr)
        if p==g/"configs/c.1/strings/0x409":
            touch(p/"configuration"); touch(g/"configs/c.1/MaxPower")
        if p.name in ["rndis.usb0","ecm.usb0"]:
            for attr in ["ifname","dev_addr","host_addr"]: touch(p/attr)
            if p.name=="rndis.usb0":
                for attr in ["compatible_id","sub_compatible_id"]: touch(p/"os_desc/interface.rndis"/attr)
        if p.name=="mass_storage.0":
            for attr in ["file","ro","removable"]: touch(p/"lun.0"/attr)
elif name=="rmdir":
    for arg in args:
        p=pathlib.Path(arg)
        if g==p or g in p.parents:
            # Kernel-owned files disappear when removing a ConfigFS item.
            if p.exists(): shutil.rmtree(p)
        else: p.rmdir()
elif name=="storage-service":
    action=args[0]
    if action=="identify":
        if state.get("ram"): sys.exit(1)
        touch(root/"storage-run/roms-device", str(root/"dev/loop999p2")+"\n")
    elif action=="unmount-roms":
        if state.get("fail_unmount"): sys.exit(1)
        state["local"]=False; save()
    elif action=="mount-roms":
        backer=g/"functions/mass_storage.0/lun.0/file"
        if backer.exists() and backer.is_file() and backer.read_text().strip(): sys.exit(99)
        state["local"]=True; save()
elif name=="mountpoint":
    target=args[-1]
    sys.exit(0 if target==str(root/"storage") and state.get("local") else 1)
elif name=="mount":
    if "-t" in args and args[args.index("-t")+1]=="configfs": sys.exit(1)
    backer=g/"functions/mass_storage.0/lun.0/file"
    if backer.exists() and backer.is_file() and backer.read_text().strip():
        print("SIMULTANEOUS MOUNT DETECTED",file=sys.stderr); sys.exit(99)
    state["local"]=True; save()
elif name=="umount":
    if state.get("fail_unmount"): sys.exit(1)
    state["local"]=False; save()
elif name=="stat": print("tmpfs" if state.get("ram") else "ext2/ext3")
elif name=="udhcpd":
    if state.get("fail_dhcp"): sys.exit(1)
elif name=="sleep": pass
elif name in ["ip","modprobe","sync","fsck.vfat"]: pass
else: raise RuntimeError(name)
'''
checks=0
def check(ok, label):
    global checks
    if not ok: raise AssertionError(label)
    checks+=1
    print("PASS", label, flush=True)

class Fixture:
    def __init__(self, path):
        self.root=path
        self.bin=path/"bin"
        self.bin.mkdir()
        for name in ["mkdir","rmdir","mount","umount","mountpoint","ip","stat","udhcpd","modprobe","sync","fsck.vfat","sleep","storage-service"]:
            (self.bin/name).write_text(WRAPPER)
            (self.bin/name).chmod(0o755)
        for name in ["storage","dev","storage-run","configfs/usb_gadget","udc/fake-udc","net/lsusb0"]:
            (path/name).mkdir(parents=True)
        os.mknod(path/"dev/loop999p2", stat.S_IFBLK | 0o600, os.makedev(7, 250))
        (path/"state.json").write_text(json.dumps({"local":True}))
        self.env=dict(os.environ,TEST_ROOT=str(path),PATH=str(self.bin)+os.pathsep+os.environ["PATH"],
            LILSMOKE_CONFIG=str(path/"config"),LILSMOKE_DEFAULT_CONFIG=str(path/"absent"),
            LILSMOKE_CONFIGFS_ROOT=str(path/"configfs"),LILSMOKE_UDC_CLASS=str(path/"udc"),
            LILSMOKE_NET_CLASS=str(path/"net"),LILSMOKE_STORAGE_ROOT=str(path/"storage"),
            LILSMOKE_USB_SERIAL_FILE=str(path/"persist/serial"),
            LILSMOKE_USB_RUN_DIR=str(path/"run"),
            LILSMOKE_STORAGE_RUN=str(path/"storage-run"),LILSMOKE_STORAGE_CMD=str(self.bin/"storage-service"))
    def state(self, **values):
        p=self.root/"state.json"
        state=json.loads(p.read_text()); state.update(values); p.write_text(json.dumps(state))
    def local(self): return json.loads((self.root/"state.json").read_text())["local"]
    def config(self, mode="drive", extra=""):
        (self.root/"config").write_text("USB_MODE="+mode+"\nUSB_DRIVE_SIZE_MB=32\n"+extra)
    def run(self, command="apply"):
        result=subprocess.run(["busybox","sh",str(USB),command],env=self.env,text=True,
                              stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=25)
        if result.returncode not in [0,1,2,75]: raise AssertionError(result.stdout)
        return result
    @property
    def gadget(self): return self.root/"configfs/usb_gadget/lilsmoke"
    def status(self): return (self.root/"run/status").read_text()

def scenario(fn):
    with tempfile.TemporaryDirectory(prefix="lilsmoke-usb-test-") as d:
        fn(Fixture(Path(d)))

def lifecycle(f):
    f.config("network+drive")
    r=f.run(); check(r.returncode==0, "composite gadget applies: "+r.stdout.strip())
    check(not f.local(), "local mount relinquished before export")
    check((f.gadget/"UDC").read_text()=="fake-udc\n", "ConfigFS binding newline")
    check((f.gadget/"configs/c.1/MaxPower").read_text()=="250\n", "MaxPower expressed in milliamps")
    conf=(f.root/"run/udhcpd.conf").read_text()
    serial=(f.root/"persist/serial").read_text()
    check(len(serial.strip())==24, "persistent per-device USB identity created")
    check("option lease 3600" in conf and "router" not in conf and "dns" not in conf, "USB DHCP preserves host internet routing")
    check(f.run("prepare-drive").returncode==1 and not f.local(), "prepare refuses an exported volume")
    f.config("off"); r=f.run()
    check(r.returncode==0 and f.local() and not f.gadget.exists(), "detach precedes local remount")
    check(not (f.root/"run/lock").exists(), "transaction lock released")
    f.config("network")
    check(f.run().returncode==0 and (f.root/"persist/serial").read_text()==serial,
          "USB identity survives gadget reconfiguration")

def unsupported(f):
    (f.root/"udc/fake-udc").rmdir()
    f.config()
    check(f.run().returncode==2 and "unsupported" in f.status() and f.local(), "no UDC fails without touching storage")

def unmount_failure(f):
    f.state(fail_unmount=True); f.config()
    check(f.run().returncode==1 and f.local() and not f.gadget.exists(), "busy local mount cannot be exported")

def detach_failure(f):
    f.config(); check(f.run().returncode==0, "detach failure fixture exports")
    (f.gadget/"UDC").unlink(); (f.gadget/"UDC").mkdir()
    f.config("off")
    check(f.run().returncode==1 and not f.local() and "detach failed" in f.status(), "failed unbind never remounts locally")

def dhcp_failure(f):
    f.state(fail_dhcp=True); f.config("network+drive")
    check(f.run().returncode==1 and f.local() and "error" in f.status(), "DHCP failure rolls back instead of reporting active")

def invalid(f):
    for value in ["999.1.1.1","192.168.7","192.168.7.255","127.0.0.1","1.2.3.4;touch /tmp/unsafe"]:
        f.config("network","USB_NETWORK_ADDRESS="+value+"\n")
        check(f.run().returncode==1 and not f.gadget.exists(), "invalid address rejected: "+value)
    f.config("network",'IGNORED=$(touch "'+str(f.root/"executed")+'")\n')
    check(f.run().returncode==0 and not (f.root/"executed").exists(), "config is data, never executed")

def ram(f):
    f.state(ram=True); f.config()
    check(f.run().returncode==1 and f.local(), "RAM-backed storage rejected")

def symlink(f):
    image=f.root/"dev/loop999p2"; image.unlink(); image.symlink_to(f.root/"state.json")
    f.config()
    check(f.run().returncode==1 and f.local(), "symlink backing file rejected")

def ecm(f):
    f.config("network","USB_NETWORK_DRIVER=ecm\nUSB_NETWORK_ADDRESS=192.168.8.2\n")
    check(f.run().returncode==0 and (f.gadget/"functions/ecm.usb0").exists(), "ECM network mode")
    check("start 192.168.8.3" in (f.root/"run/udhcpd.conf").read_text(), "DHCP peer never collides with device address")

def shutdown(f):
    f.config(); check(f.run().returncode==0, "shutdown fixture exports")
    check(f.run("stop").returncode==0 and not f.local(), "init stop detaches without remounting at shutdown")

def network_only(f):
    f.config("drive")
    check(f.run().returncode==0 and not f.local(), "drive-to-network fixture exports")
    f.config("network")
    check(f.run().returncode==0 and f.local() and
          not (f.gadget/"functions/mass_storage.0").exists(),
          "network-only mode restores local LILDRIVE access")

for fn in [lifecycle,unsupported,unmount_failure,detach_failure,dhcp_failure,invalid,ram,symlink,ecm,shutdown,network_only]:
    scenario(fn)
print("USB_SMOKE_OK checks="+str(checks))
