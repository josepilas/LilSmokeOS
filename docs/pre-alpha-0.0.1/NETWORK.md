<!-- SPDX-License-Identifier: MPL-2.0 -->
# Network And Device Services

**Capability-dependent, not physically certified on this M8 release.** The
settings UI and generic helpers are not evidence that a device has an OTG
peripheral controller, working Wi-Fi driver, Bluetooth radio or safe clock driver.
Leave unavailable features disabled; do not guess a board's wiring or flash
another model's kernel to enable a menu item.

## Local SSH

On a profile with the account, persistence and network services provisioned,
enable **SSH (Dropbear)** and inspect **Remote access info**. USB networking's
configured default device address is `192.168.7.1`; actual Wi-Fi addresses differ.
Connect only when that interface/address is active:

```sh
ssh lil@192.168.7.1
```

Some generic fresh profiles provision the initial password `lilsmoke`; do not
assume every adapted card contains that account. Change a provisioned default
immediately with `passwd` and verify persistence after reboot. Prefer authorized
keys. Do not enable remote login with blank passwords or expose it to the Internet.
An address printed in documentation does not establish a live network interface.

The generic Dropbear setup includes SCP, not an SFTP server. With SSH confirmed:

```sh
scp -O "My Game.sfc" lil@192.168.7.1:/storage/roms/snes/
```

Do not put account passwords, Wi-Fi keys, host private keys or complete interface
identifiers in public issues. Network changes must not overwrite working stock
credentials or another user's account.

## Wi-Fi And Bluetooth

Wi-Fi needs board USB host support plus a matching kernel driver/firmware and
userspace configuration. The generic helper can provision credentials, but the
Settings toggle is not an SSID/password wizard. Adapter compatibility cannot
be established from its retail name alone.

Bluetooth needs a working radio/driver and BlueZ integration. The devices page
does not certify every input/audio profile or USB adapter on the vendor kernel.
Bluetooth audio support/performance is not a release guarantee. Test separately
from wired native PCM audio and record radio/profile details without secrets.

## LILDRIVE USB Storage

On supported OTG/gadget profiles, **USB connection** can offer off, network,
drive or network+drive. LILDRIVE is the USB product name; only **LILROMS** may be
shared. The PC may show the filesystem label LILROMS, not LILDRIVE. LILOS and the
whole SD disk must never be exported. The same OTG port cannot simultaneously
serve as a PC peripheral and an ordinary USB host for controllers/adapters.

Close applications/transfers before enabling drive sharing. The console must
unmount its storage before the PC takes ownership. While exported, games,
Explorer edits and app installation are blocked. Safely eject on the PC before
switching sharing off; the console remounts only after export detach succeeds.
Do not force/lazily unmount a busy volume or access it from both sides at once.

For this physically tested M8, moving the card to a reader is the confirmed
file-transfer route. USB sharing/networking should remain experimental until
the target's actual peripheral controller and ownership transitions are tested.

## ZRAM, Clocks And OTA

ZRAM is optional compressed RAM swap, not extra physical memory or guaranteed
emulator speed. It needs the kernel driver and swap support. Overclocking needs
verified frequency/voltage/thermal controls; the presence of 1300/1400 MHz
choices is not proof of safe M8 PLL adjustment. Use stock clock unless a reviewed
profile explicitly establishes support. Public OTA activation/recovery is not
approved merely because an OTA page exists; use the release's documented updater.
