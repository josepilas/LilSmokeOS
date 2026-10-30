#!/usr/bin/env python3
"""Run actual Kconfig in disposable files without changing the build config."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
br = Path("/root/lilsmoke-os/build/buildroot")
source = (br / ".config").read_bytes()
checks = []

def check(ok, label):
    if not ok:
        raise RuntimeError(label)
    checks.append(label)
    print("PASS", label)

with tempfile.TemporaryDirectory(prefix="lilsmoke-n64-config-") as directory:
    def configure(overrides):
        config = Path(directory) / "config"
        lines = []
        for line in source.decode().splitlines():
            key = line.split("=", 1)[0]
            if line.startswith("# ") and line.endswith(" is not set"):
                key = line[2:-11]
            if key not in overrides:
                lines.append(line)
        lines.extend(key + "=" + value for key, value in overrides.items())
        config.write_text("\n".join(lines) + "\n")
        result = subprocess.run(
            ["make", "-C", str(br), "BR2_CONFIG=" + str(config), "olddefconfig"],
            env=dict(os.environ, PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"),
            capture_output=True, text=True, timeout=30)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        return config.read_text().splitlines()

    baseline = {"BR2_PACKAGE_LILSMOKE_N64": "y", "BR2_PACKAGE_LILSMOKE_LITE": "n"}
    lines = configure(baseline)
    check("BR2_PACKAGE_LILSMOKE_N64=y" in lines,
          "actual Kconfig accepts optional N64 on the o32 hard-float development target")
    lines = configure(dict(baseline, BR2_PACKAGE_LILSMOKE_LITE="y"))
    check("BR2_PACKAGE_LILSMOKE_LITE=y" in lines and "BR2_PACKAGE_LILSMOKE_N64=y" not in lines,
          "128 MB Lite preset excludes N64 even when explicitly requested")
    lines = configure(dict(baseline, BR2_MIPS_SOFT_FLOAT="y"))
    check("BR2_SOFT_FLOAT=y" in lines and "BR2_PACKAGE_LILSMOKE_N64=y" not in lines,
          "soft-float ABI cannot select the hard-float N64 package")
    lines = configure(dict(baseline, BR2_MIPS_FP32_MODE_XX="n",
                           BR2_MIPS_FP32_MODE_32="n", BR2_MIPS_FP32_MODE_64="y"))
    check("BR2_MIPS_FP32_MODE_64=y" in lines and "BR2_PACKAGE_LILSMOKE_N64=y" not in lines,
          "FR1-only floating-point ABI cannot select the FR0 N64 package")
    lines = configure(dict(baseline, BR2_STATIC_LIBS="y",
                           BR2_SHARED_LIBS="n", BR2_SHARED_STATIC_LIBS="n"))
    check("BR2_STATIC_LIBS=y" in lines and "BR2_PACKAGE_LILSMOKE_N64=y" not in lines,
          "static-only toolchain cannot select a dynamic libretro core")
check((br / ".config").read_bytes() == source, "real Buildroot configuration remains byte-identical")
(root / "test-results/n64/config-gates.log").write_text(
    "\n".join("PASS " + label for label in checks) +
    "\nN64_CONFIG_SMOKE_OK checks=" + str(len(checks)) + "\n")
print("N64_CONFIG_SMOKE_OK checks=" + str(len(checks)))
