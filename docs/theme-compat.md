<!-- SPDX-License-Identifier: MPL-2.0 -->
# EmulationStation Theme Compatibility

Current behavior for **Alpha-0.0.1**. LilSmokeUI implements a CPU/software
subset of the theme model used by EmuELEC, ArkOS and related frontends.
It is no longer the early static J3 renderer. It is also not a complete copy
of EmulationStation or a guarantee of pixel-identical output for every theme.

## Theme Sets And Selection

```text
/storage/themes/<theme-set>/theme.xml
/storage/themes/<theme-set>/<system-theme>/theme.xml
```

The loader also searches `/storage/.config/emulationstation/themes`,
`/roms/themes`, `/userdata/themes` and `/usr/share/lilsmoke/themes`.
The default XML is loaded first; the per-system XML merges over it.
Common IDs map to EmulationStation theme folders, for example `md` to
`megadrive`, `pce` to `pcengine`, and `sms` to `mastersystem`.
Preserve the complete theme folder, relative assets and fonts.

Settings exposes **Theme**, **Theme system layout**, **Theme game list**
and **Theme transition**. The latter offers auto, slide, fade and instant.
Only choices actually implemented by a theme can change its layout.

## Supported Parsing

- XML declarations/comments, self-closing tags and CDATA.
- Relative includes, variables and substitutions such as
  `${system.theme}`, `${system.name}`, `${themePath}`,
  `${screen.width}` and `${screen.height}`.
- Configured/default subset selection and aspect-related choices.
- Multiple view names; matching element properties merge by type/name.
- Supported `ifSubset`, `ifHelpPrompts` and `tinyScreen` conditions.
  These do not constitute a general expression or architecture-filter engine.

## Rendered Features

| Feature | Alpha behavior |
| --- | --- |
| System view | Horizontal/vertical carousels and wheel layouts; selected logos/text and system information |
| Game views | Basic/detailed/grid layouts, text lists, image grids and legacy tile properties |
| Images | PNG/JPEG/WebP and decoder-supported raster formats; geometry, origin, tint, opacity, clipping and rotation |
| Text | Fonts, placement, wrapping/alignment, color and alpha |
| Layers | Extras and supported widgets ordered by `zIndex`, preserving order for equal depth |
| Metadata | Cover fallback, marquee path, labels/values, supported dates and rating |
| Help | Themed `helpsystem` prompts, text/icon styling and placement |
| Screen overlays | Supported clock and battery widgets, subject to settings/device data |
| Transitions | Auto/slide/fade/instant; carousel interpolation and wraparound |
| Storyboards | Bounded position, scale, opacity and rotation tracks with easing, delay, repeats and auto-reverse |

A `video name="md_video"` region currently displays the selected cover as a
static fallback. Its recognition is **not live preview-video decoding**.
A recognized `ninepatch` extra is drawn through the image path; do not assume
complete upstream nine-slice behavior.

## Animation And Background Semantics

Storyboard limits are deliberate: at most 32 tracks per element, bounded
durations/repeats and finite numeric transforms. The supported named/default
events are driven by the active views; arbitrary upstream events are not
automatically implemented. The parser preserves merge/override semantics
instead of appending duplicate animations indefinitely.

Shared, identical background scenery stays stationary while the system
carousel moves. Per-system backgrounds that differ can still transition.
An authored animated background is not forcibly frozen. Explicit static
background hints and matching scenery are handled separately from moving
logos/foreground extras.

The interface targets deadline-based 60 Hz scheduling, but actual frame rate
depends on artwork, layout complexity and CPU load. Bounded image/text/layout
caches, conservative idle reuse and native damage updates reduce repeated work.
Unsupported native surfaces fall back to SDL presentation rather than assuming
an accelerated renderer.

## Deliberate Gaps

- No OpenGL/GLES/Vulkan shaders or GPU effects.
- No live theme background/preview video; no animated-GIF playback contract.
- No theme sound-event engine, Lua scripting or arbitrary extension widgets.
- No complete Batocera expression language or all `ifArch`/custom-view semantics.
- SVG availability depends on the actual image-decoder build; raster assets
  are the safest portable choice. Large images can exceed low-memory budgets.
- A theme importing successfully does not prove every widget is rendered or
  that it will run at full speed on the device.

LilSmoke Applications/its installer and the utility screens have their own
UI; importing a theme does not replace them with an EmulationStation skin.

## Implementation And QA

The implementation is in `Theme.cpp`, `ThemeRender.cpp`, `ThemeAnimation.h`,
`ThemeMotion.h`, `SystemView.cpp`, `GridView.cpp` and `Renderer.cpp`
under `package/lilsmoke-ui/src/src/`.

The tests in `tools/ui/` cover parsing, bounded animation, retarget/wrap,
geometry, static backgrounds, native damage/cache lifecycle and idle reuse.
Host/MIPS VM captures demonstrate those software paths, not physical M8 FPS.
See the [Alpha release notes](alpha-0.0.1/RELEASE-NOTES.md) for the distinction.

Upstream format references:
[EmuELEC THEMES.md](https://github.com/EmuELEC/emuelec-emulationstation/blob/EmuELEC/THEMES.md)
and [RetroPie THEMES.md](https://github.com/RetroPie/EmulationStation/blob/master/THEMES.md).
Our compatibility matrix above, not upstream's full feature set, defines
what LilSmokeUI implements.
