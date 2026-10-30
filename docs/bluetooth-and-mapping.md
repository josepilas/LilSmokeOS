# Bluetooth And Gamepad Mapping

## Build And Runtime

Enable `BR2_PACKAGE_LILSMOKE_BLUETOOTH` in Buildroot. This installs BlueZ,
BlueALSA, the native `lilsmoke-bt` D-Bus client and opt-in BusyBox service.
The optional kernel fragment adds BR/EDR, LE, HIDP/UHID, RFCOMM and common
USB HCI support. UART-connected radios need board-specific transport setup;
Realtek/Broadcom/MediaTek adapters may also need separately supplied firmware.
Older vendor kernels may lack some requested options. Do not claim every
Bluetooth adapter works solely because the userspace package compiles.

Runtime is off by default. Settings > Bluetooth devices offers radio power,
bounded discovery, pair, connect, disconnect, trust/untrust, forget and A2DP
output selection. PIN/passkey entry accepts keyboard digits or gamepad digit
editing. Numeric comparison requires explicit acceptance; displayed keyboard
codes must be entered on the remote keyboard. Pairing is limited to the selected
device, has a timeout, and unregisters its temporary agent.

Keys are stored under `/persist/bluetooth`, with private directory permissions,
not on the USB-exported LILROMS partition. The UI never globally trusts devices.
Use Trust explicitly on a paired controller if automatic reconnect is wanted.
Turning the radio off stops the Bluetooth/audio services and persists off.

## Audio

BlueALSA runs SBC A2DP source and HSP/HFP audio-gateway profiles. The UI audio
picker targets connected A2DP sinks (headphones/speakers). The next PSX or
RetroArch launch uses the validated device PCM; a disconnected selection falls
back to local ALSA. Active games are not rerouted mid-stream.

SCO microphone/headset support is exposed by BlueALSA, not a new recorder app.
Applications with ALSA capture support can use a BlueALSA SCO PCM. RetroArch's
microphone remains disabled in the constrained profile. Telephony, OBEX file
transfer, arbitrary BLE GATT applications and proprietary wireless receivers
are not automatically supported by this package.

Bluetooth adds encoding, latency and daemon RAM cost. Enable it only where the
actual available RAM and CPU budget permit it. No physical radio, audio latency,
RF reconnect or headset codec testing has been completed by a D-Bus fixture.

## Controllers

Settings > Gamepad mapping uses the existing raw evdev capture path. It maps
buttons, axes/hats and triggers per device, persists maps under
`/persist/config/gamepads`, and exports matching RetroArch linuxraw profiles.
Bluetooth HID controllers that the kernel exposes through evdev use this same
path; no external input mapper is required. Linuxraw or SDL2 remains selectable.

Zeebo and proprietary controllers need a supported transport/driver before
mapping can help. PS3 Sixaxis cable pairing has an upstream udev dependency and
is not falsely enabled on this BusyBox mdev build. Windows XInput is a host API;
Linux xpad/HID support handles compatible devices, not the Windows API itself.

Primary interfaces:
- https://bluez.readthedocs.io/en/latest/agent-api/
- https://bluez.readthedocs.io/en/latest/device-api/
- https://github.com/Arkq/bluez-alsa/blob/v4.1.1/README.md

## Verification

`tools/hwtest/bluetooth_dbus_smoke.py` exercises the actual helper against an
isolated BlueZ contract fixture on host and MIPS: inventories, labels, power,
pairing methods, rejection, selected-device boundaries, audio selection,
bounded scanning and cleanup. It is not a physical device test.

`bluetooth_ui_smoke.cpp` checks Settings reachability, foreground confirmation,
keyboard/gamepad code entry, cancellation, back navigation and software
rendering at 640x480 and 320x240. Controller regression tests cover real native
input code and persistence through injected Linux event streams.
