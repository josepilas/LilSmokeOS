# LilSmokeOS real hardware guide

LilSmokeOS targets the SF3000 first. Current evidence identifies that board as
a single-core HiChip HC16xx/MIPS 74Kc system, not an Ingenic JZ4770 system.
The GB350 remains independently unverified and must not inherit SF3000 wiring
or boot assumptions by name alone.

Replacement boot images are not safe to flash yet. The open HC16xx kernel has
useful CPU, clock, interrupt, GPIO, serializer-key, PWM, MMC, and USB support,
but no usable SF3000 framebuffer or audio driver. Production boot integration,
power, battery, CPUFreq, and thermal support also remain incomplete.

## Target matrix

| Device | CPU/ABI | RAM | Panel | Current status |
|---|---|---:|---:|---|
| SF3000 | 1x little-endian MIPS32r2 74Kc, o32 hard-float/DSP2 | 256 MiB physical | 854x480 | userspace testable; BSP blocked |
| GB350 | unverified | unverified | reported 640x480 | userspace profile only; BSP blocked |

The standalone root filesystem uses musl and BusyBox. Stock firmware SDK
artifacts are glibc-linked and are not generally interchangeable with it.
Applications must use software rendering; no OpenGL/GLES/Vulkan path is part
of the supported contract.

## Safe development target

Use QEMU Malta for MIPS32r2/o32 userspace, the 256 MiB memory ceiling,
BusyBox/musl, UI navigation, and emulator-launch workflow tests:

```sh
./scripts/build.sh qemu-mipsel
./scripts/run-qemu.sh build/buildroot/output/images /path/to/storage
```

The VM has one 74Kf vCPU and 256 MiB RAM. Malta peripherals are not HC16xx
peripherals, so a QEMU pass is necessary evidence for userspace but never proof
that an image is safe for a physical console.

For the tested lower-memory profile, set `LILSMOKE_QEMU_RAM=128`. The optional
locally built `74Kf-lilsmoke` CPU has the researched 32-entry TLB geometry:

```sh
sh tools/qemu/build-profile.sh
LILSMOKE_QEMU_BINARY=/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel \
LILSMOKE_QEMU_CPU=74Kf-lilsmoke LILSMOKE_QEMU_RAM=128 \
./scripts/run-qemu.sh /path/to/qa-images /path/to/storage
```

Neither profile models cycle-accurate console performance. The QA display uses
856x480 RGB24 and a USB keyboard; physical panel, serializer input and audio
still require their actual BSP. DirectFB owns the current VT in raw keyboard
mode without terminal switching; the test kernel has `CONFIG_VT=y`.

## Hardware build gate

Each board has `board/<target>/bsp-status.conf`. The gate requires explicit
validation for CPU, input, storage, display, audio, power, thermal behavior,
and production boot:

```sh
./scripts/check-real-hardware-bsp.sh sf3000
./scripts/check-real-hardware-bsp.sh gb350
```

As long as any capability remains unresolved, the project does not register a
real-board post-image hook and refuses to advertise a flashable artifact.

## Read-only device probe

Once a shell is available on a test unit, collect a report without writing
registers or flash:

```sh
/usr/lib/lilsmoke/lilsmoke-hardware-probe /storage/logs/hardware.txt
```

The report includes CP0/kernel CPU data, memory, framebuffer nodes, evdev
devices, ALSA nodes, storage, CPUFreq, thermal, power, and relevant boot-log
lines. Attach the report to the matching board revision before changing a BSP
manifest from `no` or `experimental` to `yes`.

## Physical validation order

1. Preserve full stock SD and SPI-NOR backups.
2. Use non-destructive loading and capture the full 115200 8N1 UART log.
3. Run the read-only hardware probe and archive the board revision/photos.
4. Validate display timings and page flipping before launching the UI.
5. Validate ALSA, every key, SD writes, shutdown, battery, and thermal zones.
6. Run sustained SNES and PS1 tests only after power and thermal controls work.
7. Enable a production image only after cold-boot and recovery testing.

Never automate the experimental SPI flash modification described by the
reverse-engineering work. See [hc16xx-research.md](hc16xx-research.md) for
pinned sources, adopted findings, and current blockers.
