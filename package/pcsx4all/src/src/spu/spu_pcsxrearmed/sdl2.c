/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <SDL2/SDL.h>
#include <stdio.h>
#include "out.h"

/* 125 ms maximum. SDL's native M8 adapter delivers queued stereo PCM to the
 * isolated vendor worker; ordinary Linux continues to use SDL's real driver. */
#define QUEUE_LIMIT (44100U * 4U / 8U)
static SDL_AudioDeviceID device;

static int sdl2_init(void) {
    SDL_AudioSpec wanted = {0};
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)) return -1;
    wanted.freq = 44100;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = 2;
    wanted.samples = 512;
    device = SDL_OpenAudioDevice(NULL, 0, &wanted, NULL, 0);
    if (!device) {
        fprintf(stderr, "PCSX audio unavailable: %s\n", SDL_GetError());
        return -1;
    }
    SDL_PauseAudioDevice(device, 0);
    return 0;
}
static void sdl2_finish(void) {
    if (device) SDL_CloseAudioDevice(device);
    device = 0;
}
static int sdl2_busy(void) {
    return !device || SDL_GetQueuedAudioSize(device) >= QUEUE_LIMIT / 2;
}
static void sdl2_feed(void *data, int bytes) {
    if (!device || !data || bytes <= 0 || bytes % 4) return;
    const unsigned queued = SDL_GetQueuedAudioSize(device);
    if ((unsigned)bytes > QUEUE_LIMIT || queued > QUEUE_LIMIT - (unsigned)bytes) return;
    if (SDL_QueueAudio(device, data, (unsigned)bytes))
        fprintf(stderr, "PCSX audio queue: %s\n", SDL_GetError());
}
void pcsx_audio_pause(int pause) {
    if (!device) return;
    SDL_PauseAudioDevice(device, pause != 0);
    SDL_ClearQueuedAudio(device);
}
void out_register_sdl2(struct out_driver *driver) {
    driver->name = "sdl2-native";
    driver->init = sdl2_init;
    driver->finish = sdl2_finish;
    driver->busy = sdl2_busy;
    driver->feed = sdl2_feed;
}
