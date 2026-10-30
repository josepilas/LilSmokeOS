Undertale-LilSmoke 0.1.0 - experimental, CPU-only with SDL audio

CPU-only GameMaker WAD16 interpreter and RGB565 software renderer.
No game data, generated atlas, game music or firmware is supplied.
See QUALIFICATION.txt for the exact bounded tests performed on this build.
This is not a complete playthrough or physical hardware qualification.

Bring your legally obtained Undertale 1.08 WAD16 data.win. Put it in this app's
original/ directory. original/game.unx and original/game.ios are alternatives.
Include your own external .ogg files beside data.win for a complete data set.
The OGGs are private game data too. Audio is decoded incrementally and mixed
through SDL2; no predecoded song cache is supplied or required.
Do not copy the Windows EXE or DLLs: this port does not run Windows code.
First launch prepares the ATLAS files on-device using a native MIPS converter.
A software RGB565 screen shows stage, percent, source MB and atlas MB.
B/Escape cancels; partial output never becomes a reusable complete cache.
Later launches validate and reuse cache/<source-hash>-v1/ rather than repack.
Changed data produces a new cache. Corrupt caches are preserved as .invalid
and rebuilt. original/ is NEVER removed or changed by preparation.
Originals, generated caches and saves are absent from the public package.
Allow at least 128 MiB free space before preparing; keep old saves backed up.
Do not use the copyrighted atlas offered on upstream's release page.
Optional external files used by the game must also come from your own copy.

Physical A = Z (confirm), B = X (cancel), X/Y/Select = C (menu),
arrows/left stick = movement, Start = Enter, Start+Select = exit.
Keyboard: arrows and Z/X/C/Enter. Close the window to quit on desktop.
Native input: evdev keys, standard SDL controllers, existing LilSmoke gamepad
transport, read-only stock /tmp/joy_key shared memory. No background mapper.
This first adapter does not import UI custom evdev keymap profiles.
Saves go to saves/, never to the original game directory. Back up saves.
Uninstall may remove original/data/cache/saves inside the app directory.
The engine is not a sandbox. Load only trusted, legally obtained game data.

Linux MIPS32r2 little-endian, o32 hard-float, musl. Generic software SDL2,
or LILSMOKE_HCFB=1 with the OS /os/libm8-display.so native presenter ABI.
Physical M8 HDMI/input/performance are not qualified by user-mode QEMU.
No OpenGL, GLES, Vulkan, SF2000 bare-metal loader or firmware calls.
Physical speaker output, latency and handheld performance are not qualified.
Video playback and Box2D are disabled.
Large atlas blocks are capped at 320 pixels using integer box filtering.
This matches FroggyTale's atlas format, but is not pixel-identical to Pillow
RGBA box filtering. No GPU path or full-page texture allocation during play.

License: licenses/AGPL-3.0.txt; upstream and vendor notices retained in source.
This modified program is provided WITHOUT ANY WARRANTY.
You may convey/modify it under AGPLv3. See SOURCE-OFFER.txt for source.
