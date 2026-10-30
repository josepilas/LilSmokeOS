# LilSmokeOS — VirtualBox Setup Guide

VirtualBox is the fastest development target for LilSmokeOS. Unlike
the QEMU-mipsel target (which emulates a MIPS CPU), the VBox target
builds a native x86_64 image that boots in seconds and runs at near-
host speed. This makes it ideal for UI/theme/emulator development
where you don't care about MIPS-specific behavior.

The VBox target is also our reference for any x86_64 hardware port —
if you're bringing LilSmokeOS to an x86 single-board computer, start
with VBox.

## 1. Why VirtualBox?

| Target | Boot time | Speed | Use case |
|---|---|---|---|
| `qemu-mipsel` | ~5 s | Slow (MIPS emulation) | MIPS-specific kernel/core testing |
| `vbox-x86_64` | ~2 s | Near-native | UI/theme/emulator dev, fast CI |
| `vbox-test` | ~2 s | Near-native | Minimal smoke-test builds |
| `sf3000` / `gb350` (real HW) | ~8 s | Native MIPS | Real-world testing |

For most day-to-day LilSmokeOS development, use `vbox-test` (a minimal
build with musl + a subset of emulators) for the fastest possible
edit-compile-test loop. Use `vbox-x86_64` (full glibc build with all
emulators) for release-like validation.

## 2. Prerequisites

### Host packages

You need VirtualBox itself (the GUI is not required, but the
`VBoxManage` CLI is):

```bash
# Debian / Ubuntu
sudo apt install -y virtualbox virtualbox-ext-pack genisoimage isolinux syslinux-common

# Fedora / RHEL
sudo dnf install -y VirtualBox genisoimage syslinux

# Arch
sudo pacman -S virtualbox cdrtools syslinux
```

You also need the same Buildroot host deps as the QEMU target:

```bash
sudo apt install -y \
    build-essential git wget cpio unzip rsync bc \
    libncurses-dev libssl-dev bison flex python3
```

### Verify VBoxManage is on PATH

```bash
VBoxManage --version
# should print something like: 7.0.x
```

If you installed VirtualBox from Oracle's website (not your distro),
`VBoxManage` may be in `/usr/lib/virtualbox/`. Add it to PATH or
create a symlink:

```bash
sudo ln -sf /usr/lib/virtualbox/VBoxManage /usr/local/bin/VBoxManage
```

### Verify the ISO tools

The `post-image.sh` for VBox builds an ISO using `genisoimage` and
the ISOLINUX bootloader (`isolinux.bin`, `ldlinux.c32`). Verify:

```bash
which genisoimage isolinux.bin ldlinux.c32
# /usr/bin/genisoimage
# /usr/lib/ISOLINUX/isolinux.bin
# /usr/lib/syslinux/modules/bios/ldlinux.c32
```

If any of those are missing, install the `isolinux` and `syslinux-
common` packages.

## 3. Building the VBox image

Two defconfigs target VirtualBox:

| Defconfig | libc | Emulators | Use |
|---|---|---|---|
| `lilsmoke_vbox_x86_64_defconfig` | glibc | Full set + OD emulators | Release-like validation |
| `lilsmoke_vbox_test_defconfig` | musl | Minimal subset | Fast dev loop, CI |

To build:

```bash
cd /path/to/lilsmoke-os

# Full build (glibc, ~30 min on a fast machine)
./scripts/build.sh vbox-x86_64

# Fast minimal build (musl, ~10 min)
./scripts/build.sh vbox-test
```

After the build, the bootable ISO is at:

```
build/buildroot/output/images/lilsmoke.iso
```

## 4. Running in VirtualBox

### Option A: use the run-vbox.sh script (recommended)

```bash
./scripts/run-vbox.sh
```

This script:

1. Creates a VM named `LilSmokeOS` if it doesn't exist.
2. Configures it: 256 MB RAM, no audio (VBox's AC97 is enabled but
   may need a host audio backend), no GPU acceleration, PS/2 keyboard
   + mouse.
3. Attaches the ISO as a virtual CD.
4. Boots it.

Press `Host+Q` (default: Right Ctrl + Q) to close the VM window.

The script is idempotent — re-running it boots the existing VM with
the latest ISO.

### Option B: manual VBoxManage

If you prefer the manual route (or `run-vbox.sh` fails):

```bash
VMNAME="LilSmokeOS-manual"
ISO="build/buildroot/output/images/lilsmoke.iso"

VBoxManage createvm --name "$VMNAME" --ostype Linux26_64 --register
VBoxManage modifyvm "$VMNAME" \
    --memory 256 \
    --vram 16 \
    --graphicscontroller vmsvga \
    --nic1 nat \
    --audio none \
    --boot1 dvd
VBoxManage storagectl "$VMNAME" --name IDE --add ide --controller PIIX4
VBoxManage storageattach "$VMNAME" --storagectl IDE --port 1 --device 0 \
    --type dvddrive --medium "$ISO"
VBoxManage startvm "$VMNAME"
```

### Option C: VirtualBox GUI

1. New VM → Name: `LilSmokeOS` → Type: Linux → Version: Linux 2.6/3.x/4.x (64-bit).
2. Memory: 256 MB.
3. No hard disk (we boot from ISO).
4. Settings → System → Motherboard: enable I/O APIC.
5. Settings → System → Processor: 1 CPU.
6. Settings → Display: Graphics Controller = VMSVGA, Video Memory = 16 MB.
7. Settings → Storage: add the `lilsmoke.iso` as a CD/DVD drive.
8. Settings → Audio: disabled (or set to Intel HD Audio if you want
   to test audio).
9. Start.

## 5. What you should see

On boot, the ISO's ISOLINUX bootloader prints:

```
ISOLINUX 6.04  Copyright (C) 1994-2015 H. Peter Anvin et al
Loading lilsmoke...
```

Then the Linux kernel boots and you should see on `tty1` (the VBox
window):

```
[lilsmoke] S01: mounting pseudo-fs
[lilsmoke] S02: starting mdev
[lilsmoke] S03: ...
[lilsmoke] S50: running fbdev smoke test (10s)...
[lilsmoke] S90: pre-flight for LilSmokeUI
```

After the fbtest, LilSmokeUI takes over the framebuffer and you
should see the system list. The LilSmokeOS logo is the first thing
painted by `lilsmoke-fbtest`.

If you only see serial-style boot text and never a graphical UI:

- Your VBox graphics controller is wrong (must be VMSVGA or VBoxSVGA,
  not None).
- The kernel didn't find a framebuffer. Check that
  `CONFIG_FB_VESA=y` is set in `board/vbox-x86_64/linux.config`.

## 6. Keyboard mapping

The VBox target ships with a keyboard-to-gamepad map at
`board/vbox-x86_64/input-maps/vbox-keyboard.map`. The defaults:

| Keyboard | LilSmokeOS button |
|---|---|
| Arrow keys | D-pad |
| `Z` | A |
| `X` | B |
| `A` | X |
| `S` | Y |
| `Q` | L1 |
| `W` | R1 |
| `1` | L2 |
| `3` | R2 |
| `Enter` | Start |
| `Right Shift` | Select |
| `Esc` | Home (long-press = quit game) |
| `+` / `-` | Vol up / down |

You can remap via Settings → Input — Button Mapping in the UI. The
map is saved to `/persist/config/input-maps/user.map`.

## 7. SSH access

The VBox target enables SSH by default (the QEMU target doesn't,
because QEMU's user-mode networking makes it less useful). To SSH in:

```bash
# VBox's NAT mode forwards port 22 to host port 2222 by default.
ssh -p 2222 root@127.0.0.1
# password: (none — just press Enter)
```

If port 2222 is already in use, `run-vbox.sh` will pick the next free
port. Check the script's output for the actual forwarded port.

Once you're in, useful commands:

```sh
lilsmoke-update status          # show A/B slot status
lilsmoke-scrape --all           # scrape all ROMs (needs WiFi)
tail -f /var/log/lilsmoke-ui.log  # UI log
killall lilsmoke-ui             # restart the UI (it auto-respawns)
fbset -i                        # framebuffer info
```

## 8. Sharing files with the host

The VBox `/storage` partition is a tmpfs by default — its contents
are lost when the VM is powered off. To make ROMs/themes persistent,
use a VirtualBox shared folder:

1. In VBox Settings → Shared Folders, add a host folder (e.g.,
   `~/lilsmoke-share`) with auto-mount.
2. Boot LilSmokeOS.
3. The folder appears at `/mnt/share` (VBox Guest Additions mount it
   automatically if installed; without Guest Additions, you'll need
   to mount manually: `mount -t vboxsf share /mnt/share`).

For ROMs specifically, the easiest approach is:

1. Make `~/lilsmoke-share/roms/` on the host with the standard
   subfolder layout (`nes/`, `snes/`, etc.).
2. In the running LilSmokeOS, `cp -r /mnt/share/roms/* /storage/roms/`
   (or symlink: `ln -sf /mnt/share/roms /storage/roms`).

For themes, drop them in `~/lilsmoke-share/themes/` and symlink to
`/storage/themes/`.

## 9. Audio

VirtualBox's audio support is finicky. The VBox target's
`linux.config` enables the Intel AC97 driver (`CONFIG_SND_INTEL8X0`),
which matches VBox's default audio backend.

To enable audio in the VM:

1. VBox Settings → Audio → Enable Audio.
2. Host Audio Driver: ALSA (Linux host) or CoreAudio (macOS host).
3. Audio Controller: ICH AC97.

Boot LilSmokeOS and run `speaker-test -t sine -f 440 -l 1` over SSH.
You should hear a 1-second sine wave. If not:

- Check `amixer contents` — the Master or PCM control may be muted.
- Try a different VBox audio driver.
- VBox's audio has known latency issues; for serious audio testing,
  use the QEMU-mipsel target or real HW.

## 10. Networking

The VBox target uses NAT networking by default. The VM gets a DHCP
lease in the `10.0.2.x` range. Outbound HTTP/HTTPS works; inbound
connections require port forwarding (handled for SSH by `run-vbox.sh`).

For OTA testing:

- Run the OTA server on the host: `python3 tools/ota-server/ota-server.py
  serve --port 8080 --releases ./releases`.
- In the VM, the server is reachable at `10.0.2.2:8080` (VBox's
  standard host alias).
- Edit `/persist/config/lilsmoke.conf` and set `OTA_URL=http://10.0.2.2:8080`.

## 11. Debugging kernel / init issues

If the VM hangs on boot, the serial console is your friend. The VBox
target's `inittab` spawns a shell on `ttyS0` (COM1):

```
ttyS0::respawn:/bin/sh
```

To see it:

1. VBox Settings → Serial Ports → Enable Serial Port 1.
2. Port mode: Raw File.
3. Path: `/tmp/lilsmoke-serial.log`.
4. Boot the VM. All serial output is captured to that file.

You can also redirect the kernel console to serial by editing
`isolinux.cfg` and appending `console=ttyS0,115200 console=tty0` to
the `APPEND` line.

## 12. Rebuild cycle

The fast dev loop is:

```bash
# 1. Edit a file (e.g., lilsmoke-ui source)
vim package/lilsmoke-ui/src/src/ListView.cpp

# 2. Rebuild just that package (much faster than a full build)
cd build/buildroot
make lilsmoke-ui-reconfigure

# 3. Rebuild the rootfs image (re-runs post-build.sh + post-image.sh)
make target-finalize

# 4. Restart the VM (VBox will boot the new ISO)
VBoxManage controlvm LilSmokeOS reset
```

A typical lilsmoke-ui source change takes ~10 s to compile + ~5 s for
rootfs rebuild + ~2 s for VM reset. The full inner loop is <20 s.

## 13. Troubleshooting

### "VBoxManage: error: Failed to create the host-only adapter"

`run-vbox.sh` doesn't use host-only networking (it uses NAT), but
this error can appear if a previous VBox setup left a stale adapter.
Remove it:

```bash
VBoxManage hostonlyif remove vboxnet0
```

### "The ISO boots to a black screen with a blinking cursor"

ISOLINUX didn't load the kernel. The ISO may be missing the
`isolinux.bin` or `ldlinux.c32` files. Check the `post-image.sh`
output for the `genisoimage` command and verify the paths.

Rebuild the ISO manually:

```bash
cd build/buildroot/output/images
genisoimage -o lilsmoke.iso -b isolinux/isolinux.bin \
    -c isolinux/boot.cat -no-emul-boot -boot-load-size 4 \
    -boot-info-table -J -R isolinux/
```

### "DirectFB: failed to open /dev/fb0"

The kernel didn't initialize a framebuffer. Verify:

- `CONFIG_FB_VESA=y` is set in `board/vbox-x86_64/linux.config`.
- The VM's graphics controller is VMSVGA or VBoxSVGA (not None).
- Boot the VM, SSH in, and run `ls /dev/fb*` — if it's empty, the
  driver didn't load.

### "PS1 games don't run"

The `pcsx4all` MIPS dynarec only works on MIPS targets. The VBox
target builds the x86_64 launcher but with no MIPS dynarec, so PS1
games won't run. Use the QEMU-mipsel or real HW targets for PS1
testing.

### "The build fails with 'genisoimage: not found'"

Install `genisoimage` (Debian/Ubuntu) or `cdrtools` (Fedora/Arch).

### "ldlinux.c32: not found"

Install `syslinux-common`. The file is at
`/usr/lib/syslinux/modules/bios/ldlinux.c32` on Debian/Ubuntu.

## 14. What the VBox target is NOT good for

- MIPS-specific kernel debugging (use QEMU-mipsel).
- PS1 emulation testing (use QEMU-mipsel or real HW).
- Real-HW performance profiling (the x86_64 build will be dramatically
  faster than the MIPS build — useless for perf tuning).
- Audio driver development (VBox's AC97 emulation differs from real
  I2S codecs).

For those, switch to the QEMU-mipsel or real-HW targets.

## 15. Cleaning up

To delete the LilSmokeOS VM and its disks:

```bash
VBoxManage unregistervm LilSmokeOS --delete
```

The ISO in `build/buildroot/output/images/` is left untouched — to
remove it, run `./scripts/build.sh clean` (which wipes the entire
Buildroot output tree).
