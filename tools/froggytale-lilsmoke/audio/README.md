# LilSmoke Audio Sidecar

SPDX-License-Identifier: AGPL-3.0-only

This directory owns only the audio adaptation. It does not edit the port's core,
preparation, launcher, or packaging files. No game data, audio, or private paths
are source inputs. The pinned upstream runner remains AGPL-3.0-only. Its original
LICENSE must accompany source distributions. miniaudio and stb_vorbis retain the
license notices embedded in their original sources; do not strip them.

## Integration Contract

Run from the project root, after the existing upstream staging step:

```sh
python3 -B tools/froggytale-lilsmoke/audio/stage.py \
  --upstream tools/froggytale-lilsmoke/_build/mips/stage \
  --out tools/froggytale-lilsmoke/audio/_build/stage
```

Compile ONLY the resulting `ma_audio_system.c`, not the original audio source.
The output also includes `ma_audio_system.h` and `audio-stage.json`. Both source
and header are required; the generated header changes the private backend ABI.
Place `audio/_build/stage` BEFORE upstream `src/audio/miniaudio` in include search
order. Add includes for `audio`, `vendor/miniaudio`, and `vendor/stb/vorbis`, as
well as the port's existing source/stb_ds/SDL2 paths. Define `USE_MINIAUDIO` and
`FROGGY_SDL_AUDIO=1`. Link SDL2, math, pthread, dl, and shared libatomic;
include `libatomic.so.1` in the application runtime and VM dependency closure. GCC12
accepts `-std=gnu2x`; the port's existing `-std=gnu99` is also compatible.
Compile the miniaudio translation unit at O2 with `-fno-tree-vrp` to avoid the
GCC12 RANGER hang; retain the normal O2 flags on other translation units.

The factory is unchanged: `MaAudioSystem_create(DataWin*)`. Initialize, update,
destroy, and invoke the same `AudioSystem` vtable. No second vtable implementation
or extra bridge object is required. Normal non-headless upstream startup selects
it with `USE_MINIAUDIO`. Upstream deliberately selects NOOP with `--headless`;
the core owner can provide an opt-in headless-audio test switch. The optional
`LilSmokeAudio_ready`, `LilSmokeAudio_frames`, and `LilSmokeAudio_nonzeroFrames`
exports support integration QA without PCM files.

Do not set `FROGGY_AUDIO_MANUAL` in the launcher. Only offline tests set it to 1;
that keeps the SDL device paused and enables diagnostic manual pulls.

## Device And Memory

The engine uses `MA_NO_DEVICE_IO`, `MA_NO_RESOURCE_MANAGER`, `noDevice=TRUE`,
44100 Hz, and two channels. SDL2 requests 44100 Hz / stereo / S16SYS / 1024 frames
with a pull callback. Each pull mixes through a fixed 512-frame, 4096-byte float
scratch buffer and miniaudio's S16 conversion. There is no device resampling or
format negotiation, no SDL queue, no native Windows player, and no ALSA call
from this sidecar. Errors leave the vtable inert, report failure, and clean up.
The graph follows the exact pull length rather than buffering a fixed period.
Listener groups disable pitch processing: only individual sounds need pitch.
This avoids a one-frame resampler tail on pause and stale pre-mixed output after
short pulls, without muting other voices or advancing a paused cursor.

Both external SOND assets and `audio_create_stream` use `ma_decoder_init_file`
and `ma_sound_init_from_data_source`. This decodes incrementally; no whole-song
PCM cache or resource-manager thread exists. Embedded AUDO uses the original
encoded bytes and a decoder, not a decoded sample cache. Prefer an existing
mapped WAD, including lazy AUDO views. Legacy lazy encoded buffers are capped
at 32 MiB in aggregate, entries at 8 MiB, external files at 128 MiB, live voices
at 32, stream handles at 32, and loaded audio-group index at 15. These bounds
are independent of track duration. Codec working memory remains per live voice.
The game keeps ownership of data.win; destruction stops/joins the callback,
destroys sounds before listener groups, then the engine, then extra WAD groups.

All vtable operations that touch sounds or decoders are serialized against the
SDL callback with `SDL_LockAudioDevice`. Suspend/resume uses SDL pause outside
that lock. Pause retains its decoder/cursor; paused slots cannot be evicted as
ended sounds. Decoders normalize to float stereo44100 so seeking, pitch, length,
loop position and mixing agree even for mono22050 or 32000 source audio. Gain,
gain fades, group fades, listener volume, spatial settings and master volume use
the upstream implementation. Invalid stream/group IDs and instance-ID wrap are
checked. Voice exhaustion returns -1 rather than silently dropping a live voice.

## Native M8 Route

Use the OS's already installed `libod-sdl.so` as a platform runtime preload,
not source linked into the AGPL runner. Its standard SDL2 Open/Pause/Lock/Close
API accepts this pull specification and converts to native M8 S16LE PCM. The
core/launcher owner manages native environment and isolation. Generic Linux
tests explicitly use SDL's dummy driver; dummy is never selected by this code.
QEMU evidence does not establish physical speaker output, latency, or CPU load
on the handheld. No card writes or deployment are performed by this directory.

## Reproducible Checks

Run in WSL/Linux using the existing SDK, with no fetch:

```sh
python3 -B tools/froggytale-lilsmoke/audio/build.py --target mips
python3 -B tools/froggytale-lilsmoke/audio/test.py --target mips --no-build
```

Defaults: SDK `/root/lilsmoke-os/build/buildroot/output`, CPU3, nice15, serial GCC,
compiler address space <=768 MiB. Set `SDK` or `FROGGY_AUDIO_CPU` if needed.
The standalone probe links the actual upstream WAD parser, binary reader,
stb_ds, logger, and adapted audio. It does not use a fake WAD/audio parser.
`--target host` uses installed GCC/SDL2 for a second independent check.

Owner-only proof accepts `--wad /private/data.win --ogg /private/track.ogg
--private-out /private/new-directory` on `test.py`. It reads assets in place,
checks their hashes before/after, decodes original embedded AUDO and external
OGG into nonzero stereo PCM, and checks the real SDL callback with dummy output.
The capture is clearly private raw S16LE and MUST NOT enter any package or source
archive. Add `--no-capture` to verify nonzero owner PCM in memory without creating
any raw audio file; the private log still records the measured block RMS.
Only generated fixture audio is used without explicit owner inputs.
All builds/logs/test captures live below ignored `_build` or explicit private
output; never package this directory recursively including ignored files.

The synthetic vtable probe covers gain/fades, group and listener gain, spatial
calls, pitch, seek, loop/end, individual/resource/global pause and stop, mixed
voices, 257-frame partial pulls, voice exhaustion, invalid IDs, and ID wrap.
Automatic callback tests create/play/destroy 32 transient streams while the SDL
thread runs, check paused cursor stability and suspend/resume, then destroy the
backend with a live stream. Device-open failure is tested separately. Reports
start with `passed=false` so an interrupted or failed rerun cannot leave an old
successful result in place.

The main port's `test_vm.py --audio` enables the headless real-SDL callback in
the runtime case of its first/cached/runtime sequence, using the existing
128 MiB guest / 112 MiB app limit. It copies shared libatomic into the test
runtime, sets manual pull off and SDL's dummy sink, and requires both callback
frames and nonzero PCM frames. Timings come from guest uptime. Outputs are
capped at 256 MiB; originals are exported read-only and checked by SHA256.
