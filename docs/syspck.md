# LilSmoke System Packages, format 1

.syspck is a signed, bounded 7z archive with a different extension.
The native C++17/musl tool installs allowlisted optimization settings and stages
hardware descriptions. It does not install arbitrary software or replace the
bootloader, kernel, rootfs, firmware or drivers.

Hardware DTB activation is deliberately unavailable until a verified
board-specific boot/rollback adapter exists. See cubegm-hardware-study.md.

## Files and integration

```text
package/lilsmoke-syspck/
  Config.in
  lilsmoke-syspck.mk
  src/syspck.cpp
  src/S08syspck
  examples/qemu-profile.ini
  examples/config.conf
tools/syspck/
  make_syspck.py
  analyze_cubegm.py

Device:
/usr/bin/lilsmoke-syspck
/usr/share/lilsmoke-syspck/empty/        root-owned decoder jail
/etc/lilsmoke/syspck-policy.conf         immutable image identity/approval
/etc/lilsmoke/syspck-keys/<KeyID>.pub    approved publisher keys
/persist/syspck/
  packages/<archive-sha256>.syspck
  previous.conf                        one-level settings rollback
  active                               current profile ID and archive hash
  pending                              interrupted transaction marker
  lock
/persist/config/lilsmoke.conf
```

/persist is the ext4 image inside LILOS, not another SD partition. Packages may
be copied onto LILROMS anywhere Explorer can browse, for example /storage/packages.
Do not edit that storage while LILDRIVE owns it.

Buildroot selects libarchive, xz, libfdt/dtc and libsodium; no target Python or
external input mapper is required. The image post-build hook derives the kernel
identity from the built vmlinux SHA256 when available. An unavailable kernel or
revision is marked UNVERIFIED, never approved automatically.

## Archive schema

Only these flat, regular-file members are accepted:

| Member | Limit | Requirement |
| --- | --- | --- |
| manifest.ini | 16 KiB | Always |
| manifest.sig | Exactly 64 bytes | Ed25519 signature of exact manifest bytes |
| config.conf | 8 KiB | Always, may be empty |
| dtb.bin | 512 KiB | Reference/hardware only |

The complete archive is at most 2 MiB, with at most 1 MiB expanded data.
Encryption, self-extracting files, other formats, outer filters, extra members,
directories, nested paths, links, duplicates and special files are rejected.
Author with non-solid LZMA2 and a 1 MiB dictionary. Larger decoder dictionaries
can exceed the resource limit even when the output is small.

The installer takes a private bounded byte snapshot; it never extracts archive
members onto a filesystem. Decoding happens in a child with closed inherited
file descriptors and no-new-privileges. When invoked by root, the child chroots
into the root-owned jail and drops all supplementary groups, UID and GID to
65534. The parent independently checks the bounded response, trusted signature,
payload hashes and policy before writing. A non-root check cannot chroot but
still has no elevated privileges. This isolation is not a full network/seccomp
sandbox or a substitute for updating archive dependencies.

Each native process has a 48 MiB address-space cap and 30 CPU-second limit.
Fork shares unchanged pages; these are per-process limits, not a measurement of
whole-OS RAM consumption. There is no compressed-volume or ROM-sized RAM buffer.

The reader registers only 7z with no outer filter and requires the 7z signature
at byte zero, excluding SFX/ELF paths before libarchive sees the data. Existing
Buildroot provides libarchive 3.7.4; later upstream 7z/SFX fixes and unrelated
RAR/ZIP advisories were reviewed at
[libarchive releases](https://github.com/libarchive/libarchive/releases) and
[security advisories](https://github.com/libarchive/libarchive/security/advisories).
This is a constrained reader, not a security audit of all linked library code.

## Manifest

Exactly one [SystemPackage] section; ASCII key=value lines, no interpolation,
duplicate keys, extra sections or unknown keys. Example authoring input:

```ini
[SystemPackage]
FormatVersion=1
PackageID=qemu-safe-profile
Version=1.0
Board=qemu-mipsel
HardwareRevision=malta-dev
SoC=qemu-malta
Architecture=mipsel
ABI=o32-hard-float
Libc=musl
KernelID=REPLACE_WITH_INSTALLED_POLICY_KERNEL_ID
DTBinding=none
Kind=userspace
RequiredRAMMB=128
KeyID=YOUR_APPROVED_SIGNING_KEY
```

The authoring tool adds ConfigSHA256 and, when present, DTBSHA256. PackageID,
Version and KeyID use limited ASCII tokens; KeyID names an already trusted key,
not a public key shipped inside the archive. RequiredRAMMB must be at least 16
and at most the image's 128/256 MiB hardware policy budget.

Board, HardwareRevision, SoC, Architecture, ABI, Libc and KernelID must match the
installed immutable policy exactly. Version identifies a profile; it is not an
anti-downgrade counter. Userspace rollback is intentionally supported.

| Kind | Behavior |
| --- | --- |
| userspace | No DTB, DTBinding=none; check, stage, apply allowed |
| reference | Valid DTB for study; check/stage only, never applied |
| hardware | Verified policy, matching binding/compatible/exact approved DTB hash; check/stage only until boot adapter exists |

Hardware DTBs must also have valid full FDT structure, exact total size and
bounded memory cells/regions. Data is aligned before libfdt access on MIPS.
Reference packages can describe a vendor tree that the running kernel cannot
use; their display in the UI must not be mistaken for activation approval.

## Optimization settings

config.conf is sectionless ASCII data. Only the following values are accepted:

| Key | Values |
| --- | --- |
| DISPLAY_ROTATION | 0, 90, 180, 270 |
| DEVICE_MODE | handheld, tv |
| TV_SAFE_AREA_PERCENT | 0, 2, 3, 5, 7, 10 (per edge, UI only) |
| SHOW_BATTERY | off, icon, text (TV mode suppresses the battery widget) |
| SCALING_MODE | integer, stretch, aspect |
| CPU_GOVERNOR | ondemand, performance, powersave |
| CPU_OVERCLOCK | stock only |
| ZRAM_ENABLED | off, on |
| ZRAM_SIZE_MB | 24, 32, 48, 64; at most half policy RAM |
| POINTER_SPEED | slow, normal, fast |

These use the existing settings and capability-gated services. Applying a
governor does not manufacture kernel CPUFreq support; a package cannot increase
PLL clocks, add a thermal sensor or exceed the board's physical RAM. Packages
cannot enable SSH/Wi-Fi/USB sharing or execute shell substitutions.

TV presentation changes the UI viewport and pointer coordinates without another
framebuffer allocation. It does not alter HDMI signal timing, audio devices,
game resolution or kernel capability. See m8-tv-common-image-study.md.

## Author and approve a package

On a Linux authoring PC: Python 3 + PyNaCl and 7za/7z are needed. Neither private
keys nor Python belong on the console.

```sh
python3 tools/syspck/make_syspck.py keygen \
  --private maker-private.key --public maker.pub
python3 tools/syspck/make_syspck.py pack \
  --profile profile.ini --config config.conf \
  --private maker-private.key --output safe-profile.syspck
```

Copy exact identity fields from the intended image's syspck-policy.conf into
profile.ini and set KeyID=maker. For reference/hardware, add the appropriate
Kind/DTBinding and pass --dtb path/to/dtb.bin. Native validation is still required;
the authoring command is not a hardware-certification tool.

No publisher or QA key is trusted by default. An image maintainer must verify
the publisher out-of-band and install its raw 32-byte public key as
/etc/lilsmoke/syspck-keys/maker.pub in the trusted image. Never enroll a key merely
because an untrusted package or website supplied it. Keep the private seed
offline, with restrictive permissions; the tool refuses overwriting key/output
files. Do not change HardwareValidated or ApprovedDTBSHA256 without physical BSP
validation. There is no "ignore compatibility" flag.

## Use from Explorer or terminal

Explorer: open a .syspck to validate; its context menu offers Validate package,
Stage package and Apply package settings. Applying requires explicit
confirmation; cancellation, a held A/Start repeat or a backdrop click changes nothing. Invalid
packages do not trigger a success callback. On success the UI reloads its
configuration, preventing a later Settings save from restoring stale values.
Renderer/service changes may still require restarting the UI or rebooting.

```sh
lilsmoke-syspck check /storage/packages/safe-profile.syspck
lilsmoke-syspck stage /storage/packages/safe-profile.syspck
lilsmoke-syspck apply /storage/packages/safe-profile.syspck
lilsmoke-syspck rollback
lilsmoke-syspck recover
```

Only check is permitted without root. Apply requires the persistent config that
normal UI startup initializes from the board defaults; it refuses an uninitialized
file instead of inventing a sparse config. Installation operations take an exclusive
lock and use private 0700 directories, 0600 files and fsync/rename commits.
Staged archives are named by their complete SHA256, with original bytes
preserved; a damaged existing staged copy is refused.

Apply saves the exact prior config, durably writes pending, atomically writes
the merged settings, commits active, then clears pending. Existing unrelated
settings are preserved; approved appended keys win in the existing config
reader. S08syspck runs recovery before the UI when pending exists. Recovery
restores previous.conf; rollback provides one prior configuration, not a full
firmware rollback or profile-history manager. Avoid concurrent manual config
edits from another terminal while installing.

Unexpected stale .new files in private state fail closed for administrator
inspection; not every arbitrary power-loss/SD failure is automatically repaired.
The filesystem containing /persist must already be mounted and healthy.

lilsmoke-syspck activate-dtb deliberately returns an error, including for an
approved signed hardware fixture. Safe activation needs a proven console boot
path, panel/audio/input/power/thermal drivers, immutable board identity,
known-good DTB fallback, power-failure handling and physical boot/rollback tests.
There are no raw SD/SPI flash or live /proc/device-tree writes in this installer.

## Verification

Run after building the target dependencies/tool:

```sh
bash tools/hwtest/build_syspck_mips.sh
bash tools/hwtest/validate_syspck.sh
bash tools/hwtest/validate_syspck_ui.sh
```

The tests use disposable directories, signed actual 7z files and private mount
namespaces. Evidence and QA-only examples are in test-results/syspck.
No physical card is modified. A rebuilt package/target UI is not a regenerated
Windows VM executable or a production-bootable console image.
