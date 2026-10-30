#include "od-sdl.h"
#include "input-wire.h"
#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void silence(void *userdata, unsigned char *buffer, int bytes)
{
    (void)userdata;
    memset(buffer, 0, (size_t)bytes);
}

int main(void)
{
    SDL_AudioSpec spec, got;
    assert(sizeof(struct input_event) == 16);
    /* Forced QA/native flags must never bypass the production hcfb check. */
    assert(!od_enabled());
    assert(SDL_Init(SDL_INIT_AUDIO) == 0);
    assert(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO);
    memset(&spec, 0, sizeof(spec));
    spec.freq = 44100;
    spec.format = AUDIO_S16SYS;
    spec.channels = 2;
    spec.samples = 256;
    spec.callback = silence;
    assert(SDL_OpenAudio(&spec, &got) == 0);
    SDL_PauseAudio(0);
    SDL_Delay(20);
    SDL_PauseAudio(1);
    SDL_CloseAudio();
    SDL_Quit();
    puts("PASS production SDL generic audio passthrough, time64evdev ABI, no QA gate bypass");
    return 0;
}
