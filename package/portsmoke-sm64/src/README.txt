SM64 Native Gameplay Prototype 0.5.0

NATIVE MARIO GAMEPLAY PROTOTYPE. NOT THE COMPLETE GAME. NOT AN N64 EMULATOR.

Install the .lilpck through LilSmoke Applications. Put your own unmodified
8 MiB US ROM inside the installed application's original/ directory:
/storage/independent-apps/SM64 Native Prototype/original/
ANY filename is accepted, with or without an extension. Identification uses
the N64 header, normalized byte order and supported SHA-1, never the name.
z64/v64/n64 byte order is supported. Other games/revisions, symlinks and
multiple supported ROMs (even identical copies) are rejected. Text notes
without N64 headers are ignored; at most 64 regular files are considered.

First launch shows the supplied 128x128 WebP icon, a blue progress bar,
percentage and the actual number of MB extracted. Extraction runs off the
display thread; Back/Escape cancels without deleting the app's ROM copy.
The port prepares and verifies four durable private caches: Bob geometry,
Mario model/animation, 1,322 known assets and 76 complete MIO0 segments.
Their payloads total 14.08 MB. Only after all checks pass is the supported
ROM inside this app's original/ folder deleted. No unrelated file is deleted.
Subsequent launches reuse the verified caches without a ROM. A lost/damaged
cache or a future incompatible asset manifest requires supplying the ROM again.
Updates preserve caches, saves and any pending original ROM. No ROM-derived
assets, geometry, ROM, BIOS or game screenshots belong in the release package.

The prototype renders six static Bob-omb Battlefield display lists with
1,101 ROM-derived triangles and 18 textures using a pinned MIT TinyGL CPU
rasterizer, at 320x240. No system OpenGL/GLES/Vulkan/context/GPU is used.
SDL2 and native WebP runtime libraries are bundled; DirectFB and the M8 input/video/PCM adapters are supplied
by the OS. The launcher retains their preload/settings. Original Mario jump,
landing and hurt samples are decoded from the private US VADPCM sound bank.
SDL mixes at most four short voices and delivers 44.1 kHz stereo to native PCM.
Background music and the complete sound/event sequence player are not present.

49 supported objects are read from Bob's original macro list and level metadata:
yellow/red coins, Goombas, the cage star and the eight-red-coin objective.
Goombas patrol/chase, damage Mario with temporary invulnerability, and can be
stomped while descending. Stomping bounces Mario and drops a coin. Coin pickup
restores health; depletion respawns Mario. Eight red coins spawn their star.
Collected star bits are committed atomically to saves/bob-progress.dat and
restored on relaunch. Restart Level resets coins/enemies, not saved stars.
This is a bounded behavior adaptation, not the full original object engine.

Arrows/WASD, D-pad or left stick move Mario relative to the follow camera.
A/Left Ctrl/Space jumps; releasing A early shortens the jump. Right stick
orbits the camera. Start/Return opens a pause menu with Resume, Restart Level
and Exit. A confirms; Up/Down selects. Select/Escape and SDL_QUIT exit normally
with status 0 and SM64_NATIVE_MOVEMENT_PROTOTYPE_NOT_FULL_GAME.
Errors remain nonzero. The M8's native input/video adapters remain active.

Original Mario meshes and idle/walk/single-jump ROM animation samples are used.
Basic movement uses a bounded adaptation of the decomp collision/quarter-step
and gravity routines, with a fixed 30 Hz simulation independent of rendering.
The static Bob collision world has 570 vertices and 1,060 surfaces. Special
objects/dynamic collision and the complete original action engine are missing.
Missing: other courses, bosses/Koopa missions, cannon/flight, dynamic platforms,
advanced moves, complete camera/action logic, full save format and music.
Materials approximate texture-times-shade; fog, full RDP combiners, blending,
tile clamp and dynamic objects/skybox are not implemented. This is a scene
development prototype, not a complete playable SM64 port. Hardware FPS is
not established by host or Malta VM measurements.

Optional original texture/data diagnostics remain available:
runtime/bin/portsmoke-sm64-data --original original .sm64/sm64-us-v1.pdat
runtime/bin/portsmoke-sm64-data --verify .sm64/sm64-us-v1.pdat
runtime/bin/portsmoke-sm64-data --texture-checksums .sm64/sm64-us-v1.pdat

The extractor uses an atomic .pdat.lock.d directory, not flock/fcntl locking.
PID, process start time and boot identity distinguish a live owner from a
crashed process or reused PID. Recognized dead locks and interrupted .pdat.tmp
files are recovered conservatively; unknown/empty owner records, links and
unexpected files require inspection and are preserved. Do not remove a live
lock. Legacy .pdat.lock files are left alone. Atomic rename and fsync cannot
eliminate FAT power-loss damage: use safe Power Off and back up your own data.

Old /storage/portsmoke files are never changed or deleted. To migrate, copy
your own ROM into the installed app's original/ folder. Older root-level ROMs
are deliberately not scanned, moved, renamed or deleted. No shared .peng is used.
Renaming the application folder is supported by LILSMOKE_APP_DIR.

Never redistribute this cache, your ROM, extracted assets or BIOS.
The package includes MIT tools/TinyGL, public-domain font, SDL2 and licenses.
