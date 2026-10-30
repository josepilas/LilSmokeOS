# EmulationStation Theme Compatibility

LilSmokeUI loads the EmulationStation theme-set structure used by EmuELEC,
ArkOS, RetroPie, and related systems:

```text
/storage/themes/<theme-set>/theme.xml
/storage/themes/<theme-set>/<system-theme>/theme.xml
```

It also checks these roots, in order:

```text
/storage/themes
/storage/.config/emulationstation/themes
/roms/themes
/userdata/themes
/usr/share/lilsmoke/themes
```

The default `theme.xml` is loaded first. If a system-specific theme exists,
`<system-theme>/theme.xml` is loaded next and merged over the default. System
theme names follow EmulationStation's `<theme>` value convention; LilSmokeUI
maps short launcher IDs to common ES folder names, for example `md` to
`megadrive`, `pce` to `pcengine`, `sms` to `mastersystem`, and `arcade` to
`arcade`/`fbneo`/`fba`/`mame`.

## Supported

- XML declarations, comments, self-closing tags, and CDATA text.
- `<include>` files, resolved relative to the file that includes them.
- EmuELEC subset includes: the first include per subset is used as the default.
- `<variables>` plus `${variable}` substitution, including `${system.theme}`,
  `${system.name}`, `${themePath}`, `${screen.width}`, and `${screen.height}`.
- Multiple view names split by commas or whitespace.
- Element merge semantics: later files override matching `type + name`
  properties instead of duplicating the element.
- Raster image backgrounds and extra images via SDL2_image.
- `image name="background"`, `image name="logo"`, `text name="logoText"`,
  `textlist name="gamelist"`, `imagegrid name="gamegrid"`, and older
  `gridtile name="default"/"selected"` properties.

## Deliberate Gaps

- Videos, animations, carousels, ratings, metadata labels, badges, and shader
  effects are parsed but not rendered by the current LilSmokeUI views.
- SVG support depends on SDL2_image being built with SVG support. PNG/JPEG
  assets are the safest theme target.
- Conditional attributes such as `ifArch`, `ifSubset`, `ifHelpPrompts`, and
  Batocera expression filters are currently ignored except for subset include
  default selection.
- Custom views can inherit parsed properties, but LilSmokeUI renders only its
  system list and game grid screens today.

Primary references:

- EmuELEC EmulationStation `THEMES.md`: https://github.com/EmuELEC/emuelec-emulationstation/blob/EmuELEC/THEMES.md
- RetroPie EmulationStation `THEMES.md`: https://github.com/RetroPie/EmulationStation/blob/master/THEMES.md
