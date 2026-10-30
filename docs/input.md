<!-- SPDX-License-Identifier: MPL-2.0 -->
# Input And Gamepad Mapping

Current **Alpha-0.0.1** input behavior. The earlier J5 placeholders and
swap-only remapping limitation no longer describe the active UI.

## Device Paths

LilSmokeUI reads Linux evdev devices and handles gamepad buttons, hats/axes,
keyboard and pointer events. Device discovery/state recovery accounts for
reconnects, mode changes and dropped event batches. RetroArch and adapted
native engines have their own input integrations; recognizing a controller
in the UI alone does not prove every engine mapping is correct.

RK707 D-Input, PS-Input and X-Input paths received mode-specific fixes.
RetroArch now receives both analog sticks as well as digital controls.
Those owner-confirmed results are not universal controller certification.

## Remap In Settings

1. Open **Settings -> Gamepad mapping**.
2. Highlight the logical control and press confirm.
3. Press the physical input to capture its raw binding.
4. Check the status/result and repeat for other controls; B cancels capture
   when no binding has been captured.

The current capture path can receive raw, previously unmapped input; it is not
limited to swapping two existing translated buttons. L3/R3 are included.
Saved user mappings override defaults. Per-game/engine remapping availability
is separate from frontend capture.

## Navigation And Shortcuts

| Context | Control | Result |
| --- | --- | --- |
| Game list | Y | On-screen search keyboard |
| Game list | Hold R2 | Fast downward navigation |
| Game list | X | Game Options / Favorites |
| Integrated emulator | Start+X | Menu request where implemented |
| Integrated emulator | Start+Select | Exit where implemented |
| Native TreeFrog bridge | Hold Start+Select about 500 ms on one pad | Exit |
| SDK lifecycle helper | Hold Start+Select, default 1500 ms | Cooperative exit request |

An arbitrary third-party program must consume the appropriate input/quit
events; the OS does not inject Windows keys or guarantee every application's
behavior. Do not combine half a chord from two different controllers.

## Pointer And Keyboard

Settings offers **Mouse / touchpad**, **Pointer speed** and **Cursor size**.
The native cursor is intended for compatible pointer views, not an extra
stick-driven mouse overlay inside every emulator. Motion coalescing and native
rendering/cache changes reduce work, but device polling and load still affect
smoothness.

The shared on-screen keyboard is controller-operated. A connected physical
keyboard also works in supported views and **Settings -> Shell Terminal**.

## Low-Level Keymap Files

Traditional keymaps use `<device> <code> <action>`, for example:

```text
# Linux evdev keycodes; not a universal physical-pad layout
evdev 103 up
evdev 108 down
evdev 105 left
evdev 106 right
evdev 28 start
evdev 1 select
```

System defaults are under `/etc/lilsmoke/input-maps/`; writable user mappings
are under `/persist/config/input-maps/`. Later applicable mappings override
earlier defaults. Device-specific captured mappings and emulator autoconfigs
must not be confused with these simple keyboard-code examples.

## Diagnostics And Limits

Report controller/receiver model, mode, connection type, whether connected
before boot, and whether analog input fails in UI, core or both. Preserve
input/USB and emulator logs. Generic mapping collections do not supply missing
kernel drivers or Bluetooth support. USB hub power and receiver faults can
cause failures outside the frontend's control.
