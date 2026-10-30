# LilSmoke Shell Terminal

Settings > Shell Terminal launches a real interactive `/bin/sh -i` attached
to a Unix98 PTY. This is not a command textbox. SDL2/DirectFB reads hardware
keyboard events and draws to a CPU window surface; no X11, GL or GPU is used
on the device. libvterm 0.3.3 handles the terminal parser, screen state,
alternate screen, terminal replies and special-key encoding.

## Integration

The Settings action uses the existing `ProcessLaunch` request. `App` destroys
input, textures and SDL/DirectFB before execing
`/usr/lib/lilsmoke/lilsmoke-terminal-launch`. That wrapper runs the terminal,
then execs `/usr/bin/lilsmoke-ui`, preserving the supervised wrapper PID and
restoring one UI after normal exit, helper failure or helper crash. It does
not start a second UI alongside the shell. Shutdown signals stop the wrapper
instead of restoring the UI. The UI returns to its initial system view, not
the previous Settings selection. A missing helper is reported in Settings.

No changes are made to `Input.cpp`, gamepad code, M8 boot launcher, SM64,
FFmpeg, SDK configuration/target, images or physical media. There is no full
UI build. The parent integrator must rebuild/install these source changes.

## Runtime Installation

Enable `BR2_PACKAGE_LILSMOKE_TERMINAL=y` (defaults on with LilSmokeUI where its
toolchain prerequisites hold). The package selects the new pinned libvterm
recipe, SDL2/DirectFB, SDL2_ttf, DejaVu Mono and ncurses. It installs:

- `/usr/bin/lilsmoke-terminal`
- `/usr/lib/lilsmoke/lilsmoke-terminal-launch` (mode 0755, LF shell script)
- Dependencies: target `libvterm.so.0` and its symlink/versioned file, existing
  matching MIPS/musl SDL2, SDL2_ttf, FreeType and C++ runtime dependencies.
- `/usr/share/fonts/dejavu/DejaVuSansMono.ttf` from `dejavu`.
- `/usr/share/terminfo/x/xterm-256color` from `ncurses`. Buildroot 2024.02.3
  already includes this in its standard vital terminfo list.

Keep libvterm's MIT license and source obligations with distributed artifacts.
The isolated cross-check statically links libvterm for testing only; the
Buildroot package uses the shared libvterm dependency.

The running kernel needs `CONFIG_TTY=y`, `CONFIG_UNIX98_PTYS=y`, devpts support,
a usable `/dev/ptmx` and mounted `/dev/pts` **inside the UI chroot**. These are
not provided by installing a libc or a terminal binary. `/proc` should also
be mounted (used for efficient inherited-descriptor cleanup).
Hardware keyboard input requires the DirectFB `linux_input` driver (selected
by the package), kernel HID/evdev support and readable keyboard devices under
`/dev/input/`. Physical keyboard/layout testing remains a device acceptance step.

Important handoff to the M8 launcher owner: the inspected `m8-launch.sh` uses
`mount --bind /dev "$ROOT/dev"`, not a recursive bind. A nested host `/dev/pts`
mount does not follow that bind. After binding `/dev`, the owner should bind
the existing host `/dev/pts` onto `$ROOT/dev/pts`, or mount devpts there if no
host instance exists, and unmount that nested mount before `$ROOT/dev` in
cleanup. Verify the PTMX node/symlink refers to that instance and permissions
allow the UI user to open master and slave devices. This change is intentionally
NOT made in this patch. The terminal does not mount filesystems itself.

Run in the final running chroot before enabling the menu on hardware:

```sh
/usr/bin/lilsmoke-terminal --check-pty
```

This allocates a PTY, establishes a controlling tty, execs `/bin/sh -i`, sends
`exit` and reaps it. It does not initialize a display. It still needs the
binary's shared-library dependencies. A successful probe does not verify the
physical keyboard, display adapter or launcher cleanup; exercise those too.

## Verified Platform Evidence

Read-only inspection of the owner-supplied recovered vendor `vmlinux.uImage`
(`tools/device-adapter/tests/artifacts/owner-stock-private-export-03/files/cubegm/`)
found Linux `4.4.186-release`, build #22, 2025-09-09. Its decompressed image
contains the Unix98 PTM/PTS driver registration diagnostics, `ptmx` and devpts
driver diagnostics, and embedded initramfs instructions mounting devpts.
The source image SHA256 is
`14cebc92fcd8d2fa8226bab4275e3d206fb72017cf574ddbff73a967d0ed56b5`.
No embedded IKCONFIG could be extracted, so exact kernel config text was not
verified. This is binary evidence of support, not a live-device mount test.

SDK musl `libc.so` exports `posix_openpt`, `grantpt`, `unlockpt`, `ptsname_r`
and `tcsetpgrp`. The helper links without `libutil`. The isolated MIPS binary
is ELF32 little-endian MIPS32r2/o32, using the SDK's existing ABI defaults.
Its `--check-pty` passes under QEMU user mode with `-cpu 74Kf` and SDK sysroot.
That probe uses the host kernel and `/bin/sh`, not vendor hardware.
The host PTY suite also passes with the SDK's MIPS BusyBox ash substituted
through `tools/terminal/mips-shell-fixture.sh` under QEMU. That exercises the
actual target shell's job control, editing/terminal setup and exit behavior,
but still uses a host PTY and host external commands.

## Behavior And Bounds

- Canonical shell line editing, job control and foreground terminal signals
  belong to Linux and the shell, not to a UI command parser.
- Ctrl-C/D/Z, other classic ASCII control bytes, arrows, Home/End,
  Insert/Delete, Page Up/Down, F1-F12, Shift-Tab, Alt and AltGr are supported.
  Printable input uses SDL UTF-8 text events; control/special key text events
  are suppressed to avoid duplicate keystrokes on DirectFB.
- Shift-PageUp/PageDown and mouse wheel scroll history; Shift-End returns to
  live output. Typing also returns to live output. Escape is sent to the
  shell/application; it is not an exit shortcut. Use `exit`, Ctrl-D at an
  empty prompt, or a desktop window close to leave the terminal.
- Maximum 100 rows x 240 columns; 128 history lines in a preallocated ring.
  History is not reflowed or pulled back into the live screen on resize.
  Wider historical rows are clipped; narrower rows are padded. Alternate
  screen content does not enter shell history. CSI 3 J clears scrollback.
- Nonblocking master PTY, 16 KiB bounded atomic output queue, at most 64 KiB
  reads and 128 SDL events per turn. Queue overflow rejects a whole generated
  key/reply and is visible in the status bar and stderr; it cannot grow memory.
- Software cell-damage rendering, at most 30 paints/sec, bounded glyph cache
  of 512 entries / 2 MiB, no video/audio/gamepad workers. Unsupported font
  glyphs use the font's fallback; adding comprehensive CJK fonts is separate.
- Resize updates libvterm and uses `TIOCSWINSZ`; Linux notifies the foreground
  process group with SIGWINCH. PTY EOF/EIO and child exit are handled without
  a busy read loop. Session close sends HUP, then bounded KILL escalation and
  reap attempts for the shell/foreground group. Intentional detached/nohup
  jobs retain normal Linux semantics.
- The shell runs as the current UI user, normally root on this device. It is
  a full local Linux shell with that user's privileges, not a sandbox.

The helper inherits the launcher's SDL/display adapter environment. Optional
developer-only environment settings: `LILSMOKE_TERMINAL_WINDOWED=1`,
`LILSMOKE_TERMINAL_FONT` (monospace font path),
`LILSMOKE_TERMINAL_FONT_SIZE` (10-24, default 16), and existing
`LILSMOKE_SCREEN_WIDTH/HEIGHT`. Fullscreen prefers the SDL current/desktop
display mode and uses fullscreen-desktop, drawing at actual window pixels,
not a stretched 640x480 logical surface. The environment dimensions are used
for windowed testing or as fallback when display mode discovery fails. Modes
outside 320x200-1920x1080 are rejected before acquiring a window surface.
No persistent settings schema is added.

Cross CMake validates every imported pkg-config include against configured
target sysroots. An absolute staging path prefixed a second time by pkgconf
is repaired only if the unprefixed directory exists and resolves inside a
trusted sysroot. Missing directories, host paths and symlink escapes fail
configuration. No SDK `.pc` file is edited and no missing directory is ignored.

## Tests

Run these Linux-host scripts with an isolated `LILSMOKE_TERMINAL_TEST_OUT`:

```sh
sh tools/terminal/test.sh
sh tools/terminal/cross-check.sh
sh tools/terminal/wrapper_test.sh
sh tools/terminal/gui_test.sh
sh tools/terminal/production-cmake-check.sh
```

To additionally run the PTY tests with the existing MIPS BusyBox shell:

```sh
LD_LIBRARY_PATH="$LILSMOKE_TERMINAL_TEST_OUT/prefix/lib" \
LILSMOKE_TERMINAL_TEST_SHELL="$PWD/tools/terminal/mips-shell-fixture.sh" \
    "$LILSMOKE_TERMINAL_TEST_OUT/host/terminal-tests"
```

For the parent integrator, in their configured Buildroot tree (not run here):

```sh
# First enable BR2_PACKAGE_LILSMOKE_TERMINAL=y in the resolved configuration.
make -C "$BUILDROOT" O="$BUILD_OUTPUT" -j1 libvterm lilsmoke-terminal
make -C "$BUILDROOT" O="$BUILD_OUTPUT" -j1 lilsmoke-ui-rebuild
```

The registered external tree must be this patched source tree. The parent
owns configuration resolution, package installation, payload dependencies and
the live-chroot devpts mount. The commands above do mutate that parent's
Buildroot output; only the isolated tests were run for this change.

Host dependencies: CMake, C++17 compiler, make, libtool, curl, pkg-config,
SDL2/SDL2_ttf development packages and DejaVu Mono. GUI tests additionally
need Xvfb, xdotool and ImageMagick. X11 is only a host test backend, never a
target dependency. `cross-check.sh` reads `SDK` (default existing local SDK),
builds only the new helper/libvterm single-job, and syntax-checks the changed
Settings translation unit; it never invokes a Buildroot/full UI build or
installs into SDK staging/target.

Parser/queue tests cover color, cursor positioning, Unicode/wide/combining
cells, alternate screen, history limits/clear, resize, replies and modifiers.
Real PTY tests cover all three tty descriptors, shell foreground group,
`stty size`, Ctrl-C of a foreground job, Ctrl-D exit, output-flood
responsiveness, exec failure, bounded shutdown and session reuse. Wrapper
fixtures verify one UI restoration with the original supervised PID after
statuses 0/1/127/139, plus forwarding of shutdown without UI restoration.
GUI screenshots check 640x480, 320x240, live resize and native 1280x720
fullscreen despite stale 640x480 environment hints. Production cross-CMake
uses the existing SDK pkg-config wrapper and broken SDL metadata unchanged,
builds the real project, and installs only to an isolated DESTDIR.

Primary references:
[libvterm upstream](https://www.leonerd.org.uk/code/libvterm/),
[SDL2 text events](https://wiki.libsdl.org/SDL2/SDL_TextInputEvent),
[SDL2 DirectFB event implementation](https://github.com/libsdl-org/SDL/blob/SDL2/src/video/directfb/SDL_DirectFB_events.c),
[Linux 4.4 devpts documentation](https://github.com/torvalds/linux/blob/v4.4/Documentation/filesystems/devpts.txt).

## Source Inventory

New package: `src/{Terminal.h,Terminal.cpp,Pty.h,Pty.cpp,Keyboard.h,Keyboard.cpp,
main.cpp,tests.cpp,CMakeLists.txt,PkgConfigPaths.cmake,lilsmoke-terminal-launch,LICENSE}`,
`Config.in`, `lilsmoke-terminal.mk`, this README.

New dependency recipe: `package/libvterm/{Config.in,libvterm.mk,libvterm.hash,CMakeLists.txt}`.
The CMake adapter builds unchanged upstream C files with the target compiler;
it avoids using a host-configured libtool for MIPS linking.

Tests: `tools/terminal/{test.sh,cross-check.sh,gui_test.sh,wrapper_test.sh,final-check.sh,
fixture-terminal.sh,fixture-ui.sh,mips-shell-fixture.sh,production-cmake-check.sh,
pkgconfig_paths_test.cmake,directfb_vm.py,vm-init.sh}`.

Integration edits only: top-level `Config.in`, `external.mk`, and
`package/lilsmoke-ui/src/src/{SettingsView.cpp,SettingsView.h}`. Existing
`ProcessLaunch`, `main.cpp` teardown and the actual boot launcher are unchanged.
