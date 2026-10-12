<!-- SPDX-License-Identifier: MPL-2.0 -->
# Themes

LilSmokeUI implements a resource-limited subset of EmulationStation theme-set
behavior used by ArkOS and EmuELEC. It is not EmulationStation itself and does
not guarantee identical rendering of every third-party theme.

## Install A Theme

Copy an entitled-to-use theme set to LILROMS with this structure:

```text
themes/
  MyTheme/
    theme.xml
    snes/theme.xml
    megadrive/theme.xml
    assets/...
```

Avoid double nesting such as `themes/MyTheme/MyTheme/theme.xml`. Preserve
relative asset paths and case. Open **Settings > Theme** and select the set.
The settings catalog expects a root `theme.xml`. **Theme system layout** and
**Theme game list** select recognized subset choices; `theme-default` retains
the theme's default. An unsupported subset name is not a new layout renderer.

## Interpreted Behavior

Root defaults merge with a system-specific XML, and later matching element
properties override earlier ones. Includes are resolved relative to the XML
that references them. Variables, common subset selection, view inheritance,
position/size/origin/color and layering are interpreted by the theme parser.
System aliases map launcher IDs such as `md` to common `megadrive` theme folders.

The current system view includes carousel/logo placement and system information.
Game views support lists/grids, still artwork/marquees, description and common
metadata labels/rating rendering. A theme's video region uses a still image
instead of decoding preview video. Recognized conditional/subset handling is
limited to implemented parser semantics, not every fork's expression language.

Use PNG/JPEG/WebP raster resources for predictable small-device performance.
SVG handling/conversion depends on the installed asset support and available
fallbacks; a reference to an SVG is not a guarantee of every SVG feature.
Shader effects, arbitrary animations, advanced custom components and modern
ES-DE/Batocera extensions are not universally supported. The Applications grid,
installer and native emulator menus are deliberately independent of themes.

## Troubleshoot And Share

First try the default theme. If that works, check missing includes, filename
case, selected subsets, unsupported media/components and oversized resources.
Use small cover images instead of thousands of full-resolution files. A theme
that looks good in an ArkOS screenshot may rely on a different view or aspect.

When reporting a problem, provide theme name, upstream URL/version/license,
chosen subsets, system ID and a description of expected versus actual layout.
Only share images/assets you may distribute; do not upload the private reference
sets or commercial game captures with a public issue by default.

Theme authors retain their own licenses. Installing a theme does not put its
artwork, fonts or logos under LilSmokeOS's MPL policy. Creating compatible XML
does not imply endorsement by ArkOS or EmuELEC.
