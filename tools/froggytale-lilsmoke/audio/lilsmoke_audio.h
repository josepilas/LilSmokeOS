/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef LILSMOKE_AUDIO_H
#define LILSMOKE_AUDIO_H
#include "audio_system.h"

/* Optional diagnostics. Normal game code uses the unchanged AudioSystem vtable. */
bool LilSmokeAudio_ready(AudioSystem* audio);
uint64_t LilSmokeAudio_frames(AudioSystem* audio);
uint64_t LilSmokeAudio_nonzeroFrames(AudioSystem* audio);
/* Manual pull is enabled ONLY by FROGGY_AUDIO_MANUAL=1, for offline QA. */
bool LilSmokeAudio_read(AudioSystem* audio, int16_t* stereo, uint32_t frames);
#endif
