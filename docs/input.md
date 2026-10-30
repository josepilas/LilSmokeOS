# LilSmokeOS — Input System (J5)

LilSmokeOS supports remappable gamepad input via evdev + a simple
keymap file format. This doc describes the J5 input architecture.

## Architecture

```
┌──────────────────────────────────────────────────────┐
│  LilSmokeUI / RetroArch / pcsx4all-launch            │
│        calls Input::poll() or reads /dev/input/event*│
├──────────────────────────────────────────────────────┤
│  Input class (LilSmokeUI)                            │
│   • Opens all /dev/input/event* devices              │
│   • Polls in non-blocking mode                       │
│   • Translates evdev KEY_* codes → InputButton enum  │
│     via the Keymap class                             │
├──────────────────────────────────────────────────────┤
│  Keymap class                                         │
│   • Loads /etc/lilsmoke/input-maps/*.map (system)    │
│   • Loads /persist/config/input-maps/*.map (user)    │
│   • User maps override system maps for same code     │
│   • Falls back to hardcoded defaults if no files     │
├──────────────────────────────────────────────────────┤
│  Linux evdev driver (/dev/input/event*)              │
│   • SF3000/GB350: GPIO keypad driver                 │
│   • QEMU: keyboard emulation                          │
└──────────────────────────────────────────────────────┘
```

## Keymap file format

Plain text, one binding per line:

```
# Comments start with #
<device> <code> <action>

# SF3000/GB350 gamepad (Linux evdev keycodes)
evdev 103 up       # KEY_UP
evdev 108 down     # KEY_DOWN
evdev 105 left     # KEY_LEFT
evdev 106 right    # KEY_RIGHT
evdev 28  start    # KEY_ENTER
evdev 1   select   # KEY_ESC
evdev 57  a        # KEY_SPACE
evdev 42  b        # KEY_G (placeholder — real HW uses dedicated GPIO)
evdev 24  x        # KEY_O
evdev 25  y        # KEY_P
evdev 29  l1       # KEY_LEFTCTRL
evdev 97  r1       # KEY_RIGHTCTRL
evdev 115 vol_up
evdev 114 vol_down
evdev 116 power
```

### Supported devices

| Device | Status | Notes |
|---|---|---|
| `evdev` | ✅ supported | Linux input-event-codes.h KEY_* values |
| `js` | ⏳ J5.1 | Joystick button indices (for older gamepads) |

### Supported actions

`up` `down` `left` `right` `a` `b` `x` `y` `l1` `r1` `l2` `r2`
`select` `start` `home` `vol_up` `vol_down` `power` `menu` (alias for `home`)

## File locations

| Path | Purpose | Writable by user? |
|---|---|---|
| `/etc/lilsmoke/input-maps/default.map` | System default (shipped with rootfs) | No (read-only squashfs) |
| `/persist/config/input-maps/*.map` | User overrides | Yes |
| `/persist/config/input-maps/user.map` | Saved by LilSmokeUI Settings → Input | Yes |

Files are loaded in alphabetical order. Later files override earlier
ones for the same `(device, code)` pair.

## Remapping via the UI

Settings → Input — Button Mapping:

1. Select a button (Up, Down, A, B, etc.)
2. Press A to start remapping
3. UI shows "Press a key for A..."
4. Press any already-mapped button (e.g., press B to swap A↔B)
5. The new binding is applied immediately
6. On exit, the user map is saved to `/persist/config/input-maps/user.map`

### Limitations of v0.1

- **Cannot capture unmapped keys.** If you press a physical button that
  isn't currently bound to *anything*, the UI can't see it (because
  evdev code → button translation already filtered it out). This means
  you can swap existing bindings but can't add new ones from the UI.
  Workaround: edit `user.map` directly with vi.
- **No "press a key" capture modal.** J5.1 will add a raw evdev capture
  mode that intercepts keys before keymap translation.
- **No per-game bindings.** All games share the same keymap. J5.1 may
  add per-system overrides (e.g., PS1 uses R2/L2 but NES doesn't).

## Manual editing

```sh
# Edit user map directly
vi /persist/config/input-maps/user.map

# Add lines like:
evdev 57 a
evdev 42 b

# Save and reboot (or restart lilsmoke-ui)
killall lilsmoke-ui
# inittab will respawn it
```

## Default keymap (SF3000/GB350)

The GPIO keypad driver in the SF3000-RE BSP emits these codes:

| Button | evdev code | KEY_* constant |
|---|---|---|
| D-pad Up | 103 | KEY_UP |
| D-pad Down | 108 | KEY_DOWN |
| D-pad Left | 105 | KEY_LEFT |
| D-pad Right | 106 | KEY_RIGHT |
| A | 57 | KEY_SPACE |
| B | 42 | KEY_G (placeholder) |
| X | 24 | KEY_O (placeholder) |
| Y | 25 | KEY_P (placeholder) |
| L1 | 29 | KEY_LEFTCTRL |
| R1 | 97 | KEY_RIGHTCTRL |
| L2 | 56 | KEY_LEFTALT |
| R2 | 100 | KEY_RIGHTALT |
| Select | 1 | KEY_ESC |
| Start | 28 | KEY_ENTER |
| Home | 14 | KEY_BACK |
| Vol+ | 115 | KEY_VOLUMEUP |
| Vol- | 114 | KEY_VOLUMEDOWN |
| Power | 116 | KEY_POWER |

> Note: the A/B/X/Y codes for real hardware are placeholders. The
> SF3000-RE project is still reversing the GPIO keypad driver; once
> we have the real codes, we'll update `default.map` accordingly.

## QEMU development

For QEMU (no real gamepad), the default map also binds keyboard keys:

| Action | Keyboard key |
|---|---|
| D-pad | Arrow keys |
| A | Space |
| B | Left Ctrl |
| X | Left Shift |
| Y | Left Alt |
| L1 | Tab |
| R1 | Backspace |
| Select | Esc |
| Start | Enter |
| Home | Q |
| Vol+/- | (not bound) |
| Power | (not bound — close QEMU window instead) |

## What's NOT in J5 (deferred)

- **Raw key capture** — needed to bind currently-unmapped keys. J5.1.
- **Per-system keymaps** — e.g., different layouts for PS1 vs NES. J5.1.
- **Analog stick support** — `evdev` EV_ABS events. SF3000/GB350 have
  digital D-pads only, but a USB gamepad with analog sticks would need
  this. J5.2.
- **Touchscreen support** — not relevant for SF3000/GB350 (no touchscreen).
- **Hotplug** — when a USB gamepad is plugged in, we should re-scan
  `/dev/input/event*`. Currently we only scan at startup. J5.2.
- **Multi-player** — `Input` class supports up to 2 devices but the UI
  doesn't distinguish player 1 vs player 2. J5.2.
