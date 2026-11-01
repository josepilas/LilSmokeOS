# SPDX-License-Identifier: MPL-2.0
"""Configure all shipped profiles with real Buildroot, using disposable outputs."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PROFILES = {
    "qemu-mipsel": ("BR2_mipsel", "BR2_LILSMOKE_TARGET_QEMU", "BR2_TOOLCHAIN_BUILDROOT_MUSL"),
    "sf3000": ("BR2_mipsel", "BR2_LILSMOKE_TARGET_SF3000", "BR2_TOOLCHAIN_BUILDROOT_MUSL"),
    "gb350": ("BR2_mipsel", "BR2_LILSMOKE_TARGET_GB350", "BR2_TOOLCHAIN_BUILDROOT_MUSL"),
    "vbox-x86_64": ("BR2_x86_64", "BR2_LILSMOKE_TARGET_VBOX_X86_64", "BR2_TOOLCHAIN_BUILDROOT_GLIBC"),
    "vbox-test": ("BR2_x86_64", "BR2_LILSMOKE_TARGET_VBOX_X86_64", "BR2_TOOLCHAIN_BUILDROOT_MUSL"),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--buildroot", type=Path, required=True)
    parser.add_argument("--tree", type=Path, default=ROOT)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    tree, buildroot = args.tree.resolve(), args.buildroot.resolve()
    makefile = (buildroot / "Makefile").read_text()
    if not re.search(r"^export BR2_VERSION := 2024\.02\.3$", makefile, re.MULTILINE):
        raise SystemExit("Expected the pinned Buildroot 2024.02.3 checkout")
    report = {"passed": False, "buildroot": "2024.02.3", "profiles": {},
              "packages_compiled": False, "physical_image_tested": False}
    with tempfile.TemporaryDirectory(prefix="lilsmoke-defconfigs-") as temporary:
        for target, symbols in PROFILES.items():
            output = Path(temporary) / target
            environment = dict(os.environ, LILSMOKE_BUILDROOT_DIR=str(buildroot),
                               LILSMOKE_OUTPUT_DIR=str(output))
            result = subprocess.run(["bash", str(tree / "scripts/build.sh"), target, "--configure-only"],
                                    env=environment, text=True, capture_output=True, timeout=180)
            if result.returncode:
                raise RuntimeError(target + " configuration failed:\n" + result.stdout + result.stderr)
            config = (output / ".config").read_text()
            for symbol in symbols + ("BR2_TARGET_ROOTFS_CPIO", "BR2_PACKAGE_LILSMOKE_UI"):
                if symbol + "=y" not in config.splitlines():
                    raise RuntimeError(target + " lost required configuration: " + symbol)
            for relative in re.findall(r'\$\(BR2_EXTERNAL_LILSMOKE_PATH\)/([^"\s]+)', config):
                if not (tree / relative).exists():
                    raise RuntimeError(target + " references missing support input: " + relative)
            info = subprocess.run(["make", "-s", "-C", str(buildroot), "O=" + str(output),
                                   "BR2_EXTERNAL=" + str(tree), "show-info"],
                                  text=True, capture_output=True, timeout=180)
            if info.returncode:
                raise RuntimeError(target + " recipe evaluation failed:\n" + info.stdout + info.stderr)
            packages = json.loads(info.stdout)
            report["profiles"][target] = {"configured": True, "recipe_graph_evaluated": True,
                "packages_in_graph": len(packages),
                "configuration_warnings": [line.strip() for line in result.stderr.splitlines() if line.startswith("WARNING:")]}
            print(target + ": CONFIGURE_AND_RECIPE_GRAPH_OK", flush=True)
    report["passed"] = True
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
