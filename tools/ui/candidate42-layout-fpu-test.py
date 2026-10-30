#!/usr/bin/env python3
"""Bounded guest-kernel Cop1 path check; no SD, SDK writes or QEMU rebuild."""
import gzip
import hashlib
import json
from pathlib import Path
import stat
import subprocess
import time

P = Path(__file__).resolve().parents[2]
B = Path("/root/lilsmoke-os/build/buildroot/output")
Q = Path("/root/lilsmoke-candidate42-layout-fpu")
QEMU = Path("/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel")


def main():
    Q.mkdir(exist_ok=True)
    binary = Q / "probe"
    source = Path(__file__).with_name("candidate42-layout-fpu-probe.c")
    command = [str(B / "host/bin/mipsel-buildroot-linux-musl-gcc"), "-std=c11", "-O2",
               "-Wall", "-Wextra", "-Werror", str(source), "-lm", "-o", str(binary)]
    subprocess.run(command, check=True)
    subprocess.run([str(B / "host/bin/mipsel-buildroot-linux-musl-readelf"), "-A", str(binary)],
                   stdout=(Q / "probe-abi.txt").open("w"), check=True)
    archive = bytearray()

    def entry(name, mode, data=b"", major=0, minor=0):
        name = name.encode() + b"\0"
        fields = (1, mode, 0, 0, 1, 0, len(data), 0, 0, major, minor, len(name), 0)
        archive.extend(b"070701" + b"".join(f"{v:08x}".encode() for v in fields))
        archive.extend(name); archive.extend(b"\0" * (-len(archive) % 4))
        archive.extend(data); archive.extend(b"\0" * (-len(archive) % 4))

    for name in ("bin", "dev", "proc", "lib"):
        entry(name, stat.S_IFDIR | 0o755)
    entry("dev/console", stat.S_IFCHR | 0o600, major=5, minor=1)
    entry("bin/busybox", stat.S_IFREG | 0o755, (B / "target/bin/busybox").read_bytes())
    entry("lib/libc.so", stat.S_IFREG | 0o755, (B / "target/lib/libc.so").read_bytes())
    entry("lib/ld-musl-mipsel.so.1", stat.S_IFLNK | 0o777, b"libc.so")
    entry("qa", stat.S_IFREG | 0o755, binary.read_bytes())
    init = b"""#!/bin/busybox sh
/bin/busybox mount -t proc proc /proc
/bin/busybox cat /proc/cpuinfo
/qa
rc=$?
echo C42_FPU_EXIT=$rc
/bin/busybox poweroff -f
while :; do /bin/busybox sleep 60; done
"""
    entry("init", stat.S_IFREG | 0o755, init)
    entry("TRAILER!!!", 0)
    initrd = Q / "probe.cpio.gz"
    initrd.write_bytes(gzip.compress(archive, mtime=0))
    report = {"scope": "guest Cop1 emulation path, NOT hardware cycles/FPS", "cases": [],
              "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest()}
    for nofpu in (False, True):
        cmd = [str(QEMU), "-M", "malta", "-cpu", "74Kf-lilsmoke", "-smp", "1", "-m", "120",
               "-kernel", str(B / "images/vmlinux"), "-initrd", str(initrd), "-append",
               "console=ttyS0,115200 rdinit=/init panic=-1 nowait" + (" nofpu" if nofpu else ""),
               "-display", "none", "-serial", "stdio", "-monitor", "none", "-nic", "none", "-no-reboot"]
        log = Q / ("nofpu.log" if nofpu else "normal.log")
        with log.open("w") as output:
            process = subprocess.Popen(cmd, stdout=output, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 120
            try:
                while process.poll() is None and time.monotonic() < deadline:
                    if "C42_FPU_EXIT=" in log.read_text(errors="replace"): break
                    time.sleep(0.2)
            finally:
                if process.poll() is None:
                    process.terminate()
                    try: process.wait(timeout=5)
                    except subprocess.TimeoutExpired: process.kill(); process.wait()
        text = log.read_text(errors="replace")
        passed = "C42_FPU_EXIT=0" in text and "C42_FPU_PROBE_OK" in text
        report["cases"].append({"nofpu": nofpu, "command": cmd, "passed": passed,
                                "measurements": [s for s in text.splitlines() if s.startswith("PROBE ")]})
    report["passed"] = all(case["passed"] for case in report["cases"])
    (Q / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    if not report["passed"]: raise SystemExit(1)


if __name__ == "__main__": main()
