# LilSmokeOS — Porting Guide

This guide explains how to bring LilSmokeOS to a new device. It's
organized as a checklist — work through it in order, and at the end
you'll have a new `board/<your-device>/` directory, a new
`configs/lilsmoke_<your-device>_defconfig`, and a bootable image.

## 1. Will LilSmokeOS run on your device?

LilSmokeOS targets a very narrow hardware niche. Before you start,
verify your device meets **all** of these:

| Requirement | Why |
|---|---|
| MIPS32 r2 (74Kf preferred) or x86_64 CPU | We don't ship an ARM build. ARM ports are possible but untested. |
| ≤ 256 MB RAM | More is fine but wasteful. Less than 128 MB → you're in trouble. |
| No GPU required | We use DirectFB on a Linux framebuffer. A GPU is fine but unused. |
| Linux framebuffer (`/dev/fb0`) at a known resolution | DirectFB needs fbdev. KMS-only GPUs need extra work. |
| ALSA-supported audio | AC97, I2S, USB audio all work. |
| SD card or eMMC for storage | We don't support booting from NAND/NOR directly. |
| ≤ 640×480 display | Higher resolutions work but consume more RAM. 4:3 aspect preferred. |
| U-Boot or compatible bootloader | BIOS-only x86 boards can use ISOLINUX/SYSLINUX instead. |

If your device doesn't meet one of these, porting will require
architectural work, not just board files. Open an issue first to
discuss.

## 2. Reference boards

| Board | Architecture | Bootloader | Use as a reference when… |
|---|---|---|---|
| `board/qemu-mipsel/` | MIPS32 74Kf | QEMU direct | You're targeting a MIPS SoC. |
| `board/vbox-x86_64/` | x86_64 | ISOLINUX | You're targeting an x86_64 device or another VM. |
| `board/sf3000/` | MIPS32 JZ4770 | U-Boot | You're targeting real MIPS handheld hardware. |
| `board/gb350/` | MIPS32 JZ4770 | U-Boot | Same SoC, different panel. |

`board/vbox-x86_64/` is the simplest port — it's all generic Linux
drivers (VESA fb, AC97, E1000). If you're porting to a new x86_64
single-board computer, start by copying it.

`board/sf3000/` is the reference for real MIPS hardware with U-Boot,
A/B partitions, and a FIT image.

## 3. Porting checklist

### Step 1: Create the board directory

```bash
mkdir -p board/<your-device>/{init.d,input-maps,mdev}
```

Copy the entire `init.d/`, `input-maps/`, and `mdev/` trees from the
closest reference board. You'll customize them in later steps.

### Step 2: Kernel config (`linux.config`)

Start from the closest reference board's `linux.config` and adjust:

1. **CPU architecture**: `CONFIG_CPU_MIPS32_R2` (MIPS) or `CONFIG_X86`
   / `CONFIG_X86_64` (x86). For other arches, start from the upstream
   kernel's `defconfig` for your SoC.
2. **SoC-specific drivers**: enable your SoC's framebuffer, MMC, USB,
   GPIO, audio drivers. For Ingenic JZ4770: `CONFIG_MACH_JZ4770`,
   `CONFIG_FB_JZ4770`, `CONFIG_MMC_JZ4770`, etc.
3. **Framebuffer driver**: this is critical. Without a working
   `/dev/fb0`, DirectFB can't initialize and LilSmokeUI shows a black
   screen. Test with `cat /dev/urandom > /dev/fb0` — you should see
   visual snow.
4. **Audio driver**: ALSA SoC audio (`CONFIG_SND_SOC_*`) for I2S
   codecs, or `CONFIG_SND_INTEL8X0` for AC97 (QEMU malta, VBox).
5. **Input**: `CONFIG_INPUT_EVDEV` is mandatory. GPIO keypads need
   `CONFIG_KEYBOARD_GPIO` + a devicetree `gpio-keys` node.
6. **WiFi** (if applicable): build the driver as a module
   (`=m`) — it's loaded on demand only when WiFi is enabled.

Save as `board/<your-device>/linux.config`.

### Step 3: BusyBox config (`busybox.config`)

You can usually copy `board/qemu-mipsel/busybox.config` verbatim. It's
a fragment — Buildroot merges it with its defaults. Add or remove
applets as your device requires.

### Step 4: DirectFB config (`directfbrc`)

This tells DirectFB how to talk to your framebuffer. The important
lines:

```
fbdev=/dev/fb0
mode=640x480           ← your panel's native resolution
depth=32               ← 32 (BGRA) is safest; 16 works if RAM is tight
pixelformat=ARGB       ← matches depth=32
```

For x86 with VESA fbdev (VirtualBox, older PCs):

```
fbdev=/dev/fb0
mode=640x480
depth=32
mmx=on                 ← x86-only; MIPS leaves this off
sse=on
```

For MIPS, leave `mmx`/`sse` off — they're x86 SIMD flags.

### Step 5: inittab

This tells BusyBox init what to launch. The QEMU version spawns
LilSmokeUI on `tty1` and shells on `ttyS0`/`tty2`/`tty3`. For your
device, you may want fewer gettys (saving ~1 MB each):

```
::sysinit:/etc/init.d/rcS
tty1::respawn:/usr/bin/lilsmoke-ui 2>/var/log/lilsmoke-ui.log
ttyS0::respawn:/bin/sh              ← keep this for UART debugging
::ctrlaltdel:/sbin/reboot
::shutdown:/etc/init.d/rcK
```

### Step 6: lilsmoke.conf

This is the default user config. Copy the QEMU version and adjust:

- `LILSMOKE_VERSION`: keep in sync with `CHANGELOG.md`.
- `WIFI_ENABLED`: leave `0` — users opt in via Settings.
- `LCD_BRIGHTNESS`: only meaningful if your device has a brightness
  control.
- `CPU_GOVERNOR`: `ondemand` for most devices; `performance` if your
  SoC doesn't support frequency scaling.

### Step 7: Input maps

Copy `board/qemu-mipsel/input-maps/default.map` to
`board/<your-device>/input-maps/<your-device>.map`. Then replace the
evdev codes with your device's actual codes.

To find the codes:

1. Boot your device with a debug shell (UART or SSH).
2. Run `evtest /dev/input/event0` (you may need to enable `evtest` in
   your Buildroot config — it's not on by default).
3. Press each button. evtest prints the evdev keycode (decimal).
4. Fill in the map file.

Format: `evdev <code> <action>` per line. Valid actions:
`up down left right a b x y l1 r1 l2 r2 select start home vol_up
vol_down power menu escape`.

### Step 8: init.d scripts

You copied these from a reference board. The only one that almost
always needs editing is `S01mounts`:

- **QEMU** uses tmpfs fallbacks for `/persist` and `/storage` (because
  the initramfs has no real partitions).
- **VirtualBox** also uses tmpfs for both — same reason.
- **Real HW (SF3000/GB350)** mounts `/dev/mmcblk0p5` (ext4) on
  `/persist` and `/dev/mmcblk0p6` (vfat) on `/storage`.

If your device has a different partition layout, update `S01mounts`
accordingly. The script tries multiple device nodes and falls back to
tmpfs, so it's robust — but if your partitions are at different
indices (`p6`, `p7`), add those.

### Step 9: post-build.sh

Copy the reference board's `post-build.sh`. It installs your board's
inittab, init scripts, config defaults, DirectFB config, theme, logo,
and fstab into the rootfs.

Adjust:

- The list of `init.d/S*` files (you may have added or removed some).
- The `input-maps/` install (use your board's map name).
- The `mdev/` install (if you have new hotplug helpers).

### Step 10: post-image.sh

This is the most board-specific script. It takes the Buildroot output
(kernel, rootfs, initramfs) and packages it into a flashable form.

| Bootloader | What `post-image.sh` produces |
|---|---|
| QEMU direct (`-kernel` + `-initrd`) | Nothing — QEMU loads vmlinux + cpio directly. |
| ISOLINUX (x86 ISO) | `lilsmoke.iso` via `genisoimage` + `isolinux.cfg` + `ldlinux.c32`. |
| SYSLINUX (x86 HDD) | `lilsmoke.img` with an MBR + SYSLINUX boot sector. |
| U-Boot (MIPS, no FIT) | `uImage` via `mkimage -A mips -O linux -T kernel -C none`. |
| U-Boot + FIT (MIPS, modern) | `lilsmoke.itb` FIT image with kernel + dtb + initramfs nodes. |

See `board/vbox-x86_64/post-image.sh` for the ISOLINUX example.
See `board/sf3000/post-image.sh` for the FIT image example.

### Step 11: Defconfig

Create `configs/lilsmoke_<your-device>_defconfig`. Start by copying
the closest reference and adjust:

```makefile
# --- Toolchain ---
BR2_<arch>=y                          ← mipsel / x86_64 / etc.
BR2_TOOLCHAIN_BUILDROOT_MUSL=y        ← strongly recommended
BR2_TOOLCHAIN_BUILDROOT_CXX=y         ← LilSmokeUI is C++

# --- Kernel ---
BR2_LINUX_KERNEL=y
BR2_LINUX_KERNEL_CUSTOM_VERSION=y
BR2_LINUX_KERNEL_CUSTOM_VERSION_VALUE="5.15.148"  ← or your SoC's vendor kernel
BR2_LINUX_KERNEL_USE_CUSTOM_CONFIG=y
BR2_LINUX_KERNEL_CUSTOM_CONFIG_FILE="$(BR2_EXTERNAL_LILSMOKE_PATH)/board/<your-device>/linux.config"

# --- Target ---
BR2_LILSMOKE_TARGET_<YOUR_DEVICE>=y   ← needs a Config.in entry (Step 12)

# ... (rest copied from reference)
```

### Step 12: Register the board in Config.in and external.mk

In `Config.in`, add to the `choice` block:

```kconfig
config BR2_LILSMOKE_TARGET_<YOUR_DEVICE>
    bool "<Your Device> (description)"
    help
      Builds for <your device>. <one-liner about HW>.
```

And to the `BR2_LILSMOKE_TARGET` string:

```kconfig
default "<your-device>"  if BR2_LILSMOKE_TARGET_<YOUR_DEVICE>
```

In `external.mk`, add to the board-dir selection:

```makefile
else ifeq ($(BR2_LILSMOKE_TARGET_<YOUR_DEVICE>),y)
LILSMOKE_BOARD_DIR := $(BR2_EXTERNAL_LILSMOKE_PATH)/board/<your-device>
```

### Step 13: Build and test

```bash
./scripts/build.sh <your-device>
```

This should produce a bootable image in
`build/buildroot/output/images/`. Boot it on your device (or in your
emulator) and verify:

1. Kernel boots (serial log shows the kernel cmdline).
2. Init scripts run (you see `[lilsmoke] Sxx:` lines).
3. `/dev/fb0` exists (run `ls -l /dev/fb0` over SSH/UART).
4. `lilsmoke-fbtest` paints the logo (S50).
5. LilSmokeUI shows the system list (S90 + inittab respawn).
6. D-pad navigation works (your input map is correct).
7. Audio works (open Settings → Master volume, press left/right — you
   should hear a beep).

### Step 14: Document

- Add an entry to `docs/README.md` for your new board.
- Add a row to the targets table in `README.md`.
- Add an entry to `CHANGELOG.md` under "Unreleased".
- Add a J-entry to `worklog.md` describing what you did.

### Step 15: PR

Read `CONTRIBUTING.md` and open a PR. A maintainer will review the
kernel config (we're picky about not enabling unnecessary options),
the memory budget (we'll measure idle RSS), and the boot flow on real
HW if you have a device.

## 4. Common pitfalls

### "DirectFB says 'No video mode found'"

Your `directfbrc`'s `mode=` line doesn't match what the kernel
framebuffer is actually advertising. Run `fbset -i` over SSH and
verify the geometry. Either change the kernel fb mode or update
`directfbrc`.

### "LilSmokeUI crashes immediately with 'SDL: no available video device'"

DirectFB didn't initialize. Check `/var/log/lilsmoke-ui.log` for the
DirectFB error. The usual culprits:

- `/dev/fb0` doesn't exist (kernel config missing your fb driver).
- Permissions: `chmod 0660 /dev/fb0` and ensure the `lilsmoke-ui`
  user (default: root) can read/write it.
- `directfbrc` has a syntax error.

### "Buttons don't work"

- `evtest /dev/input/event0` — do you see events when you press
  buttons? If no, the kernel isn't seeing the input device at all
  (devicetree issue for GPIO keypads).
- If yes, your `input-maps/*.map` has the wrong codes. Re-run the
  evtest procedure from Step 7.

### "PS1 games crash with SIGSEGV"

PCSX4ALL's MIPS dynarec requires the lower 2 GB of virtual address
space to be free for fixed PS1 memory mappings. This is achieved with
`-Wl,-Ttext-segment=0x40000000` (already set in `pcsx4all.mk`).

If your device uses a different default text segment (some non-MIPS
ports do), you'll get a segfault on the first `mmap` to address 0.
Either:

1. Don't ship PCSX4ALL on non-MIPS targets (set
   `BR2_PACKAGE_PCSX4ALL=n` in your defconfig), or
2. Rewrite the dynarec to not require address-0 mappings (huge effort).

### "Audio is silent"

- `aplay /usr/share/sounds/test.wav` (if you ship one) — does the
  codec make any sound?
- Check `amixer contents` — many codecs ship muted by default. Add
  `amixer set 'PCM' 90% unmute` to `S40alsa`.
- For I2S codecs, the devicetree's `simple-audio-card` routing may be
  wrong. Check `dmesg | grep -i asoc`.

### "Boot fails on real HW but works in QEMU"

The QEMU initramfs is writable. Real HW has read-only squashfs. If
your `S*` scripts write to `/etc/*` or `/var/*` before `S06overlay`
runs, they'll fail silently. Audit the script order.

### "Image is too big for the partition"

The squashfs rootfs must fit in the A/B slot (96 MB by default — see
`docs/ota.md`). If your build is too big:

1. `make graph-size` (Buildroot) to see what's eating space.
2. Drop packages you don't need (look at `BR2_PACKAGE_*` in your
   defconfig).
3. Enable `BR2_PACKAGE_LILSMOKE_LITE` (drops FBNeo, Handy, Beetle PCE).
4. Switch compression from XZ to zstd (`BR2_TARGET_ROOTFS_SQUASHFS4_ZSTD`).

## 5. When to ask for help

Open an issue with the `porting` label and include:

- The SoC name (e.g., "Ingenic JZ4770").
- A dmesg excerpt from boot.
- The contents of `/etc/lilsmoke/lilsmoke.conf`.
- The output of `lilsmoke-update status` (if you got that far).
- Whether you're targeting real HW or an emulator.

A maintainer will help you debug. Porting is fun — welcome aboard.
