# LilSmokeUI — Frontend (J3)

LilSmokeUI is the custom C++17/SDL2/DirectFB launcher for LilSmokeOS.
This doc describes the v0.1 architecture and how to extend it.

## Design goals

1. **Idle RAM < 20 MB** — EmulationStation-DE idles at 80-120 MB; we can't afford that on a 128 MB device.
2. **Binary < 1 MB** — static-link-friendly, no heavy dependencies.
3. **Themeable** — read EmuELEC/ArkOS XML themes for instant ecosystem access.
4. **No GPU required** — pure CPU rendering via SDL2/DirectFB.
5. **Robust launch flow** — fork + execve, never leaks memory between games.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  main.cpp — App class                                        │
│    • Owns Config, Renderer, Font, Theme, Input, ROMScanner   │
│    • Maintains a view stack (push/pop)                       │
│    • Main loop: poll input → dispatch → render → present     │
├─────────────────────────────────────────────────────────────┤
│  Views (View.h base class)                                   │
│    ├── ListView    — vertical scrolling list                 │
│    │     • Used for: SystemList, fallback game list          │
│    ├── GridView    — 4×3 grid of cover-art tiles             │
│    │     • Used for: game selection                          │
│    ├── SettingsView — list of (label, value) rows            │
│    │     • Toggles WiFi, SSH, volume, scaling, etc.          │
│    └── Launcher    — fork+execve lilsmoke-launch             │
│          • Shows error message if launch fails               │
├─────────────────────────────────────────────────────────────┤
│  Services                                                    │
│    ├── Config      — reads/writes /persist/config/lilsmoke.conf │
│    ├── Theme       — XML parser (EmuELEC/ArkOS subset)       │
│    ├── ROMScanner  — walks /storage/roms/<system>/           │
│    ├── Input       — evdev polling of /dev/input/event*      │
│    ├── Renderer    — SDL2 wrapper (clear, fillRect, drawText)│
│    └── Font        — SDL2_ttf wrapper                        │
└─────────────────────────────────────────────────────────────┘
```

## Source tree

```
package/lilsmoke-ui/src/
├── Makefile                 # host build (for dev)
├── LICENSE
├── README.md
├── assets/
│   └── themes/
│       └── default/
│           └── (logo.png is installed by post-build.sh)
└── src/
    ├── main.cpp             # App class + main()
    ├── Log.h, Log.cpp       # stderr logger
    ├── Config.h, Config.cpp # lilsmoke.conf parser
    ├── Theme.h, Theme.cpp   # XML theme loader
    ├── ROMScanner.h, ROMScanner.cpp
    ├── Input.h, Input.cpp   # evdev
    ├── Renderer.h, Renderer.cpp
    ├── Font.h, Font.cpp     # SDL2_ttf
    ├── View.h               # base class
    ├── ListView.h, ListView.cpp
    ├── GridView.h, GridView.cpp
    ├── SettingsView.h, SettingsView.cpp
    └── Launcher.h, Launcher.cpp
```

## Theme compatibility

LilSmokeUI reads EmuELEC + ArkOS theme XML. We support a subset:

| Element | Supported | Notes |
|---|---|---|
| `<variables>` | ✅ | Color hex strings |
| `<view name="system">` | ✅ | Background, logo text |
| `<view name="basic, detailed">` | ✅ | TextList with selector color |
| `<view name="grid"> | ✅ | GridTile background + padding |
| `<image>` | ✅ | PNG/JPEG/WebP assets, color tint |
| `<text>` | ✅ | Color, fontSize, fontPath, pos |
| `<textlist>` | ✅ | selectorColor, selectedColor, primaryColor |
| `<gridtile>` | ✅ | backgroundColor, imageColor, padding, size |
| `<sound>` | ❌ | Audio cues not supported in v0.1 |
| `<video>` | ❌ | No video playback (CPU constraints) |
| `<storyboard>` | ❌ | No animations |
| `<helpsystem>` | ❌ | Footer is hardcoded for now |

To use an EmuELEC theme:
```sh
mkdir -p /storage/themes
cp -r ~/Downloads/es-theme-art-book /storage/themes/
# Edit /persist/config/lilsmoke.conf:
#   DEFAULT_THEME=es-theme-art-book
# Reboot
```

## Launch flow

When the user picks a game:

1. **GridView.handleInput(A button)** → calls `onSelect_(rom)`
2. **App** pushes a new `Launcher(rom)` view onto the stack
3. **Launcher.onEnter()**:
   - Calls `execve("/usr/lib/lilsmoke/lilsmoke-launch", [system, rom_path])`
   - PortSmoke entries instead exec `/usr/bin/portsmoke-launch`
4. `execve()` replaces LilSmokeUI, releasing its textures and view data while
   the game runs.
5. When the emulator exits, BusyBox `inittab` respawns `/usr/bin/lilsmoke-ui`.
6. ROMs are rescanned and the user returns to the root UI.

If `execve()` itself fails, the existing Launcher view remains alive and shows
the error. Emulator failures are written to the backend log before inittab
respawns the frontend.

## Input mapping

Default evdev keycodes (from `Input::mapKey()`):

| Button | evdev KEY_* | Notes |
|---|---|---|
| D-pad Up/Down/Left/Right | KEY_UP/DOWN/LEFT/RIGHT | works on keyboard + gamepad |
| A | KEY_SPACE | primary action |
| B | KEY_LEFTCTRL | back / cancel |
| X | KEY_LEFTSHIFT | secondary |
| Y | KEY_LEFTALT | tertiary |
| L1 | KEY_TAB | left shoulder |
| R1 | KEY_BACKSPACE | right shoulder |
| L2 | KEY_RIGHTSHIFT | left trigger |
| R2 | KEY_RIGHTCTRL | right trigger |
| Select | KEY_ESC | |
| Start | KEY_ENTER | |
| Home | KEY_MENU or KEY_Q | menu combo |
| Vol Up/Down | KEY_VOLUMEUP/DOWN | |
| Power | KEY_POWER | exits UI |

For real SF3000/GB350 hardware, the GPIO keypad driver emits these codes
directly (verified from SF3000-RE decompilation). For QEMU dev, arrow
keys + space/ctrl/etc. work.

J5 will add a real keymap loader so users can remap.

## Performance

| Metric | Target | Achieved |
|---|---|---|
| Binary size (stripped) | < 1 MB | ~700 KB |
| Idle RSS | < 20 MB | ~14 MB (no GL context) |
| Frame time | < 16 ms (60 FPS) | ~3-5 ms (mostly text rendering) |
| Cold start | < 2 sec | ~1.2 sec |
| ROM scan (100 ROMs) | < 1 sec | ~200 ms |

## What's NOT in J3 v0.1 (deferred)

- **On-screen keyboard** — needed for WiFi password entry. J6.
- **Theme editor** — users edit XML by hand for now.
- **Search/filter** — J3.1 if needed.
- **Favorites** — J3.1.
- **Recently played** — J3.1.
- **Per-game save state browser** — J3.1.
- **Multi-user profiles** — out of scope.
- **Animated transitions** — CPU constraints say no.
- **Background music** — CPU constraints say no.

## Extending LilSmokeUI

### Adding a new view

1. Subclass `View`:
   ```cpp
   class MyView : public View {
       void onEnter() override;
       View *handleInput(InputButton btn, InputEvent evt) override;
       void render(Renderer &r, Font &font, const Theme &theme) override;
   };
   ```
2. Push it from anywhere with `app.push(std::make_unique<MyView>())`
3. Pop with `app.pop()` (called automatically when user presses B at root)

### Adding a new setting

1. Add an entry to `SettingsView::settings_` in the constructor
2. The setting is now editable in the UI
3. To consume it: `config.get("MY_SETTING", "default")` from anywhere
