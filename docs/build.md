# LilSmokeOS — Build Guide

## Prerequisites (host machine)

You need a Linux build host (Ubuntu 22.04+ recommended). macOS / Windows
are not supported by Buildroot without WSL2/Docker.

```bash
# Ubuntu / Debian
sudo apt install -y \
    build-essential git wget cpio unzip rsync bc \
    libncurses-dev libssl-dev \
    qemu-system-mips \
    bison flex \
    python3

# Fedora / RHEL
sudo dnf install -y \
    gcc gcc-c++ git wget cpio unzip rsync bc \
    ncurses-devel openssl-devel \
    qemu-system-mips \
    bison flex python3
```

## First build (QEMU target)

```bash
git clone <lilsmoke-os repo url>
cd lilsmoke-os

# Build (downloads Buildroot + cross-toolchain + kernel + userland)
./scripts/build.sh qemu-mipsel

# Run in QEMU
./scripts/run-qemu.sh
```

Expected output in QEMU:
```
[lilsmoke] S01: mounting pseudo-fs
[lilsmoke] S02: starting mdev
[lilsmoke] S03: ...
[lilsmoke] S90: pre-flight for LilSmokeUI
[lilsmoke-ui] J0 placeholder — TTY1 will respawn here
```

Press `Ctrl-A X` to exit QEMU.

## Build targets

| Target | Board | Use |
|---|---|---|
| `qemu-mipsel` | QEMU malta | J0–J3 development |
| `sf3000` | SF3000 (4.5″ IPS) | real hardware (J4+) |
| `gb350` | GB350 (3.5″ IPS) | real hardware (J4+) |

## Customizing the build

```bash
# Open menuconfig (kernel + userspace + packages)
./scripts/build.sh menuconfig

# After changes, save back to defconfig:
cd build/buildroot
make BR2_EXTERNAL=../../ savedefconfig
cp defconfig ../../configs/lilsmoke_qemu_mipsel_defconfig
```

## Build artifacts

After `./scripts/build.sh qemu-mipsel`, look in `build/buildroot/output/images/`:

| File | Purpose |
|---|---|
| `vmlinux` | Kernel ELF image (loaded by QEMU `-kernel`) |
| `rootfs.cpio.gz` | Initramfs (loaded by QEMU `-initrd`) |
| `rootfs.squashfs` | Optional compressed rootfs artifact |
| `lilsmoke.itb` | Real-hardware FIT image installed as `/lilsmoke.itb` in the active slot |
| `lilsmoke-version.txt` | Build manifest |

## Rebuild speed tips

Buildroot caches aggressively. After the first full build (~45 min):

```bash
# Rebuild just the kernel after editing linux.config
cd build/buildroot
make linux-reconfigure

# Rebuild just lilsmoke-ui after editing its source
make lilsmoke-ui-reconfigure

# Rebuild rootfs image after post-build.sh changes
make target-finalize
```

## Troubleshooting

### "qemu-system-mipsel: command not found"
Install the QEMU MIPS package (see Prerequisites).

### Kernel boots but no shell prompt
The serial console is on `ttyS0`. Make sure your terminal is 115200 8N1.
On the host, run `./scripts/run-qemu.sh` — it pipes serial to stdout.

### Build fails on toolchain step
Try a clean rebuild:
```bash
./scripts/build.sh clean
./scripts/build.sh qemu-mipsel
```

### Out of disk space
A full build needs ~10 GB. Buildroot caches downloads in `~/.buildroot-dl/`
— you can move this by setting `BR2_DL_DIR` in `build/buildroot/.config`.

## Next steps

- J1 adds SDL2 + DirectFB + ALSA packages → see `docs/media-stack.md` (J1)
- J2 adds RetroArch + cores → see `docs/cores.md` (J2)
- J3 adds LilSmokeUI → see `docs/frontend.md` (J3)
