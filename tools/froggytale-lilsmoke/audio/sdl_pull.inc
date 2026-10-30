/* SPDX-License-Identifier: AGPL-3.0-only */
/* Included by the checked, staged upstream miniaudio implementation. */
#include "lilsmoke_audio.h"
#include <sys/stat.h>

#define LS_AUDIO_RATE 44100
#define LS_AUDIO_PULL 512
#define LS_AUDIO_FILE_CAP (128U * 1024U * 1024U)
#define LS_AUDIO_ENTRY_CAP (8U * 1024U * 1024U)
#define LS_AUDIO_LAZY_CAP (32U * 1024U * 1024U)

static void lsAudioPull(MaAudioSystem* ma, int16_t* output, uint32_t count) {
    float scratch[LS_AUDIO_PULL * 2];
    memset(output, 0, (size_t)count * 2 * sizeof(*output));
    if (!ma->initialized || ma->suspended) return;
    while (count) {
        ma_uint64 read = 0;
        uint32_t take = count > LS_AUDIO_PULL ? LS_AUDIO_PULL : count;
        memset(scratch, 0, (size_t)take * 2 * sizeof(*scratch));
        ma_result result = ma_engine_read_pcm_frames(&ma->engine, scratch, take, &read);
        if (result != MA_SUCCESS) {
            ++ma->pullErrors;
            return;
        }
        ma_pcm_f32_to_s16(output, scratch, read * 2, ma_dither_mode_none);
        for (ma_uint64 i = 0; i < read; ++i)
            if (output[i * 2] || output[i * 2 + 1]) ++ma->nonzeroFrames;
        ma->framesRead += read;
        output += take * 2;
        count -= take;
    }
}

static void lsAudioCallback(void* userdata, Uint8* output, int bytes) {
    if (bytes <= 0) return;
    memset(output, 0, (size_t)bytes);
    lsAudioPull((MaAudioSystem*)userdata, (int16_t*)output, (uint32_t)bytes / 4);
}

static ma_decoder_config lsAudioDecoderConfig(void) {
    /* Normalizing at the decoder also makes upstream's 44100-based seek correct. */
    return ma_decoder_config_init(ma_format_f32, 2, LS_AUDIO_RATE);
}

static ma_result lsAudioFile(MaAudioSystem* ma, const char* path, SoundInstance* slot) {
    struct stat st;
    if (!path || stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        (uint64_t)st.st_size > LS_AUDIO_FILE_CAP) return MA_INVALID_FILE;
    ma_decoder_config config = lsAudioDecoderConfig();
    ma_result result = ma_decoder_init_file(path, &config, &slot->decoder);
    if (result != MA_SUCCESS) return result;
    result = ma_sound_init_from_data_source(&ma->engine, &slot->decoder, 0,
                                           &ma->listenerGroups[0], &slot->maSound);
    if (result != MA_SUCCESS) {
        ma_decoder_uninit(&slot->decoder);
        return result;
    }
    slot->ownsDecoder = true;
    return MA_SUCCESS;
}

static AudioEntry* lsAudioEntry(MaAudioSystem* ma, const Sound* sound) {
    if (sound->audioGroup < 0 || sound->audioGroup >= arrlen(ma->base.audioGroups)) return NULL;
    DataWin* group = ma->base.audioGroups[sound->audioGroup];
    if (!group || sound->audioFile < 0 || (uint32_t)sound->audioFile >= group->audo.count) return NULL;
    AudioEntry* entry = &group->audo.entries[sound->audioFile];
    if (!entry->present || !entry->dataSize || entry->dataSize > LS_AUDIO_ENTRY_CAP) return NULL;
    if (!entry->data && group->mappedFile) {
        if (entry->dataOffset > group->fileSize || entry->dataSize > group->fileSize - entry->dataOffset)
            return NULL;
        entry->data = group->mappedFile + entry->dataOffset;
    }
    if (!entry->data) {
        size_t resident = 0;
        for (ptrdiff_t g = 0; g < arrlen(ma->base.audioGroups); ++g) {
            DataWin* dw = ma->base.audioGroups[g];
            if (!dw || dw->mappedFile) continue;
            for (uint32_t i = 0; i < dw->audo.count; ++i) {
                if (dw->audo.entries[i].data) resident += dw->audo.entries[i].dataSize;
            }
        }
        if (resident > LS_AUDIO_LAZY_CAP || entry->dataSize > LS_AUDIO_LAZY_CAP - resident) return NULL;
        DataWin_loadAudoIfNeeded(group, (uint32_t)sound->audioFile);
    }
    return entry->data ? entry : NULL;
}

static void lsAudioInit(MaAudioSystem* ma, DataWin* dataWin, FileSystem* fs) {
    if (ma->initialized || ma->base.audioGroups) return;
    ma->base.dw = dataWin;
    arrput(ma->base.audioGroups, dataWin);
    ma->fileSystem = fs;
    ma->manualPull = getenv("FROGGY_AUDIO_MANUAL") && !strcmp(getenv("FROGGY_AUDIO_MANUAL"), "1");
    ma_engine_config config = ma_engine_config_init();
    config.noDevice = MA_TRUE;
    config.channels = 2;
    config.sampleRate = LS_AUDIO_RATE;
    /* lsAudioPull already bounds each read. Let the graph follow its exact
       length so a short callback cannot leave pre-mixed, unpausable frames. */
    config.periodSizeInFrames = 0;
    if (ma_engine_init(&config, &ma->engine) != MA_SUCCESS) {
        logError("FROGGY_AUDIO engine initialization failed\n");
        return;
    }
    for (int i = 0; i < MAX_LISTENERS; ++i) {
        /* Listener groups only apply gain. A second pitch resampler retains a
           mixed frame after a voice is paused and adds needless callback work. */
        if (ma_sound_group_init(&ma->engine, MA_SOUND_FLAG_NO_PITCH | MA_SOUND_FLAG_NO_SPATIALIZATION,
                                NULL, &ma->listenerGroups[i]) != MA_SUCCESS) {
            for (int j = 0; j < i; ++j) ma_sound_group_uninit(&ma->listenerGroups[j]);
            ma_engine_uninit(&ma->engine);
            return;
        }
        ma_sound_group_set_spatialization_enabled(&ma->listenerGroups[i], MA_FALSE);
        ma->listenerGains[i] = 1;
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) goto fail;
    ma->sdlAudioRef = true;
    SDL_AudioSpec desired = {0}, got = {0};
    desired.freq = LS_AUDIO_RATE;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 1024;
    desired.callback = lsAudioCallback;
    desired.userdata = ma;
    ma->device = SDL_OpenAudioDevice(NULL, 0, &desired, &got, 0);
    if (!ma->device || got.freq != LS_AUDIO_RATE || got.channels != 2 || got.format != AUDIO_S16SYS)
        goto fail;
    ma->initialized = true;
    logInfo("FROGGY_AUDIO sink=SDL2 rate=44100 channels=2 format=S16 voices=%d manual=%d\n",
            MAX_SOUND_INSTANCES, ma->manualPull);
    if (!ma->manualPull) SDL_PauseAudioDevice(ma->device, 0);
    return;
fail:
    logError("FROGGY_AUDIO SDL2 initialization failed: %s\n", SDL_GetError());
    if (ma->device) SDL_CloseAudioDevice(ma->device);
    ma->device = 0;
    if (ma->sdlAudioRef) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    ma->sdlAudioRef = false;
    for (int i = 0; i < MAX_LISTENERS; ++i) ma_sound_group_uninit(&ma->listenerGroups[i]);
    ma_engine_uninit(&ma->engine);
}

bool LilSmokeAudio_ready(AudioSystem* audio) {
    return audio && ((MaAudioSystem*)audio)->initialized;
}

uint64_t LilSmokeAudio_frames(AudioSystem* audio) {
    MaAudioSystem* ma = (MaAudioSystem*)audio;
    if (!LilSmokeAudio_ready(audio)) return 0;
    SDL_LockAudioDevice(ma->device);
    uint64_t frames = ma->framesRead;
    SDL_UnlockAudioDevice(ma->device);
    return frames;
}

bool LilSmokeAudio_read(AudioSystem* audio, int16_t* output, uint32_t frames) {
    MaAudioSystem* ma = (MaAudioSystem*)audio;
    if (!LilSmokeAudio_ready(audio) || !ma->manualPull || !output || frames > LS_AUDIO_RATE * 10U)
        return false;
    SDL_LockAudioDevice(ma->device);
    uint32_t errors = ma->pullErrors;
    lsAudioPull(ma, output, frames);
    bool ok = ma->pullErrors == errors;
    SDL_UnlockAudioDevice(ma->device);
    return ok;
}

uint64_t LilSmokeAudio_nonzeroFrames(AudioSystem* audio) {
    if (!LilSmokeAudio_ready(audio)) return 0;
    MaAudioSystem* ma = (MaAudioSystem*)audio;
    SDL_LockAudioDevice(ma->device);
    uint64_t frames = ma->nonzeroFrames;
    SDL_UnlockAudioDevice(ma->device);
    return frames;
}
