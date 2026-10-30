# LilSmokeOS — Branding

## Logo

**Source URL:** https://files.catbox.moe/pyy0s1.webp

**Local copy:** [`./lilsmoke-logo.webp`](./lilsmoke-logo.webp) (999 × 414, webp/VP8, ~10 KB)

**Dimensions:** 999 × 414 px (aspect ratio 2.41:1 — wide banner format)

### Usage guidelines

- **Frontend header:** scale to 50% of screen width, centered horizontally, ~8% from top
- **Boot splash:** full-width on 640×480 = 640 × 266 (integer scale-down of 999×414)
- **Markdown / docs:** use the catbox URL or relative link to local copy
- **Theme inclusion:** themes may override the logo by placing `logo.webp` in their `art/` folder

### Embedding in code

```cpp
// In LilSmokeUI (J3), the logo is loaded as:
const char* LOGO_PATH = "/usr/share/lilsmoke/themes/default/art/logo.webp";
```

For now (J0), the logo file lives at `assets/logo/lilsmoke-logo.webp` in the repo.
The post-build script in J3 will copy it into the rootfs at:
`/usr/share/lilsmoke/themes/default/art/logo.webp`

## Palette

The default LilSmokeOS theme uses these four colors. Themes can override any of them.

| Role | Hex | RGB | Sample |
|---|---|---|---|
| Dark grey (main background) | `#2B2D31` | 43, 45, 49 | ■ |
| Dark green (accent / borders) | `#1F3D2B` | 31, 61, 43 | ■ |
| Dark blue (secondary background) | `#1E2A47` | 30, 42, 71 | ■ |
| Light beige (primary text) | `#D9C9A3` | 217, 201, 163 | ■ |

## Typography

Default theme uses **DejaVu Sans** (shipped with the rootfs, ~600 KB).
Themes may ship their own TTF in `art/font.ttf`.

## Theme compatibility

LilSmokeOS themes are **EmuELEC + ArkOS theme-format compatible**. Users can drop a theme folder into `/storage/themes/<name>/` and select it from Settings → Appearance.

Unsupported theme elements (videos, complex animations, OpenGL shaders) are silently skipped — no errors, no crashes.

## Filesystem layout

```
/usr/share/lilsmoke/
├── themes/
│   └── default/                  ← built-in theme
│       ├── theme.xml
│       └── art/
│           ├── logo.webp
│           ├── font.ttf
│           ├── bg-system.jpg     (640×480 BGRA pre-rendered)
│           └── bg-game.jpg
│
/storage/themes/                  ← user-installed themes (writable, persistent)
├── es-theme-art-book/            (EmuELEC theme example)
├── arkos-theme-neon/
└── ...
```
