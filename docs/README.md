<!-- SPDX-License-Identifier: MPL-2.0 -->
# LilSmokeOS Documentation

Current version: **Alpha-0.0.1**. Start with the release-specific guides rather
than the archived J-job roadmap or the Pre-Alpha snapshot.

## Current Release

| Task | Guide |
| --- | --- |
| Download, flash and first boot | [Alpha release guide](alpha-0.0.1/README.md) |
| See all changes since Pre-Alpha | [Alpha release notes](alpha-0.0.1/RELEASE-NOTES.md) |
| Use games, controls, media and apps | [Alpha user guide](alpha-0.0.1/USER-GUIDE.md) |
| Choose emulator/core, archives and states | [Emulation](cores.md) |
| Open adapted native engines | [OpenDingux integration](opendingux-integration.md) |
| Install/update/rename/uninstall apps | [LilSmoke Applications](lilsmoke-applications.md) |
| Copy TreeFrog ports or convert them | [TreeFrog compatibility and converter](treefrog-port-converter.md) |
| Use imported themes | [Theme compatibility](theme-compat.md) |
| Write theme XML | [Theme creation](THEME-GUIDE.md) |
| Use/remap controllers | [Input](input.md) |
| Use Explorer | [File Explorer](lilsmoke-explorer.md) |
| Understand media and device audio | [Media stack](media-stack.md) |
| Inspect source/distribution boundaries | [Alpha source inventory](alpha-0.0.1-source.md) |

The flashable image is for the stated SEGAM-M8-V6.0 stock lineage, not every
M8/R36SX/SF3000/GB350. Legacy A/B and VirtualBox notes are development material;
they do not describe the Alpha image's LILOS/LILROMS layout.

## Developers

- [Developer index](developer/README.md)
- [Source and build](developer/SOURCE-AND-BUILD.md)
- [Board/profile porting](developer/BOARD-PORTING.md)
- [Application packages and emulator apps](developer/APP-PACKAGES.md)
- [Applications SDK/API](lilsmoke-applications-sdk.md)
- [Testing and evidence](developer/TESTING.md)
- [Current frontend architecture](frontend.md)
- [Contributing](../CONTRIBUTING.md)
- [Architecture](../ARCHITECTURE.md)

Public companion projects:
[SDK/API and Studio](https://github.com/josepilas/LilSmoke-SDK),
[Port Converter source/dist](https://github.com/josepilas/LilSmoke-Port-Converter),
and [browser converter](https://josepilas.github.io/LilSmoke-Port-Converter/).

## Hardware-Dependent Services

[Remote access/LILDRIVE](remote-access.md),
[Bluetooth/mapping](bluetooth-and-mapping.md),
[storage layout](storage-layout.md), [system packages](syspck.md) and
[overclocking](overclocking.md) describe guarded integrations. Availability
requires matching kernel/drivers/hardware, not just a visible menu item.

## History And Licensing

- [Changelog](../CHANGELOG.md)
- [Historical Pre-Alpha guide](pre-alpha-0.0.1/README.md)
- [Credits](../CREDITS.md)
- [License policy](../LICENSE_POLICY.md)
- [Third-party notices](../THIRD_PARTY_NOTICES.md)

Authored OS code is MPL-2.0; dependencies retain their own terms. The original
release source ZIP/inventory is an immutable publication snapshot. Later
documentation-only corrections live on `main` and in the documentation asset.
