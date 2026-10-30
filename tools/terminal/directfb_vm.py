"""Actual MIPS SDL2/DirectFB + USB keyboard + PTY, in a disposable 120 MiB VM."""
import argparse
import gzip
import json
from pathlib import Path
import socket
import subprocess
import sys
import time

P = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(P / "tools/hwtest"))
from mips_boot_smoke import Newc
from mips_disk_fixture import runtime_libraries


class Qmp:
    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(5)
        self.socket.connect(str(path))
        self.stream = self.socket.makefile("rwb", buffering=0)
        self.receive()
        self.call("qmp_capabilities")

    def receive(self):
        while True:
            line = self.stream.readline()
            if not line:
                raise RuntimeError("QMP disconnected")
            value = json.loads(line)
            if "event" not in value:
                return value

    def call(self, name, arguments=None):
        self.stream.write((json.dumps(dict(execute=name, arguments=arguments or {})) + "\n").encode())
        result = self.receive()
        if "error" in result:
            raise RuntimeError(str(result))
        return result

    def key(self, *keys):
        for down in (True, False):
            self.call("input-send-event", {"events": [dict(type="key", data=dict(
                down=down, key=dict(type="qcode", data=key))) for key in keys]})
            time.sleep(0.12)

    def text(self, value):
        mapping = {" ": "spc", "/": "slash", "-": "minus", "\n": "ret"}
        for char in value:
            self.key(mapping.get(char, char))

    def close(self):
        self.stream.close()
        self.socket.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, default=Path("/root/lilsmoke-os/build/buildroot/output"))
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--qemu", default="/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    target = args.sdk / "target"
    archive = Newc()
    for directory in ("bin", "lib", "usr", "usr/bin", "usr/lib", "usr/share", "usr/share/fonts",
                      "usr/share/fonts/dejavu", "dev", "proc", "sys", "tmp", "run", "etc"):
        archive.dir(directory)
    archive.chrdev("dev/console", 5, 1)
    archive.file("bin/busybox", (target / "bin/busybox").read_bytes(), 0o755)
    archive.file("lib/libc.so", (target / "lib/libc.so").read_bytes(), 0o755)
    archive.symlink("bin/sh", "busybox")
    archive.symlink("lib/ld-musl-mipsel.so.1", "libc.so")
    archive.file("usr/bin/lilsmoke-terminal", args.binary.read_bytes(), 0o755)
    archive.file("usr/share/fonts/dejavu/DejaVuSansMono.ttf",
                 (target / "usr/share/fonts/dejavu/DejaVuSansMono.ttf").read_bytes())
    plugins = []
    archive.dir("usr/lib/directfb-1.7-7")
    for directory in sorted((target / "usr/lib/directfb-1.7-7").rglob("*")):
        name = directory.relative_to(target).as_posix()
        if directory.is_dir():
            archive.dir(name)
        elif directory.is_file():
            archive.file(name, directory.read_bytes(), 0o755)
            if ".so" in directory.name:
                plugins.append(directory)
    runtime_libraries(args.sdk, archive, [args.binary, *plugins])
    archive.file("init", (P / "tools/terminal/vm-init.sh").read_bytes(), 0o755)
    initrd = args.out / "terminal.cpio.gz"
    initrd.write_bytes(gzip.compress(archive.build(), compresslevel=1))
    socket_path = args.out / "qmp.sock"
    socket_path.unlink(missing_ok=True)
    command = [args.qemu, "-M", "malta", "-cpu", "74Kf-lilsmoke", "-smp", "1", "-m", "120",
               "-kernel", str(args.sdk / "images/vmlinux"), "-initrd", str(initrd),
               "-append", "console=ttyS0 video=cirrusfb:640x480M-16@60 panic=-1",
               "-vga", "cirrus", "-display", "none", "-serial", "stdio", "-monitor", "none",
               "-no-reboot", "-device", "usb-kbd", "-qmp", f"unix:{socket_path},server=on,wait=off"]
    log_path = args.out / "serial.log"
    with log_path.open("wb") as log:
        vm = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
        qmp = None
        def wait_for(marker, seconds=40):
            deadline = time.monotonic() + seconds
            while marker not in log_path.read_text(errors="replace"):
                if vm.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError(f"Timeout: {marker}\n" + log_path.read_text(errors="replace")[-6000:])
                time.sleep(0.1)
        try:
            wait_for("QA_READY_pty")
            qmp = Qmp(socket_path)
            time.sleep(0.5)
            qmp.text("tty\ntouch /run/k\n")
            wait_for("QA_READY_keyboard", 10)
            qmp.text("touch /run/")
            qmp.key("left")
            qmp.key("right")
            qmp.text("a\n")
            wait_for("QA_READY_arrow", 10)
            qmp.text("/bin/sleep 30\n")
            time.sleep(0.5)
            qmp.key("ctrl", "c")
            qmp.text("touch /run/c\n")
            wait_for("QA_READY_ctrl-c", 10)
            time.sleep(0.3)
            qmp.call("screendump", {"filename": str(args.out / "directfb.ppm")})
            from PIL import Image
            picture = Image.open(args.out / "directfb.ppm")
            if len(picture.getcolors(picture.width * picture.height) or []) < 5:
                raise RuntimeError("Blank framebuffer screenshot")
            picture.save(args.out / "directfb.png")
            qmp.key("ctrl", "d")
            wait_for("QA_TERMINAL_VM_DONE", 15)
            if "QA_TERMINAL_EXIT=0" not in log_path.read_text(errors="replace"):
                raise RuntimeError("Terminal did not exit cleanly")
        except Exception:
            if qmp:
                try:
                    qmp.call("screendump", {"filename": str(args.out / "failure.ppm")})
                    from PIL import Image
                    Image.open(args.out / "failure.ppm").save(args.out / "failure.png")
                except Exception:
                    pass
            raise
        finally:
            if qmp:
                qmp.close()
            if vm.poll() is None:
                vm.terminate()
            try:
                vm.wait(timeout=5)
            except subprocess.TimeoutExpired:
                vm.kill()
                vm.wait()
    (args.out / "report.json").write_text(json.dumps(dict(passed=True, guest_ram_mib=120, cpus=1,
        backend="SDL2 DirectFB fbdev software window surface", input="QMP USB keyboard via Linux evdev",
        real_pty=True, shell="SDK BusyBox ash", arrows=True, ctrl_c=True, ctrl_d_exit=True,
        physical_hardware_tested=False), indent=2) + "\n")
    print("PASS actual MIPS SDL2/DirectFB keyboard -> real PTY -> BusyBox shell, arrows, Ctrl-C/D, framebuffer; 120 MiB, one CPU")


if __name__ == "__main__":
    main()
