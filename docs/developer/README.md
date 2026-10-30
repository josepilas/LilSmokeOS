<!-- SPDX-License-Identifier: MPL-2.0 -->
# Developer Guides

The current release is Alpha-0.0.1. These guides describe the source/build
contracts, not the earlier J-milestone roadmap as certified hardware releases.
Read the [Alpha changes](../alpha-0.0.1/RELEASE-NOTES.md),
[source snapshot boundaries](../alpha-0.0.1-source.md) and current component
guides when a historical example differs from the running system.

- [Source layout and builds](SOURCE-AND-BUILD.md)
- [Adding a board and stock profile](BOARD-PORTING.md)
- [Native application packages and emulator apps](APP-PACKAGES.md)
- [Applications SDK/API and graphical starter](../lilsmoke-applications-sdk.md)
- [TreeFrog direct compatibility and browser converter](../treefrog-port-converter.md)
- [Testing and evidence](TESTING.md)
- [Contributor workflow](../../CONTRIBUTING.md)
- [User/release index](../alpha-0.0.1/README.md)
- [Historical Pre-Alpha publication checklist](../pre-alpha-0.0.1/PUBLICATION-CHECKLIST.md)

The physical reference device is an HCSEMI/CubeGM SEGAM-M8-V6.0 with its
specific V1.0.0 2024-12-05 stock lineage. MIPS ISA similarity does not make it
an Ingenic board, and a QEMU Malta VM does not model its vendor interfaces.
Separate original LilSmoke platform work from upstream engines/assets and
record each component's license before proposing distribution.
