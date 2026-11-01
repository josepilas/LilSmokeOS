<!-- SPDX-License-Identifier: MPL-2.0 -->
# LilSmokeOS Build Guide

The wrapper pins **Buildroot 2024.02.3**. Use a Linux build host; Windows can
use WSL2 and macOS can use a Linux VM/container. Build on a case-sensitive
Linux filesystem in a path without spaces. These are development profiles,
not a way to reproduce every vendor boot blob in the published M8 image.

## Host Requirements

For Ubuntu/Debian:

```sh
sudo apt install build-essential git wget cpio unzip rsync bc file \
  libncurses-dev libssl-dev bison flex python3 qemu-system-mips
git clone https://github.com/josepilas/LilSmokeOS.git
cd LilSmokeOS
```

A complete compile needs substantially more space than configuration checks.
Allow space for downloaded sources, toolchains and each output tree. Neither
a universal build duration nor a ten-gigabyte maximum is guaranteed.

## Target Profiles

```sh
bash scripts/build.sh --list-targets
```

| Wrapper target | Checked-in Buildroot defconfig | Boundary |
| --- | --- | --- |
| `qemu-mipsel` | `configs/lilsmoke_qemu_mipsel_defconfig` | Malta/MIPS/musl development |
| `sf3000` | `configs/lilsmoke_sf3000_defconfig` | HC16xx userspace profile; hardware image remains BSP-gated |
| `gb350` | `configs/lilsmoke_gb350_defconfig` | Unverified board userspace profile; hardware image remains BSP-gated |
| `vbox-x86_64` | `configs/lilsmoke_vbox_x86_64_defconfig` | Legacy x86_64/glibc development profile |
| `vbox-test` | `configs/lilsmoke_vbox_test_defconfig` | Legacy x86_64/musl test profile |

`configs/lilsmoke_media.fragment` is an optional fragment, **not a complete
target defconfig**. Target listing no longer presents it as a buildable board.

The initial Alpha source export accidentally omitted the five
`*_defconfig` files. They are restored on `main`, and the source-export
filter/tests now preserve Buildroot's filename convention. The original
Alpha source ZIP remains a historical snapshot; use a current checkout or
the separately named source-r1 archive when provided with the release.
The OS image and original release tag are not changed by this source fix.

## Configure Without Building Packages

```sh
bash scripts/build.sh qemu-mipsel --configure-only
bash scripts/build.sh sf3000 --configure-only
bash scripts/build.sh gb350 --configure-only
bash scripts/build.sh vbox-x86_64 --configure-only
bash scripts/build.sh vbox-test --configure-only
```

The first command clones the pinned Buildroot if needed and creates its Kconfig
host helper. It then writes the selected output's `.config`. It does not
compile target packages/kernel, assemble an image, access a card or certify a
hardware profile. Configuration-only therefore does not need to bypass the
SF3000/GB350 image safety gate.

## Build QEMU

```sh
LILSMOKE_JOBS=1 bash scripts/build.sh qemu-mipsel
bash scripts/run-qemu.sh build/buildroot/output/images
```

Without arguments the wrapper selects `qemu-mipsel`. A normal build loads
the target defconfig and then invokes Buildroot's build. Existing output
configuration can be overwritten: use distinct output directories for
different targets. The development guest uses substitute Malta hardware;
it is not an exact HCSEMI peripheral/timing model.

## Separate Outputs Or An Existing Buildroot Checkout

```sh
export LILSMOKE_BUILDROOT_DIR=/path/to/buildroot-2024.02.3
export LILSMOKE_OUTPUT_DIR=/path/to/outputs/lilsmoke-qemu
bash scripts/build.sh qemu-mipsel --configure-only
LILSMOKE_JOBS=1 bash scripts/build.sh qemu-mipsel
```

The override must point to a matching Buildroot source checkout; it does not
download a new SDK or use its existing output directory automatically.
The default source is `build/buildroot`, default output its `output` folder.
`LILSMOKE_OUTPUT_DIR` is passed through Buildroot's `O=` mechanism. Preserve
working SDKs and private build outputs when testing a fresh configuration.

```sh
bash scripts/build.sh menuconfig
bash scripts/build.sh clean
```

These commands use the same selected Buildroot/output overrides. `clean`
removes build results in that output; it is not required to test a defconfig.

To save a reviewed configuration, invoke `savedefconfig` in the chosen output
and copy the resulting minimal defconfig to the corresponding repository file.
Do not commit generated `.config`, toolchains, downloads, ROMs or private BSPs.

## Direct Buildroot Use

```sh
make -C /path/to/buildroot-2024.02.3 O=/path/to/output \
  BR2_EXTERNAL=/path/to/LilSmokeOS lilsmoke_qemu_mipsel_defconfig
make -C /path/to/buildroot-2024.02.3 O=/path/to/output \
  BR2_EXTERNAL=/path/to/LilSmokeOS
```

The wrapper's SF3000/GB350 guard is not a claim that direct `make` produces a
safe flashable image. Those profiles are intentionally userspace-only while
their BSP status remains incomplete. Do not flash them to the tested M8 or
another board based on a shared marketing name.

## Regression Checks

No network, SDK or real compilation is needed for wrapper/source-export tests:

```sh
python3 -B -m unittest discover -s tools/build-config -p test_build_wrapper.py -v
python3 -B -m unittest discover -s tools/release -p test_alpha_source.py -v
```

With a local pinned Buildroot checkout, test all five real configurations and
their package recipe graphs using disposable output directories:

```sh
python3 -B tools/build-config/check_defconfigs.py \
  --buildroot /path/to/buildroot-2024.02.3 \
  --report tools/build-config/_build/defconfigs.json
```

This uses the actual wrapper, requires the expected target/architecture/libc
settings, checks referenced support paths, then evaluates `make show-info`.
It does not compile all packages or boot QEMU/VirtualBox/hardware. Existing
upstream DirectFB/musl dependency warnings are recorded rather than hidden;
full toolchain/library validation remains a separate step.

## Artifacts And Limits

QEMU expects `vmlinux` and `rootfs.cpio.gz` in the selected output's
`images/` directory. Other rootfs artifacts depend on the profile. Legacy
VirtualBox profiles/hooks expect an x86 kernel/initramfs and host ISO tools;
see [VirtualBox development](vbox-guide.md), not a physical-M8 guarantee.

For production M8 dependencies and licensing/build limits, read
[Source and build](developer/SOURCE-AND-BUILD.md) and
[Alpha source distribution](alpha-0.0.1-source.md). Configuration success is
not evidence of a complete binary rebuild, legal clearance or safe flashing.
