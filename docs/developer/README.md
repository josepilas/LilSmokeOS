<!-- SPDX-License-Identifier: MPL-2.0 -->
# Developer Guides

These new guides describe the audited Pre-Alpha-0.0.1 development baseline,
not the earlier J-milestone roadmap as a series of certified hardware releases.

- [Source layout and builds](SOURCE-AND-BUILD.md)
- [Adding a board and stock profile](BOARD-PORTING.md)
- [Native application packages and emulator apps](APP-PACKAGES.md)
- [Testing and evidence](TESTING.md)
- [Contributor workflow](../../CONTRIBUTING.md)
- [User/release index](../pre-alpha-0.0.1/README.md)
- [Publication gate](../pre-alpha-0.0.1/PUBLICATION-CHECKLIST.md)

The physical reference device is an HCSEMI/CubeGM SEGAM-M8-V6.0 with its
specific V1.0.0 2024-12-05 stock lineage. MIPS ISA similarity does not make it
an Ingenic board, and a QEMU Malta VM does not model its vendor interfaces.
Separate original LilSmoke platform work from upstream engines/assets and
record each component's license before proposing distribution.
