# Controller Reliability Handoff, 2026-10-18

## Compatibility And Scope

The shared file remains ABI 1: magic 0x4c534750, 424 bytes, four 100-byte pads
at offset 24, unchanged button positions and six signed 16-bit axes. Reader
generation tracking is local metadata, not a wire change. The frozen old reader
algorithm and the new reader both pass host and real MIPS helper tests.

Preserve the cached public SM64 game (ea51c64...) without recompilation or cache
invalidation. Only the parent's planned bootstrap/runtime rebuild is needed.
This agent did not change bootlauncher, SettingsView, SM64 public files, FFmpeg,
SDK build/staging/target contents, or physical SD contents. The parent applied
the SDL source patch and owns production builds, deployment and hardware QA.

## Concrete Causes And Fixes

- PocketSNES had no shared-state reader. Its evdev path could not receive a
  boot-X-only controller on the M8, and SDL fallback could contend for USB or
  retain stale joystick input. It now consumes shared state directly, assigns
  two players, and remains authoritatively neutral after daemon loss/unplug.
- SDL 2.28.5's successful /dev inotify watch only recognizes hidraw changes.
  Nested usbfs node changes need not advance its HID discovery counter. The
  daemon opts into bounded 250ms discovery through LILSMOKE_HIDAPI_POLL.
- SDL joystick enumeration itself opens HID USB interfaces. The SDL patch
  reserves HID ownership for the daemon while the shared-state pathname exists.
  Other SDL joystick consumers clean up their HID driver and do not claim USB;
  the daemon's polling hint exempts it. This benefits cached old clients too.
  Presence intentionally includes stale files: daemon loss must not trigger
  competing USB fallback. Without a state file, normal SDL behavior remains.
- PS raw-HID used a single VID/PID-selected handle and a constant instance ID.
  It now opens each eligible interface-0 path separately, supports multiple
  receivers, gives reconnects distinct instances, validates a packet before
  announcing connected, clears failed/removed handles, and retries acquisition.
- The old PS path cleared held buttons/axes after 1000ms without a report.
  Healthy report-on-change devices can be silent while held. That false expiry
  is removed; daemon state heartbeats continue every loop while idle.
- Updated UI/Apps and RA consumers recognize mode/instance/PID/state-file
  generations, reload profiles, and release replaced input. Common Input owns
  its reader per instance and emits aggregate multi-source releases, including
  native EOF/errors and SYN_DROPPED. Cursor and analog behavior are preserved.
- Apps adapter Makefile dependencies now rebuild input.o/common-input.o/
  common-keymap.o when shared headers change, avoiding mixed Input class sizes.

## Required Parent Builds

1. SDL2 2.28.5: apply the complete current
   package/lilsmoke-usb-gamepads/sdl-input/0001-sdl-hidapi-usbfs-poll.patch before
   sdl2-rebuild. An already-patched Buildroot tree will NOT rerun its new
   POST_PATCH hook just because sdl2-rebuild was requested. The parent has now
   applied it with --fuzz=0 and completed the SDL rebuild. Preserve the existing
   SDL graphics/audio options and HIDAPI/libusb support.
2. SDL staging metadata: run sdl2-reinstall after the final controller .mk hook
   update. It replaces both canonical STAGING_DIR and BASE_DIR/staging DirectFB
   paths in sdl2.pc with ${includedir}/directfb, and in sdl2-config with
   ${prefix}/include/directfb. No further SDL code rebuild is needed for this
   metadata correction. Reconfigure consumers after repair; do not reuse stale
   CMake/PkgConfig SDL2 include-path cache values.
3. Daemon: compile package/lilsmoke-usb-gamepads/src/usb-gamepads.c against the
   canonical package/lilsmoke-ui/src/src/gamepad-state.h and SDL2 headers, using
   the existing MIPS target flags, -std=gnu11 -O2 and -lSDL2.
4. LilSmokeUI: rebuild all objects using the current Input.h/Input.cpp/header.
5. PocketSNES: rebuild m8_pocketsnes.c, m8_pocketsnes_evdev.c and their sal
   callers using Makefile.lilsmoke. Keep the existing emulator patches; the
   0006 sal input hook now receives shared input through m8_evdev_poll.
6. RetroArch: copy current m8_lilsmoke_input.c/.h and canonical gamepad-state.h
   into its input/drivers_joypad build tree, then rebuild/relink the frontend.
   Use the lilsmoke joypad driver. No libretro core rebuild is required.
7. odSDL: rebuild libod-sdl.so from its Makefile with the current common Input.
   Do not retain input.o built against an older Input class layout.
8. Bundled PCSX4ALL: rebuild/relink pcsx4all-launch.cpp, native-menu.cpp and
   common Input/Keymap/Log against the UNCHANGED libpcsx4all_rkgame.a. Regenerate
   the bundled PCSX4ALL .lilpck with tools/apps/build_pcsx4all.py. Do not rebuild
   the PSX core merely for this input update.

## Runtime Deployment Set

- /usr/lib/libSDL2-2.0.so.0.2800.5 and the existing libSDL2-2.0.so.0/libSDL2.so links.
- /usr/bin/lilsmoke-usb-gamepads and /usr/bin/lilsmoke-ui.
- /usr/lib/lilsmoke/od-emulators/pocketsnes and /usr/bin/retroarch.
- /usr/lib/lilsmoke/libod-sdl.so and the regenerated bundled PCSX4ALL .lilpck.
- The parent's rebuilt SM64 bootstrap/runtime consumer only, not the cached game.

Keep existing controller-private libusb and the daemon's
LD_LIBRARY_PATH=/usr/lib/lilsmoke-usb:/usr/lib:/lib. No new kernel/evdev module,
mapping migration, or protocol migration is required. Do not deploy .pc files
as a runtime fix. Stop/restart affected processes around deployment so old
mapped libraries/USB owners do not survive the upgrade. Normal subsequent
controller add/remove/mode changes require no reboot.

## Validation

tools/gamepads/hotplug-validation-2026-10-18.json records the merged checks.
The main suite passes host, ASan/UBSan, and qemu-mipsel -cpu 74Kf helpers for
connect/remove, X/D/PS state identities, two pads, mode/profile replacement,
same-PID/instance state-file replacement, consumer restart, stale/torn/future
state, analogs/triggers, and both Start chords. Native gamepad regression has
30 checks; cursor regression has 448 checks in each execution mode.

Actual host and MIPS daemon processes pass >1100ms idle-heartbeat, single-owner
lock, and clean neutral shutdown checks. The SDL discovery and USB ownership
gates pass host/MIPS deterministic tests, and both complete patched SDL input
translation units compile to MIPS objects. Metadata fixtures test canonical
and staging-alias paths, pkg-config sysroot use, sdl2-config and idempotence.
Builds are serial isolated helpers, not whole UI/emulator/SDK builds.

Run the complete suite as root in Ubuntu-24.04:

    python3 "$P/tools/gamepads/test_hotplug.py" --output /root/lilsmoke-hotplug-final

P is this project root. No test writes the production SDK or card. SDL fixtures
accept an already-patched SDK only when the complete reverse dry-run succeeds
with --fuzz=0; partial/divergent sources fail rather than being silently accepted.

## Hardware Acceptance Still Required

No physical hotplug claim is made by the helper tests. The old USB log does not
contain enough low-level PS read-error detail to prove a receiver/kernel fault.
The new logs distinguish opened from connected/validated_report and record read
versus inventory removal. On actual M8, confirm boot-X-only PocketSNES, repeated
X/D/PS cycling in every consumer, unplug while held, daemon kill/restart while
held, >1000ms idle/hold, two receivers, and D-mode keyboard/mouse cursor behavior.
Generic new controllers remain subject to SDL2 HIDAPI protocol/mapping support.
