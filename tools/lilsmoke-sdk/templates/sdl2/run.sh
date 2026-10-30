#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Jose Pilas.
set -eu
test -n "$LILSMOKE_APP_DIR"
cd -- "$LILSMOKE_APP_DIR"
# Keep the OS native display/input/audio preloads. An OS profile may select
# dummy SDL audio with a native interposer or a guest test sink.
audio_override=$(printenv LILSMOKE_AUDIO_OVERRIDE || true)
if [ -n "$audio_override" ]; then export SDL_AUDIODRIVER="$audio_override"; fi
exec ./runtime/bin/application "$@"
