<!-- SPDX-License-Identifier: MPL-2.0 -->
# Music, Video And Photos

Open a recognized file in **LilSmoke Explorer** to launch the software media
player. Exit returns to the same folder/file. Media is read-only; the player
does not alter songs, videos or photos. Local playlists/directory playback
are supported, not network streaming or DRM content.

## Controls

| Control | Music/video | Photos |
| --- | --- | --- |
| A / Start | Pause/resume | Toggle slideshow |
| B | Close panel; otherwise return to Explorer | Same |
| X | Options | Options |
| L / R | Previous / next | Previous / next |
| Left / right | Seek ten seconds | Pan when zoomed |
| Up / down | Volume | Pan when zoomed |
| Y | Shuffle | Slideshow |
| Select | Cycle repeat | Fit |
| Triggers | Seek | Zoom |

Options and pointer controls include playback timeline, volume/mute, shuffle,
repeat off/all/one, metadata, playlist/info, video aspect, audio-track selection,
captions and photo rotation/zoom/fit. Pointer dragging pans a zoomed photo.
On-screen commands/tooltips identify controls when using mouse/touchpad.

## Files And Playlists

Test fixtures cover WAV, MP3, FLAC, OGG/Vorbis, Opus, AAC/M4A and WMA audio;
MP4/H.264, AVI/MPEG4, WebM/VP8 and Matroska/MJPEG video; PNG, JPEG, WebP,
BMP, TIFF and GIF images. Additional native FFmpeg formats may decode, but
an extension/container does not guarantee every embedded codec/profile works.
External-codec-only formats can be unavailable. Encrypted/DRM media is not
supported. Animated image files currently display their first frame.

Use local M3U/PLS music/video playlists. Network URLs are ignored. ROM/disc
playlists are not treated as songs simply because they end in `.m3u`. Captions
use external SRT/WebVTT files; embedded/bitmap subtitle tracks are not advertised
as supported. Embedded album art is supported when recognized by the decoder.

## Practical Limits

The M8 has no hardware video/3D acceleration. Start with modest SD/480p,
low-bitrate content. Video decode input is bounded to 1080p, output to 854x480,
one decoder thread and RGB565. Those bounds are **not a promise of realtime
1080p**. High-frame-rate, high-bitrate, HEVC and VP9 content can be impractical.
Photos are limited to 12 megapixels and downscaled to bounded display storage.

The player uses bounded queues and a 96 MiB virtual-address cap. The launcher
releases its resources during playback. Native M8 sound uses the existing PCM
adapter rather than assuming ALSA exists in its stock kernel. Host/MIPS tests
verified file opening, playback controls, nonblank frames and return to Explorer;
physical M8 media audio/performance remains to be checked.

For missing sound, check mute/volume, TV audio and a known-working game first.
For slow video, try lower resolution/bitrate or a simpler codec. For unsupported
files, keep the original and use another compatible copy/encoding. An error
screen is preferable to bypassing limits and exhausting the console's memory.
