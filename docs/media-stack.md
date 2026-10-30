<!-- SPDX-License-Identifier: MPL-2.0 -->
# Media Stack

Current **Alpha-0.0.1** architecture. The old J1 ALSA-only diagram and its
performance budgets were not measurements of the HCSEMI device.

## Explorer Integration

LilSmoke Explorer dispatches supported music, video and image files to
`lilsmoke-media`. The player provides the relevant playback, playlist,
seeking and photo controls. Actual formats depend on installed demuxers/
decoders; accepting an extension is not a universal playback guarantee.

FFmpeg and ffprobe are included. The presence of these tools does not mean
the device can decode/transcode HD content in real time.

## Audio

On boards with ALSA, the appropriate SDL/ALSA route can be used. The release's
SEGAM-M8-V6.0 stock kernel instead exposes the vendor PCM interfaces; native
audio adaptation and isolated workers are used. Linking vendor glibc libraries
directly into a musl UI is not supported.

Changes since Pre-Alpha include delivery-capped monotonic audio pacing,
startup ordering/queue fixes and reduced music/photo redraw work. These
address stalling and corrupted/incorrectly paced output. The owner confirmed
music playback, not every codec or output device.

## Video

The player attempts the compatible native worker where available, with
explicit startup/probe/cleanup handling. A failed worker is stopped before
software fallback; temporarily unknown duration is not by itself a failure.

The software path uses bounded compressed-packet buffering (128 packets/
4 MiB), direct BGRA/ARGB8888 conversion, controlled overlay copies and timing
diagnostics. Its fallback output is capped at 640x360. These are workload
limits, not a guarantee of that frame rate/resolution on every source.

Native device allocation can fail; performance depends on the codec,
resolution, bitrate, storage and the actual driver/worker. No new GPU/3D API
is introduced. Smooth HD video and universal native acceleration are not
promised.

## UI And Process Boundaries

The frontend releases its SDL resources before a low-memory process handoff.
The board supervisor returns to LilSmokeUI afterward, where an unchanged
library cache can be reused. Media adapters and TreeFrog compatibility workers
are separate contracts, not interchangeable library search paths.

## Validation

Host tests cover queueing, timing, copies, decoder behavior and error paths.
A MIPS VM cannot establish audible HDMI/DAC output, HC device allocation or
physical video FPS. Earlier device reports and the Alpha-image acceptance
status are distinguished in the [release notes](alpha-0.0.1/RELEASE-NOTES.md).

Relevant implementation: `package/lilsmoke-media/src/`,
`board/segam-m8-v6-stock/` and the Explorer/session launch helpers.
Report the exact file's codec/resolution, backend, error and logs when playback
fails; do not upload private media unnecessarily.
