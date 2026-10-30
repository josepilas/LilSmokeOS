# Release License Review

This is a practical distribution review, not legal advice. Read the actual
included licenses. Publication remains the maintainer's decision.

## Runtime And Derived Files

The FroggyTale pin and inherited Butterscotch runner use AGPLv3. This port
retains AGPL-3.0-only for the combined runtime and all adapted runtime code.
In particular, native preparation preserves the upstream packer's format and
algorithm; audio staging modifies upstream miniaudio integration; the adapter
implements upstream runner interfaces. None is a sound candidate for an
MPL-only relicense. Existing AGPL build/test/launch tools are retained as AGPL.

The root license, notices, engine's legal display and source-access information
are retained. Distribution of the binary must be accompanied by the exact
same-release source ZIP. A pristine upstream link is not enough for the modified
runner. This distribution includes build scripts, generated staging recipes,
interface headers and vendored libraries rather than relying on a private OS repo.

## Independent MPL Tools

The new release/archive and filesystem-audit scripts are separate host programs.
They inspect or copy files through standard filesystem/ZIP/JSON interfaces,
contain no runtime implementation or upstream-derived algorithm, and are not
linked into the engine. Only those files are marked MPL-2.0. They do not apply
MPL to existing AGPL tooling or the output they package.

## SDL2 And GCC Runtime

SDL2 2.28.5 is zlib-licensed. The zlib conditions require preserving notices,
accurate origin and marking altered source; they do not impose a general
source-delivery requirement. We nevertheless include the actual SDK SDL2
source tree needed for the target build, exact public build headers, upstream
license and credits for offline completeness. The source was taken from the
qualified SDK's extracted tree; any Buildroot changes remain represented by
those bytes. SDL test assets and unrelated platform project/binary outputs
are omitted, not game data to redistribute.

libatomic files carry GPL-3.0-or-later plus GCC Runtime Library Exception 3.1.
The exception permits qualifying target-code combinations with independent
modules under their terms; it does not erase GCC's own copyleft or license
libatomic source as MPL. This engine was built by the existing GCC 12.3
toolchain, not an incompatible GCC-IR optimizer. We retain both license texts
and include libatomic plus its configure/support source closure conservatively.
No assertion that the exception excuses distributing arbitrary GCC-derived
source or the compiler itself is made. A complete compiler SDK is not bundled:
general-purpose compilers/linkers and system libraries are recipient-supplied
build prerequisites, not missing proprietary pieces of this runner.

The MIT native input header is independent of the MPL-covered OS/UI source.
Only that header is copied, with MIT text. The font has a public-domain notice,
not a newly invented MIT license. Other vendors retain notices in their files.

## Primary References

- Upstream AGPL at the pinned revision:
  https://github.com/Synaps33/FroggyTale/blob/5699418f671dc8bb07dc2e4adc60169b616ab906/LICENSE
- AGPL source/system-tool definitions and conveyance:
  https://www.gnu.org/licenses/agpl-3.0.html
- SDL licensing: https://www.libsdl.org/license.php
- GCC exception text: https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html
- libatomic's own notice:
  https://github.com/gcc-mirror/gcc/blob/releases/gcc-12.3.0/libatomic/cas_n.c
- MPL boundaries: https://www.mozilla.org/en-US/MPL/2.0/FAQ/
