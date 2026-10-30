# LilSmokeOS — Theme Creation Guide

This guide explains how to write a LilSmokeOS theme. LilSmokeOS themes
are XML + PNG/JPEG/WebP, deliberately compatible with a subset of the
EmuELEC and ArkOS theme formats so you can drop existing themes into
`/storage/themes/` and they'll mostly "just work".

## 1. Where themes live

| Location | Purpose |
|---|---|
| `/usr/share/lilsmoke/themes/default/` | Built-in default theme. Read-only. |
| `/storage/themes/<name>/theme.xml` | User-installed themes. Read-write. |
| `/persist/config/lilsmoke.conf` | The `DEFAULT_THEME` key picks which one. |

The Settings → Theme menu lists every theme found in `/storage/themes/`
plus the built-in default.

## 2. File layout

A theme is a folder containing:

```
my-theme/
├── theme.xml           # Required. The theme descriptor.
├── art/                # Optional. PNG/JPEG/WebP assets.
│   ├── background.png
│   ├── logo.png
│   ├── font.ttf
│   └── system/
│       ├── nes.png
│       ├── snes.png
│       └── ...
└── sfx/                # Optional. Sound effects (future — not yet wired).
    └── cursor.wav
```

The `theme.xml` is the only required file. Everything else is optional.

## 3. The XML format

A theme is a single `<theme>` element containing a `<variables>` block
and one or more `<view>` blocks.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<theme>
    <formatVersion>7</formatVersion>

    <variables>
        <primaryColor>D9C9A3</primaryColor>
        <backgroundColor>2B2D31</backgroundColor>
    </variables>

    <view name="system">
        <!-- elements for the system list -->
    </view>

    <view name="basic, detailed">
        <!-- elements for the game list -->
    </view>

    <view name="grid">
        <!-- elements for the game grid -->
    </view>
</theme>
```

### `<formatVersion>`

LilSmokeOS understands formatVersion 3 through 7 (the EmuELEC/ArkOS
range). Anything else is rejected with a log warning.

### `<variables>`

Variables are name/value pairs that can be referenced later via
`${varName}`. They're optional but useful for keeping colours
consistent.

### `<view>`

A view is a screen in LilSmokeUI. We support four:

| View name | Screen |
|---|---|
| `system` | The top-level system list (SystemListView). |
| `basic` | The vertical game list (GameListView in list mode). |
| `detailed` | The vertical game list with cover art (GameListView in detailed mode). |
| `grid` | The 4×3 cover-art grid (GridView — the default for games). |

A single `<view>` element can apply to multiple view names by
comma-separating them: `<view name="basic, detailed">`.

### Elements

Each view contains elements. Each element is one of:

#### `<image>`

```xml
<image name="background" extra="true">
    <color>2B2D31</color>            <!-- fill colour if no path -->
    <path>./art/background.png</path> <!-- optional PNG -->
    <pos>0 0</pos>                    <!-- top-left, in 0–1 normalised coords -->
    <size>1 1</size>                  <!-- width/height, normalised -->
    <origin>0 0</origin>              <!-- 0–1, origin within the image -->
    <maxSize>0.5 0.5</maxSize>        <!-- alternative to size -->
</image>
```

`extra="true"` means the image is decorative (not the focused item).
Without `extra="true"`, the image is treated as a content element.

#### `<text>`

```xml
<text name="logoText">
    <color>D9C9A3</color>
    <fontPath>./art/font.ttf</fontPath>  <!-- optional, falls back to DejaVu -->
    <fontSize>0.06</fontSize>           <!-- relative to screen height -->
    <pos>0.5 0.12</pos>
    <alignment>center</alignment>       <!-- left | center | right -->
    <text>LilSmokeOS</text>             <!-- literal text -->
</text>
```

#### `<textlist>`

A scrollable list of strings (used for the system list and the basic
game list view).

```xml
<textlist name="gamelist">
    <selectorColor>1F3D2B</selectorColor>   <!-- highlight bar -->
    <selectedColor>D9C9A3</selectedColor>   <!-- focused item text -->
    <primaryColor>D9C9A3</primaryColor>     <!-- unfocused item text -->
    <secondaryColor>9C9C9C</secondaryColor> <!-- secondary (subtitle) text -->
    <fontSize>0.04</fontSize>
    <pos>0.05 0.20</pos>
    <size>0.55 0.70</size>
    <alignment>left</alignment>
</textlist>
```

#### `<gridtile>`

Defines the appearance of tiles in the grid view. There are two named
tiles: `default` (unfocused) and `selected` (focused).

```xml
<gridtile name="default">
    <backgroundColor>1E2A47</backgroundColor>
    <imageColor>FFFFFF</imageColor>
    <padding>8 8</padding>              <!-- horizontal/vertical padding in px -->
    <size>0.18 0.20</size>             <!-- normalised -->
</gridtile>

<gridtile name="selected">
    <backgroundColor>1F3D2B</backgroundColor>
    <imageColor>D9C9A3</imageColor>
    <padding>6 6</padding>
    <size>0.20 0.22</size>             <!-- slightly larger when selected -->
</gridtile>
```

## 4. Coordinate system

All coordinates and sizes are normalised to the screen:

- The screen is `1.0 × 1.0`.
- `(0, 0)` is the top-left.
- `(1, 1)` is the bottom-right.
- `(0.5, 0.5)` is the centre.

The actual screen is 640×480 (4:3). A `fontSize` of `0.04` means "4%
of the screen height" = 19.2 px → rounded to 19 px → DejaVu Sans @
19 pt.

> This is the same convention as EmuELEC/ArkOS — themes designed for
> their 16:9 layouts will work, but anything positioned near the
> right edge (`pos x > 0.75`) will look cramped. For best results,
> design for 4:3.

## 5. Colours

Colours are hex strings. Three formats are accepted:

| Format | Meaning |
|---|---|
| `FFF` | `#FFFFFF` (RGB, 4-bit per channel) |
| `FFFFFF` | `#FFFFFF` (RGB, 8-bit per channel) |
| `FFFFFFFF` | `#FFFFFF` + alpha `0xFF` (RGBA, 8-bit per channel) |

A leading `#` is optional and stripped.

## 6. The default LilSmokeOS palette

The built-in default theme uses:

| Variable | Hex | Usage |
|---|---|---|
| `primaryColor` | `D9C9A3` | Light beige — primary text |
| `accentColor` | `1F3D2B` | Dark green — selector bar, focused tile |
| `backgroundColor` | `2B2D31` | Dark grey — main background |
| `secondaryBgColor` | `1E2A47` | Dark blue — secondary background, unfocused tile |

You don't have to use these. They're just what we picked.

## 7. Asset rules

### Images

- Cover art: 320×240 max (matches the grid tile size — no upscaling).
- Backgrounds: 640×480 BGRA32 (~1.2 MB uncompressed). Larger images are
  rejected by the asset loader.
- Logo: any size, but it's drawn at its native resolution and centred.
- Supported formats: PNG, JPEG, and WebP via SDL2_image.
- Total theme size should stay under 4 MB — LilSmokeUI keeps the
  active theme fully in RAM.

### Fonts

- TrueType (`.ttf`) only. OTF is not supported (FreeType can read it
  but we disable OTFe tables to save space).
- One font file per theme. If you want multiple weights, pick the one
  you want and set `<fontSize>` per element.
- If no `<fontPath>` is specified, the system DejaVu Sans is used.

### Paths

Paths in the XML are **relative to the theme folder**:

```xml
<path>./art/background.png</path>
```

LilSmokeUI converts these to absolute paths at load time. Don't use
absolute paths — they break when the user moves the theme folder.

## 8. EmuELEC / ArkOS compatibility

LilSmokeUI's theme loader was written to be a strict subset of the
EmuELEC/ArkOS format. Here's what's supported and what isn't:

| Feature | Supported? |
|---|---|
| `<variables>` | Yes |
| `<view name="...">` with comma-separated names | Yes |
| `<image>` with `color` / `path` / `pos` / `size` / `origin` / `maxSize` | Yes |
| `<text>` with `color` / `fontPath` / `fontSize` / `pos` / `alignment` | Yes |
| `<textlist>` with `selectorColor` / `selectedColor` / `primaryColor` / `secondaryColor` / `fontSize` / `pos` / `size` | Yes |
| `<gridtile>` with `backgroundColor` / `imageColor` / `padding` / `size` | Yes |
| `<sound>` | No (silently ignored) |
| `<video>` | No (silently ignored) |
| `<carousel>` | No (silently ignored — we use a vertical list) |
| `<helpsystem>` | No (we draw our own footer help) |
| `<rating>` | No |
| `<datetime>` | No |
| `<controls>` (the on-screen controller diagram) | No |
| Animations (`<storyboard>`) | No (silently ignored) |
| Shaders | No |
| Z-index (`<z>`)| No (rendered in document order) |

Unsupported elements are silently skipped — they don't break the theme.
This means you can drop an unmodified EmuELEC/ArkOS theme into
`/storage/themes/` and it'll render (minus the unsupported bits).

## 9. A complete minimal theme

```xml
<?xml version="1.0" encoding="UTF-8"?>
<theme>
    <formatVersion>7</formatVersion>

    <variables>
        <bg>1a1a2e</bg>
        <fg>eeefff</fg>
        <accent>0f3460</accent>
    </variables>

    <view name="system">
        <image name="background" extra="true">
            <color>${bg}</color>
            <pos>0 0</pos>
            <size>1 1</size>
        </image>
        <text name="logo">
            <color>${fg}</color>
            <fontSize>0.08</fontSize>
            <pos>0.5 0.1</pos>
            <alignment>center</alignment>
            <text>My Theme</text>
        </text>
        <textlist name="systems">
            <selectorColor>${accent}</selectorColor>
            <selectedColor>${fg}</selectedColor>
            <primaryColor>${fg}</primaryColor>
            <fontSize>0.05</fontSize>
            <pos>0.1 0.3</pos>
            <size>0.8 0.6</size>
            <alignment>center</alignment>
        </textlist>
    </view>

    <view name="grid">
        <image name="background" extra="true">
            <color>${bg}</color>
            <pos>0 0</pos>
            <size>1 1</size>
        </image>
        <gridtile name="default">
            <backgroundColor>${accent}</backgroundColor>
            <imageColor>${fg}</imageColor>
            <padding>8 8</padding>
            <size>0.18 0.20</size>
        </gridtile>
        <gridtile name="selected">
            <backgroundColor>${fg}</backgroundColor>
            <imageColor>${accent}</imageColor>
            <padding>6 6</padding>
            <size>0.20 0.22</size>
        </gridtile>
    </view>
</theme>
```

Save this as `/storage/themes/minimal/theme.xml`, then Settings →
Theme → minimal.

## 10. Testing your theme

The fastest loop is on VirtualBox (no MIPS emulation overhead):

```bash
./scripts/build.sh vbox-test
./scripts/run-vbox.sh
```

Mount `/storage` from the host (see [vbox-guide.md](vbox-guide.md)),
drop your theme in, reboot the VM (or restart LilSmokeUI from SSH:
`killall lilsmoke-ui`).

The UI logs theme-parse errors to `/var/log/lilsmoke-ui.log`. Tail it
over SSH:

```sh
ssh root@lilsmoke tail -f /var/log/lilsmoke-ui.log
```

## 11. Performance

LilSmokeUI caches decoded theme images in RAM. A theme with 50 system logos
at 320×240 (~50 KB compressed each) = 2.5 MB just for source assets, plus decoded textures. That's fine on
256 MB devices; on 128 MB SKUs with the Lite preset, you may want to
use fewer / smaller logos.

The asset cache has a hard cap (16 MB by default, 8 MB on Lite). When
the cap is hit, the least-recently-used textures are evicted. If your
theme causes frequent evictions, the UI will stutter when scrolling.

Tips:

- Use paletted PNGs (8-bit) where possible — they decode faster than
  32-bit RGBA. WebP is fine for storage size, but still decodes on the CPU.
- Re-use the same PNG file across multiple elements rather than having
  near-duplicate files.
- Avoid full-screen background images. A solid `<color>` fill is free
  (it's a single SDL `RenderFillRect`).
- Keep font sizes in the 0.03–0.08 range. Very large fonts (0.15+)
  generate large glyph textures.

## 12. Distributing your theme

Package your theme as a `.zip` or `.7z` with the folder name as the top level:

```
my-theme.zip
└── my-theme/
    ├── theme.xml
    └── art/
        └── ...
```

Users extract into `/storage/themes/`. There is no in-UI theme installer
yet — it's a manual file copy. LilSmokeOS includes `7za` for users who
copy a `.7z` theme package onto the device and extract it from the shell.

## 13. What's NOT supported (and won't be)

- Video backgrounds (no GPU, no video decoder — would cost 60 MB+).
- Animated GIFs (would need a full GIF decoder).
- Theme sound effects (we have an audio budget for the *game*, not the
  UI).
- Per-system theme overrides (every system uses the same theme —
  system logos are loaded from `<theme>/art/system/<system>.png`).
- Lua scripting in themes.
- Live shader parameters.

If you want any of these, you want a different OS — LilSmokeOS is for
hardware that can't do them.
