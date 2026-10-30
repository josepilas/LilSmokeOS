# LilSmokeOS Emulator

Windows click-to-run QEMU virtual machine for the real LilSmokeOS QEMU-mipsel
target. This is not a mock UI shell: it boots `vmlinux` plus
`rootfs.cpio.gz` from the Buildroot output.

The packaged `LilSmokeOS Emulator.exe` uses this layout beside itself:

```text
LilSmokeOS Emulator/
|-- LilSmokeOS Emulator.exe
|-- images/
|   +-- vmlinux
|   +-- rootfs.cpio.gz
|-- runtime/
|   +-- qemu/
|       +-- qemu-system-mipsel.exe
|       +-- qemu-system-mipselw.exe
|-- storage/
|   +-- bios/
|   +-- roms/
|   +-- saves/
|   +-- states/
|   +-- themes/
|   +-- portsmoke/
|-- persist/
+-- logs/
```

Hardware profile:

```text
Machine: QEMU malta, MIPS32 little-endian
RAM: 256 MB
GPU: none
Display: QEMU SDL window using the guest framebuffer
Storage: ./storage mounted inside the guest as /storage via QEMU 9p
Persist: ./persist mounted inside the guest as /persist via QEMU 9p
```

Build the image before packaging:

```sh
scripts/build.sh qemu-mipsel
```

Then copy these files into `images/` beside the `.exe`:

```text
build/buildroot/output/images/vmlinux
build/buildroot/output/images/rootfs.cpio.gz
```

Alternatively, set `LILSMOKE_IMAGES` to a Buildroot `output/images`
directory. The launcher will copy complete images into its local `images/`
folder before booting.
