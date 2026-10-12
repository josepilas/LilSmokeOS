<!-- SPDX-License-Identifier: MPL-2.0 -->
# Controllers, Keyboard And Mouse

LilSmoke uses shared native input adapters; it does not require a desktop
input-mapping program. Physical confirmation covers the Rii RK707 receiver's
D-Input, PS Input and X-Input modes and RetroArch analog input. This is not a
certification of all USB receivers, Bluetooth pads or controller touchpads.

## Controller Setup

Connect the receiver/controller before boot for the first test. If it has
several modes, choose one and leave it stable. Use **Settings > Gamepad mapping**
to map the device's actual buttons/axes, then verify both launcher and emulator.
RetroArch has its own device-type/core options as well as frontend mappings.
Analog-capable PSX games may need the appropriate emulated controller type.
A game with only digital input may not use analog sticks directly.

Core menu and exit requests are **Start+X** and **Start+Select**. A native
application must handle the adapter's menu/quit events; arbitrary third-party
software is not guaranteed to use the same bindings. Avoid assigning a desktop
mouse to an analog stick merely to compensate for missing gamepad mapping.

If a mode fails, try a known-working mode and record the model, connection,
USB VID:PID and whether the device was present at boot. Remove USB serials and
other identifiers from public logs. Do not change Windows drivers or flash the
receiver simply to diagnose a console mapping issue.

## Physical Keyboard And On-Screen Input

Arrows navigate, Enter activates and Escape returns in the frontend.
Explorer uses F2 for actions, F3 for Find and F4 to mark an item. Physical text
input is a basic US-keyboard layout with Shift/Caps Lock and editing keys;
IME and selectable international layouts are not certified in this edition.

Native UI text prompts support controller-driven entry. This is not a global
IME for arbitrary applications: an application must integrate text input or
provide its own keyboard. Follow the controls displayed by the prompt.

## Mouse And Touchpads

Enable **Mouse / touchpad**, then adjust **Pointer speed** and **Cursor size**.
The launcher supports motion, primary activation, wheel navigation and a back
action; Explorer's secondary action opens file options. Recognized evdev
touchpads support single-finger relative movement, finger-lift reanchoring and
primary click. Multitouch gestures are not implemented.

The small WebP cursor atlas has role-specific sprites. Native emulator ports
that own gamepad input suppress the unwanted desktop-style analog cursor.
An application's intentional game cursor (for example an adventure engine)
is separate. Pointer performance and compatibility depend on the actual input
device and display path; report a reproducible stutter rather than assuming
every touchpad is identical.

Bluetooth requires supported hardware/kernel/userspace and separate pairing;
see [network and device-service limits](NETWORK.md).
