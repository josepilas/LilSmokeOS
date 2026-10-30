<!-- SPDX-License-Identifier: MPL-2.0 -->
# LilSmokeUI Frontend

Current **Alpha-0.0.1** architecture. Earlier J3 descriptions of a static
system list, eight-core-only UI and unconditional rescan are superseded by
this document. See [release changes](alpha-0.0.1/RELEASE-NOTES.md).

LilSmokeUI is a C++17, controller-first frontend using software SDL/DirectFB
and board-specific framebuffer adapters. It does not require a GPU, desktop
compositor, X11, Wayland or an accelerated graphics API.

## Views And Services

| Area | Current responsibility |
| --- | --- |
| SystemView | Theme-aware system carousel, animation, fixed/shared scenery and selection |
| GridView | Themed game list/grid, metadata, Y search, held-R2 traversal and game options |
| Recents/Favorites | Persistent collections referring to the existing ROM catalogue |
| Applications | Independent app grid, package installer, rename/uninstall and app launch |
| TreeFrog Ports | Separate direct-compatibility catalogue and original-port handoff |
| OpenDingux | Engine/application-owned menu launch; not a replacement ROM selector |
| Explorer | Files/archives/packages and media handoff |
| Settings | Provider selection, scan, display/theme/input and guarded device services |
| Theme/ThemeRender | XML merge/variants, rendering, bounded animation and layer order |
| ROMScanner | Library discovery, cache and metadata/playlist integration |
| Input/Keymap | Evdev discovery, mappings, analog state, capture and pointer events |
| Renderer/Font | Software drawing, bounded caches, damage/fallback presentation |
| Launcher/session | Validated provider dispatch, low-memory process handoff and return |

Relevant source is under `package/lilsmoke-ui/src/src/`. Helpers are in
`package/lilsmoke-scripts/src/`; M8 integration is under
`board/segam-m8-v6-stock/`.

## Frame Loop

1. Poll SDL/evdev events, coalesce eligible motion and dispatch input.
2. Apply pending view, scan, theme or process-launch actions.
3. Query the view's activity and composition-invalidating state.
4. Compose when needed; active animations and pointer/keyboard overlays remain
   on a conservative redraw path.
5. Present through supported native dirty-region output or SDL fallback.
6. Sleep to the next deadline, targeting 60 Hz without burst catch-up frames.

Caches retain glyphs/text runs, images, parsed properties and stable layout.
They have bounded retention and invalidation rules. Idle reuse is not permission
to freeze an active animation, clock, changed theme or exposed window.

`UI_PROFILE`, `UI_FRAME_ACTIVITY`, `UI_REUSE_PROFILE`,
`UI_DAMAGE_PROFILE` and `UI_DRAW_PROFILE` logs separate input/render/present
work. A configured pacing target is not a measured device FPS guarantee.

## Launch And Return

1. Selecting a ROM resolves the same provider registry shown in Settings.
   Missing/incompatible explicit choices produce an error.
2. A pending process request lets the App destroy SDL/DirectFB resources
   before replacing the UI process; the full frontend is not retained beside
   the emulator on a 128 MiB device.
3. `lilsmoke-session` and the game/provider helpers preserve logical content
   identity, resolve supported archives, configure native adapters and run
   the selected engine.
4. The board supervisor relaunches the frontend on a normal return.
   A warm-return marker allows reuse of an unchanged ROM cache/playlist
   snapshot instead of rebuilding the library every time.
5. A forced/manual scan invalidates that shortcut and discovers external
   changes. Failure and shutdown are handled separately from normal return.

Applications and direct TreeFrog ports have distinct launch contracts.
Their compatibility runtimes are not silently interchangeable, and legacy
PortSmoke support is not the current Applications package architecture.

## Themes And Utility Screens

The [theme matrix](theme-compat.md) documents current carousel, storyboard,
metadata, help and transition support. Utilities and the Applications grid/
installer keep their own design rather than adopting every theme's layout.
Theme `md_video` regions use a static cover fallback.

## Input

The [input guide](input.md) describes actual raw capture and device handling.
Y search and held-R2 navigation are game-list actions; Start+X/Start+Select
are adapter/engine shortcuts, not unconditional global keyboard injection.

## Hardware And Validation

The release reference is HCSEMI SEGAM-M8-V6.0, not a generic Ingenic JZ4770.
The M8's native PCM route is not ALSA, and vendor glibc workers stay isolated
from the musl UI. The 2D scanout scaler is not hardware 3D acceleration.

No current universal binary-size/RSS/boot-time/FPS measurement is asserted
here. Old J-job performance tables were design/development notes, not
certification of this release. VM tests use substitute devices; exact physical
image acceptance and per-game measurements must be reported separately.
