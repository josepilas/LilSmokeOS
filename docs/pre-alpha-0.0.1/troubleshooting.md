<!-- SPDX-License-Identifier: MPL-2.0 -->
# Troubleshooting

Use a spare card with verified backups. Never format a working stock card,
blindly replace its kernel/DTB or disable device-identity checks to try a fix.

## Black Screen Or Stock Menu

Check the exact board/firmware profile, completed image write, supplied checksum
and Device Adapter report. A stock logo followed by black can mean a userspace
launch/display failure, not a missing ROM. A stock menu instead of LilSmoke can
mean the launch hook did not activate. A copied DTB or renamed `main.syspck`
alone does not supply a compatible boot chain. Record the observed boot stages
and any redacted bring-up log; retain rollback files before changing anything.

## Windows Shows RAW Or Requests Repair

Cancel the format prompt. Recheck reader connection, card identity, partition
labels and whether both volumes are readable. A dirty flag is different from
unreadable files or controller I/O errors. Stop writes after actual read errors,
back up readable data and seek recovery before any repair. Repair tools can
alter files; do not run one without a backup. Use safe shutdown/eject on both
console and PC. Repeated corruption also warrants another reader/card/power
source, not just repeated filesystem repair.

## Games Missing Or Launch Error

Check `LILROMS/roms/<system>/`, supported extensions and installed provider,
then select **Scan games now**. A configured missing core will produce an
error rather than a silent substitute. Try a compatible provider and a legally
obtained plain ROM/full disc set. For archives, check cache space, required
companion files and compression limits. Preserve the original archive.
"RetroArch exited with error 1" is a symptom: obtain the corresponding launcher/
core log and core selection instead of assuming every game needs the same fix.

## Poor Speed, Tiny Image Or Missing Sound

Use defaults and the physically tested provider first. Disable expensive filters
and shaders. Original 4:3 game aspect on a 16:9 TV is intentional; a tiny centered
surface is not. Report both UI size and game size plus provider, scaling choice
and active resolution. Do not infer rendered detail from HDMI scanout.

Check master/player/emulator volume, mute and TV HDMI audio. Native M8 sound
does not rely on an ALSA device being present. A QEMU dummy-audio test is not a
hardware sound pass. N64/general PSX speed is constrained; PCSX4ALL is the
owner-confirmed PSX option, while PCSX ReARMed has known performance concerns.

## Controller Or Menu Problems

Record controller model, mode and cable/dongle/Bluetooth connection. Start with
the receiver present at boot. Check **Gamepad mapping** and the emulator's input
type. Test Start+X menu and Start+Select exit with full releases between chords.
Distinguish analog game input from an unintended mouse cursor. A native app's
own menu bindings may differ. Remove USB serials/private paths from shared logs.

## App Install/Update Failure

Check a root `meta.json`, mandatory name/version, exactly one root `.sh`, safe
paths, native MIPS/musl binaries and free staging space. A same-version package
is deliberately not reinstalled. Renaming an app retains its ID; duplicates
must not ambiguously claim the same ID. Do not manually delete recovery journals
to bypass an unfinished operation. Back up saves before uninstalling.

## Shutdown Does Not Confirm Success

Wait for **Power off operation succeeded** for ten seconds and subsequent
black screen. **Storage closed but SD needs repair** is a warning, not success.
**STORAGE BUSY WAIT** indicates resources still need closing. Finish transfers,
return storage ownership from USB sharing and record the exact warning. A black
screen without completion text is not sufficient evidence of safe shutdown.

## Reporting A Bug

Use the [repository issue forms](https://github.com/josepilas/LilSmokeOS/issues)
for a bug or hardware-support report. Include public release
version (or explicit private-build identifier), board/firmware, reproduction,
provider/controller/theme and expected/actual behavior. State whether it was
physical hardware, host or QEMU. Share relevant redacted log excerpts, not entire
card images, ROMs, BIOS, stock exports, credentials or private game captures.
