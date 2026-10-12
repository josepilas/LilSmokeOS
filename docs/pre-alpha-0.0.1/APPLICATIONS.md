<!-- SPDX-License-Identifier: MPL-2.0 -->
# LilSmoke Applications

This theme-independent home grid replaces the former PortSmoke tab. Each app
is one folder under `/storage/independent-apps/`, containing its runtime and
one root `.sh` launcher. The title matches the app folder and metadata name;
version appears below it. Missing/invalid icons use a question mark.

## Install And Cancel

Copy a trusted `.lilpck` to LILROMS, for example `packages/`. Open it in
**LilSmoke Explorer**. The installer displays name, version and the icon on a
software-projected translucent cube. **CANCEL** returns to Explorer without
installing. **INSTALL** displays a blue progress bar, percentage and processed
decimal MB; completion is reported only after commit.

A `.lilpck` is a supported archive identified by content (ZIP, 7z, tar and
supported compressed tar variants), not a new executable file format. Its
`meta.json`, root launcher and contents must be at the archive root, **not
inside an extra wrapper folder**. Bundled ARM/Windows/glibc binaries will not
become MIPS/musl compatible merely by renaming an archive.

Packages are trusted native code with access to storage/device services, not
sandboxed app-store content. Obtain them from a source you trust. Archive
validation does not prove that an installed program is safe or licensed.

## Update, Rename, Uninstall

For the same installed version the installer says **Already installed.** For
either a newer or older version it asks **This package is a different version
from yours. Would you like to update?** INSTALL confirms replacement; CANCEL
keeps the existing application. Downgrades are deliberate, not automatic.

The updater adds/overwrites package files and removes obsolete files owned by
the old package, including a renamed root launcher. User-created files are
preserved unless the incoming package owns the same path. Keep backups: the
package is allowed to replace its declared paths. Staging needs enough space
for the new app and retained data. Do not disconnect power mid-install.

Select an app and press Y for **Rename**, **Uninstall** or Cancel. Rename changes
the folder and displayed metadata name while retaining the stable app ID.
Updates keep a user's renamed title. Uninstall requires confirmation and deletes
the app folder **including data/saves inside it**. Manual folder deletion also
removes it from the grid. Back up data before either method.

## Emulator Applications

An app may declare one or multiple `open_core` system IDs. It then appears in
**Emulator Core Settings** for those systems. Selecting a ROM passes `core=gba`
and a system-relative name such as `rom=Game Name.gba` to its root script, not
the absolute ROM path. Ordinary grid launches open the app's own menu. The
stable metadata ID allows rename without losing a configured core selection.
See the [package developer guide](../developer/APP-PACKAGES.md).

PCSX4ALL is a preinstalled app with `open_core=psx`; it is no longer an
OpenDingux home entry. Builtin seeding preserves user data/rename and does not
reinstall an intentionally removed app on each boot. Public inclusion still
depends on the release's full third-party source/license audit.

## SM64 Development Status

The **original SM64 engine** development build uses the decompilation's original
game logic and a CPU software renderer, not the earlier simplified prototype.
Owner-private packages include locally extracted assets and are **not public
release artifacts**, even if their application installer works.

The audited private 0.5.0 baseline retains 320x240 HUD/dialogs and starts world
detail at 256x192, enabling 320x240 only with sustained measured headroom. Fixed
quality costs CPU time; physical performance is scene-dependent. Later engine
work must update its own evidence before these settings are advertised anew.

The private 0.6 options-menu work adds Start+X access to quality, scaling,
volume, resume and exit. Host and MIPS checks passed, including frozen game
logic in the menu, saved settings and resume. Physical M8 acceptance remains
pending. It is not a declared finished/public SM64 release and makes no fixed
physical-FPS promise. Start alone retains the original game pause behavior;
Start+Select exits. Menu choices are stored in the app's saves folder and
override, without deleting, legacy resolution setting files.

The desired public BYO flow is an owner-supplied supported ROM in the app's
`original/` folder, followed by extraction UI/progress and playable original
content. It is **not declared complete or distributable here**. A redistributable
renderer and an asset-free runtime/loader are publication requirements. The
current graphics license forbids binary redistribution; ownership of a ROM
does not remove that separate restriction. Never upload a private SM64 package,
extracted data, executable or game capture with release docs.
