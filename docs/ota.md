> Layout version 2 supersedes this historical partition-based design.
> LILOS is FAT32 p1 and LILROMS is exFAT/FAT32 p2. The old installer has been
> removed and install/activate are disabled until a verified file-based
> boot/rollback adapter exists. See [Storage layout](storage-layout.md).
> The remainder below records the old design; do not use its flashing commands.

# LilSmokeOS — A/B Partitions + OTA Updates (J4)

This document describes the J4 persistence layer: A/B partition layout,
OverlayFS for the rootfs, boot counter with automatic rollback, and the
OTA update flow.

## Partition layout (real hardware, J8)

| Part | Offset | Size | FS | Purpose |
|---|---|---|---|---|
| `p1` | 1 MB | 4 MB | raw | U-Boot + SPL + uEnv.txt |
| `p2` | 5 MB | 96 MB | ext4 | FIT boot **slot A** (`/lilsmoke.itb`) |
| `p3` | 101 MB | 96 MB | ext4 | FIT boot **slot B** (`/lilsmoke.itb`) |
| `p4` | 197 MB | rest | extended | Container for logical data partitions |
| `p5` | 198 MB | 16 MB | ext4 | `/persist` (configs, saves, bootcounter) |
| `p6` | 214 MB | rest | FAT32 | `/storage` (ROMs, BIOS, themes, scraped art) |

Total minimum SD card size: 256 MB. Recommended: 8 GB or larger.

**Key insight**: Each boot slot is an ext4 filesystem containing the
board FIT image (`/lilsmoke.itb`). Runtime user data lives on `/persist`
(ext4, journaled) and `/storage` (FAT32, for compatibility with stock
firmware's ROM-loading flow).

## Why A/B?

A single rootfs would force us to either:
1. Make the rootfs writable (FAT32/ext4) → power-fail corruption risk
2. Reflash the whole partition on update → brick risk if update interrupted

A/B solves both:
1. Each slot is replaced as a complete bootable FIT image
2. Updates write to the *inactive* slot -> no brick risk
3. If the new slot fails to boot 3 times, bootloader falls back automatically

## Boot flow

```
Power on
    │
    ▼
U-Boot (in p1)
    │
    │  Read /persist/next_slot (default: same as last boot)
    │  If /persist/slot_<X>_status = "bad", pick the other slot
    │
    ├─ If next_slot = A → boot from p2 (slot A)
    └─ If next_slot = B → boot from p3 (slot B)
    │
    ▼
Linux kernel boots with kernel cmdline: lilsmoke.slot=A (or B)
    │
    ▼
S01mounts — mount /proc, /sys, /dev, /persist, /storage
    │
    ▼
S05bootcounter — read /persist/bootcounter
    │              If >= 3 → mark this slot bad, reboot
    │              Otherwise → increment, write back
    ▼
S06overlay — mount OverlayFS on /etc and /var
    │            (lower = read-only squashfs, upper = /persist/overlay/*)
    ▼
S10-S40 — zram, network, dropbear, ALSA
    │
    ▼
S89bootok — wait 5 seconds, then:
    │           Reset bootcounter to 0
    │           Mark /persist/slot_<current>_status = "good"
    │           (This signals to the bootloader that this slot is healthy)
    ▼
S90lilsmoke-ui — start LilSmokeUI on tty1
```

## OverlayFS

The shipped system remains immutable at runtime. But users need to be able to:
- Change `/etc/wpa_supplicant.conf` (WiFi credentials)
- Add entries to `/etc/dropbear/authorized_keys` (SSH keys)
- Write logs to `/var/log`

Solution: OverlayFS.
- **Lower dir**: the original `/etc` (or `/var`) from the shipped image
- **Upper dir**: `/persist/overlay/etc` (writable, on ext4)
- **Merged view**: `/etc` (what apps see)

Changes to `/etc/*` are persisted across reboots. An OTA update can
replace the inactive FIT boot slot entirely; the user's overlay changes
are preserved on `/persist`.

> On QEMU we don't have a real `/persist` partition (initramfs), so
> S06overlay is a no-op. The rootfs is already writable in QEMU. J8
> (real hardware image) will set up the partitions properly.

## OTA bundle format

```
lilsmoke-update-<version>.tar.gz
├── manifest.json        # metadata (version, sha256, size, target slot)
└── lilsmoke.itb         # FIT image: kernel + dtb + initramfs/rootfs payload
```

`manifest.json` example:
```json
{
    "version": "0.3.0",
    "sha256": "abc123...",
    "size": 24567890,
    "min_version": "0.1.0",
    "release_notes": "Added N64 support (just kidding)"
}
```

## OTA workflow

```sh
# 1. Check for updates (fetches manifest.json, compares version)
lilsmoke-update check

# 2. Download the bundle to /persist/updates/
#    Verifies SHA256 against manifest
lilsmoke-update download

# 3. Format the inactive slot and install /lilsmoke.itb
#    Does NOT change boot order yet; just writes the inactive slot
lilsmoke-update install

# 4. Mark the inactive slot as the next-boot slot
lilsmoke-update activate

# 5. Reboot
lilsmoke-update reboot

# 6. System boots into new slot
#    S05bootcounter increments
#    S89bootok (if UI starts successfully) marks slot as good
#    If 3 failed boots → bootloader falls back to old slot
```

### Status check

```sh
lilsmoke-update status
```

Output:
```
LilSmokeOS OTA status
======================
Current version:  0.2.0
Active slot:      A
Inactive slot:    B
Next boot slot:   B
Boot counter:     0

Slot status:
  Slot A: good
  Slot B: pending

Downloaded bundles:
  24.5M /persist/updates/lilsmoke-update-0.3.0.tar.gz
```

## Rollback scenarios

### Scenario 1: New slot fails to boot at all
- Bootloader tries new slot
- Kernel panic / no output → manual reset
- Counter doesn't increment (no S05 ran)
- After 3 resets, U-Boot falls back to old slot
- (U-Boot uses a hardware watchdog for this — see J8)

### Scenario 2: New slot boots but LilSmokeUI crashes
- S05bootcounter runs, increments to 1
- LilSmokeUI crashes immediately
- inittab respawns it (BusyBox init)
- If it crashes 3 times within 60 seconds, init gives up
- User powers off
- Next boot: counter is still 1, increment to 2
- Repeat → 3, S05 marks slot as bad, reboots
- Bootloader picks the other (good) slot

### Scenario 3: User manually rolls back
```sh
# Force the system to use slot A on next boot
echo "A" > /persist/next_slot
reboot
```

## Save management

Game saves and states live on `/persist` (ext4, journaled):

```
/persist/
├── config/
│   ├── lilsmoke.conf          # main config
│   ├── wifi.conf              # wpa_supplicant config
│   ├── retroarch-core-options.cfg
│   └── asound.state           # ALSA mixer state
├── bios/
│   └── scph1001.bin           # PS1 BIOS (user-provided)
├── saves/
│   ├── <system>/<romname>.srm     # battery saves
│   └── pcsx4all/<romname>.mcr     # PS1 memory card
├── states/
│   └── <system>/<romname>.state   # save states
├── scrape/
│   ├── <hash>.json                # metadata cache
│   └── covers/<system>/<rom>.(png|webp|jpg)  # box art
├── overlay/
│   ├── etc/                       # OverlayFS upper for /etc
│   └── var/                       # OverlayFS upper for /var
├── updates/
│   └── lilsmoke-update-*.tar.gz   # downloaded OTA bundles
├── bootcounter                    # current boot count (0 = healthy)
├── active_slot                    # "A" or "B"
├── next_slot                      # optional override
├── slot_A_status                  # "good" | "bad" | "pending"
└── slot_B_status
```

All of this survives:
- Power loss (ext4 journal)
- OTA updates (the slot partitions change, but /persist doesn't)
- Rollback (the old slot still reads the same /persist)

## Remaining work

- **Hardware rollback validation** — the U-Boot env now reads
  `/persist/slot_*_status` and picks p2/p3, but the full reset/watchdog
  path still needs board-level validation on SF3000/GB350 hardware.
- **Delta updates** — we ship a complete FIT image every time. For
  handheld use this is fine; delta updates add complexity.
- **Signed updates** — J6 will add Ed25519 signature verification.
  For now we trust the HTTPS channel + SHA256.
- **Background downloads** — the user has to manually trigger
  `lilsmoke-update download`. J6 may add an auto-check-on-WiFi-connect.
