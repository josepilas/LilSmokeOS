#!/bin/bash
# Validate and inventory the four MIPS VM theme capture sets.

set -Eeuo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <capture-root>" >&2
    exit 2
fi

ROOT="$(realpath "$1")"
REPORT="$ROOT/media-validation.md"
CHECKSUMS="$ROOT/SHA256SUMS"
SLUGS="art-book-next nes-box switch woody-cody"

for tool in compare identify ffprobe sha256sum find sort awk; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "missing validation tool: $tool" >&2
        exit 1
    }
done

png_count="$(find "$ROOT" -type f -name '*.png' | wc -l)"
video_count="$(find "$ROOT" -type f -name '*.webm' | wc -l)"
[ "$png_count" -eq 24 ] || {
    echo "expected 24 PNG files, found $png_count" >&2
    exit 1
}
[ "$video_count" -eq 4 ] || {
    echo "expected 4 WebM files, found $video_count" >&2
    exit 1
}

{
    echo "# LilSmokeOS MIPS VM media validation"
    echo
    echo "- PNG captures: $png_count"
    echo "- WebM recordings: $video_count"
    echo "- Required frame size: 854x480"
    echo
    echo "| Theme | Asset | Dimensions | Codec / colors | Duration |"
    echo "|---|---|---:|---:|---:|"
} > "$REPORT"

for slug in $SLUGS; do
    dir="$ROOT/$slug"
    [ -d "$dir" ] || { echo "missing theme directory: $dir" >&2; exit 1; }
    set -- "$dir"/*.png
    [ "$#" -eq 6 ] || {
        echo "$slug: expected six PNG files, found $#" >&2
        exit 1
    }

    for png in "$dir"/*.png; do
        dimensions="$(identify -format '%wx%h' "$png")"
        colors="$(identify -format '%k' "$png")"
        [ "$dimensions" = "854x480" ] || {
            echo "$png: unexpected dimensions $dimensions" >&2
            exit 1
        }
        [ "$colors" -gt 24 ] || {
            echo "$png: frame appears blank or incomplete ($colors colors)" >&2
            exit 1
        }
        echo "| $slug | $(basename "$png") | $dimensions | $colors colors | - |" >> "$REPORT"
    done

    gameplay_rmse="$(compare -metric RMSE \
        "$dir/05-game-title.png" "$dir/06-gameplay-input.png" null: \
        2>&1 >/dev/null || true)"
    gameplay_rmse="$(printf '%s\n' "$gameplay_rmse" | \
        awk -F '[()]' '{ print $2 + 0 }')"
    awk -v value="$gameplay_rmse" 'BEGIN { exit !(value > 0.10) }' || {
        echo "$slug: gameplay frame is too similar to the title ($gameplay_rmse RMSE)" >&2
        exit 1
    }

    video="$dir/$slug.webm"
    [ -s "$video" ] || { echo "missing recording: $video" >&2; exit 1; }
    video_info="$(ffprobe -v error -select_streams v:0 \
        -show_entries stream=codec_name,width,height,avg_frame_rate \
        -of csv=p=0 "$video")"
    codec="$(printf '%s' "$video_info" | awk -F, '{print $1}')"
    width="$(printf '%s' "$video_info" | awk -F, '{print $2}')"
    height="$(printf '%s' "$video_info" | awk -F, '{print $3}')"
    frame_rate="$(printf '%s' "$video_info" | awk -F, '{print $4}')"
    audio_codec="$(ffprobe -v error -select_streams a:0 \
        -show_entries stream=codec_name -of default=noprint_wrappers=1:nokey=1 \
        "$video")"
    duration="$(ffprobe -v error -show_entries format=duration \
        -of default=noprint_wrappers=1:nokey=1 "$video")"
    [ "$codec" = "vp9" ] || { echo "$video: expected VP9, found $codec" >&2; exit 1; }
    [ "$audio_codec" = "opus" ] || {
        echo "$video: expected Opus guest audio, found ${audio_codec:-none}" >&2
        exit 1
    }
    [ "$width" = "854" ] && [ "$height" = "480" ] || {
        echo "$video: unexpected frame size ${width}x${height}" >&2
        exit 1
    }
    awk -v value="$duration" 'BEGIN { exit !(value >= 35.0) }' || {
        echo "$video: recording is too short (${duration}s)" >&2
        exit 1
    }
    echo "| $slug | $(basename "$video") | ${width}x${height} | $codec+$audio_codec @ $frame_rate | ${duration}s |" >> "$REPORT"
done

(
    cd "$ROOT"
    find . -type f \( -name '*.png' -o -name '*.webm' -o -name '*.log' \) \
        -print0 | sort -z | xargs -0 sha256sum
) > "$CHECKSUMS"

printf 'THEME_MEDIA_VALIDATION_OK videos=%s images=%s report=%s\n' \
    "$video_count" "$png_count" "$REPORT"
