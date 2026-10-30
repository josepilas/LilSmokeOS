# Core Metadata

Vendored from libretro/libretro-core-info, commit
`5a74858ab2f7a50cebb5a6330895bc38899531c0` (MIT; see COPYING).
These files describe the cores; their license fields describe the separate
emulator engines, not this metadata's license.

The package renames each info file to match the installed binary from
cores.lock. In particular snes9x2005 becomes snes9x_2005. RetroArch 1.19.1
uses this metadata to enable save states; a missing file disables them.

Local change: Snes9x 2005 advertises basic rather than deterministic save
states. The tested legacy APU serializer restores video but omits audio
history/sample phase, so bit-exact audio restoration is not supported.
Other cores' capabilities come from upstream and are not gameplay guarantees.
