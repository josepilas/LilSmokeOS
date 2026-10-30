# Remote Access, LILDRIVE and Pointer Input

## Availability

These features are integrated into the Buildroot external tree and LilSmokeUI.
They are not a replacement for a verified console BSP. SF3000 USB is still
experimental; GB350 controller/port wiring remains unverified. No new ISO,
Windows mock frontend or flash-ready firmware image was produced.

The same OTG connector cannot simultaneously be a USB peripheral connected to
a PC and a USB host for a keyboard, controller or Wi-Fi dongle. A second host
port or appropriate board-supported arrangement is needed for that combination.
Bluetooth pairing and arbitrary unsupported Wi-Fi chipsets are not implemented.

## SSH

Enable **Settings > SSH (Dropbear)**, then open **Remote access info** for the
current interface addresses. With USB networking enabled, the default command is:

```sh
ssh lil@192.168.7.1
```

The initial password on a fresh image is `lilsmoke`. Change it immediately with
`passwd`, on a trusted local network. SSH is disabled by default on the handheld
profiles; it is not intended to be exposed to the Internet. Root SSH login and
blank-password login are not enabled.

The account is UID/GID 1000. Its home and authorized keys are in
`/persist/home/lil` and `/persist/home/lil/.ssh/authorized_keys`. Host keys live
in `/persist/ssh`, outside the read-only root filesystem. Password persistence
requires the existing persistent /etc overlay to be working. A tmpfs fallback
does not survive reboot.

The service uses the existing small Dropbear package. Its stock S50dropbear
startup is removed so that it cannot bypass the LilSmokeOS SSH setting.
S30dropbear supports start, stop and restart and does not kill unrelated servers.
It can start before DHCP completes, then accept connections when an interface
receives an address.

For file transfers to the full storage tree, use SCP mode:

```sh
scp -O example.sfc lil@192.168.7.1:/storage/roms/snes/
```

The bundled Dropbear includes SCP, not an SFTP server. Select SCP explicitly in
graphical clients. An older persistent /etc overlay containing its own passwd
file may hide the newly built `lil` account; the service reports a missing account
instead of silently changing another user's identity. Such an existing install
needs an account migration or a fresh compatible rootfs.

## USB Modes

**Settings > USB connection** provides:

| Mode | PC network | LILDRIVE owner |
|---|---|---|
| off | None | Console, when the image exists |
| network | USB Ethernet | Console |
| drive | None | PC |
| network+drive | USB Ethernet | PC |

The runtime helper is `/usr/lib/lilsmoke/lilsmoke-usb`; S25usb-gadget runs it
after storage/network initialization. The service uses Linux ConfigFS,
RNDIS by default, optional ECM, and the mass-storage function.

Configuration in `/persist/config/lilsmoke.conf`:

```ini
USB_MODE="network"
USB_NETWORK_ADDRESS="192.168.7.1"
USB_NETWORK_DRIVER="rndis"
USB_DRIVE_SIZE_MB=128
SSH_ENABLED=on
```

Use an unused private /24 network. The PC receives .2 through DHCP (.3 if
the device itself is .2). No default gateway or DNS is advertised: the cable
should not replace the PC's Internet connection. If automatic configuration
fails, configure the PC manually, for example 192.168.7.2/24, with no gateway.
Windows enumeration/RNDIS driver binding still requires physical validation.
ECM is available for hosts that support it.

The USB serial and locally administered MAC addresses derive from a persisted
per-device identity in `/persist/usb/serial`. The development VID/PID are Linux
gadget demonstration IDs, not IDs allocated to a shipping LilSmokeOS product.

## LILDRIVE Safety

LILDRIVE is the USB product name. The exported volume is now the **LILROMS
partition**, mounted at /storage locally, with exFAT or FAT32 support. The former
dedicated transfer image and size setting have been retired. OS files on LILOS
are never shared. Windows may display the filesystem label LILROMS.

1. Close games and applications using storage.
2. Choose drive or network+drive and connect a data-capable OTG cable.
3. Copy directly into ROM, BIOS, theme or port directories on LILROMS.
4. Safely eject the PC volume, then switch sharing off on the console.
5. Open LilSmoke Explorer under /storage.

The console unmounts before export and remounts only after BOTH USB unbinding
and backing-device detach succeed. Busy filesystems, nested mounts and active
PortSmoke loop images refuse export. Explorer and emulator-launch guards cover
all of /storage during sharing. A filesystem check runs before remount.
A RAM/9p development backend is not a USB block volume.

Full partition/provisioning/recovery details: [Storage layout](storage-layout.md).
Status: /usr/lib/lilsmoke/lilsmoke-usb status
Runtime status: /run/lilsmoke-usb/status

## Wi-Fi

Compatible adapters need working host-controller, driver and firmware support
in the board kernel. Network interfaces are recognized by wireless capability,
not just the name wlan0.

An administrator can provision credentials with:

```sh
/usr/lib/lilsmoke/lilsmoke-wifi connect "SSID" "WPA passphrase"
```

Credentials are stored with mode 0600; SSIDs use hexadecimal encoding and the
configuration retains only the derived WPA key, not the plaintext PSK comment.
The existing Settings Wi-Fi toggle starts/stops the configured connection.
This change does not add an SSID/password wizard to Settings.

`WIFI_IPV4=dhcp` is the default and the DHCP client renews leases. A local-only
static configuration can use `WIFI_IPV4="192.168.1.50/24"`; choose an unused
address on the PC/router's subnet. This setting does not configure a router,
access point, gateway, DNS server or USB Internet sharing.

## Keyboard, Mouse and Touchpads

The frontend reads evdev directly and discovers devices again every two seconds.
There is no external input-mapping daemon and no GPU dependency.

- Keyboard: arrows navigate, Enter activates, Escape returns, F2 opens Explorer
  actions, F3 searches, F4 marks. Text prompts accept a basic US physical-keyboard
  layout, Shift/Caps Lock, Backspace and Delete. IME/ABNT2 layout selection is not
  included in this version.
- Mouse: movement, left-click activation, wheel navigation and right-click back.
  In Explorer, right-click opens file actions instead.
- Supported controller touchpads: single-finger relative movement, reanchoring
  on finger lift and a primary click through their evdev touchpad device.
  Additional contacts cannot jump the cursor; multitouch gestures are not provided.
- Existing console GPIO/cubevol mappings stay separate from physical-keyboard
  Enter/Escape handling. The stock shared-memory controller can coexist with USB
  pointers. Gamepads use the standard button map and hat-axis navigation.

Settings exposes pointer enable, speed and cursor size. The supplied eleven
WebP sprites are preserved unchanged and decoded into small software textures.
Normal, link, text, busy, help, move, precision, unavailable and hand roles are
connected to appropriate views; the two resize sprites are registered for future
resizable surfaces, not used to imply a nonexistent resizing interaction.

The pointer texture set is about 176 KiB at 64x64 RGBA. Loading the source sheet
temporarily uses additional decoded image memory, which is released. The existing
launcher still execs the game so UI textures/input descriptors do not remain in RAM.
These frontend changes do not change input handling inside unrelated game engines.

## Kernel Integration and Evidence

Merge `board/sf3000/hc16xx-usb-input.fragment` into the pinned SF3000 kernel when
its BSP is ready. The GB350 fragment is requirements only, not verified wiring.
Check the resolved kernel config for ConfigFS, RNDIS/ECM/mass storage, loop/FAT,
evdev, USB HID and the applicable Sony/PlayStation driver. The older and newer
kernel trees do not support exactly the same HID symbols.

The real-device build gates remain intact. See
[the test report](remote-input-validation.md) and
[the 16-part progress log](remote-input-progress.md).

Primary implementation references:
[Linux ConfigFS gadgets](https://docs.kernel.org/usb/gadget_configfs.html),
[mass-storage ownership rules](https://docs.kernel.org/usb/mass-storage.html),
[evdev event semantics](https://docs.kernel.org/input/event-codes.html),
[multitouch slots](https://docs.kernel.org/input/multi-touch-protocol.html),
[PlayStation HID source](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-playstation.c).
