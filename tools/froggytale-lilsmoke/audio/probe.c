/* SPDX-License-Identifier: AGPL-3.0-only */
#include <SDL.h>
#include "ma_audio_system.h"
#include "lilsmoke_audio.h"
#include "stb_ds.h"
#include <assert.h>
#include <math.h>
#include <sys/resource.h>

void platformLog(const logType type, const char* format, va_list args) {
    (void)type;
    vfprintf(stderr, format, args);
}

typedef struct { FileSystem base; const char* directory; } ProbeFs;
static char* resolve(FileSystem* base, const char* filename) {
    ProbeFs* fs = (ProbeFs*)base;
    size_t size = strlen(fs->directory) + strlen(filename) + 2;
    char* result = safeMalloc(size);
    snprintf(result, size, "%s/%s", fs->directory, filename);
    return result;
}
static bool exists(FileSystem* fs, const char* name) {
    char* path = resolve(fs, name);
    bool result = access(path, R_OK) == 0;
    free(path);
    return result;
}
static FileSystemVtable fsVtable = {.resolvePath = resolve, .fileExists = exists};
static unsigned checks;
#define CHECK(test) do { ++checks; if (!(test)) { \
    fprintf(stderr, "AUDIO_CHECK_FAILED line=%d expression=%s\n", __LINE__, #test); exit(2); } } while (0)
static int16_t pcm[4096 * 2];
static double energy(void) {
    double sum = 0;
    for (size_t i = 0; i < sizeof(pcm) / sizeof(*pcm); ++i) sum += (double)pcm[i] * pcm[i];
    return sqrt(sum / (sizeof(pcm) / sizeof(*pcm)));
}
static double pull(AudioSystem* audio) {
    CHECK(LilSmokeAudio_read(audio, pcm, 4096));
    return energy();
}
static void le16(uint8_t* p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void le32(uint8_t* p, uint32_t v) { le16(p, v); le16(p + 2, v >> 16); }
static uint8_t* fixture(size_t* size) {
    const unsigned rate = 22050, frames = 22050;
    *size = 44 + frames * 2;
    uint8_t* wave = safeCalloc(1, *size);
    memcpy(wave, "RIFF", 4); le32(wave + 4, *size - 8);
    memcpy(wave + 8, "WAVEfmt ", 8); le32(wave + 16, 16);
    le16(wave + 20, 1); le16(wave + 22, 1); le32(wave + 24, rate);
    le32(wave + 28, rate * 2); le16(wave + 32, 2); le16(wave + 34, 16);
    memcpy(wave + 36, "data", 4); le32(wave + 40, frames * 2);
    for (unsigned i = 0; i < frames; ++i)
        le16(wave + 44 + i * 2, (uint16_t)(int16_t)(12000 * sin(6.283185307179586 * 440 * i / rate)));
    return wave;
}

static void selftest(ProbeFs* fs) {
    size_t bytes;
    uint8_t* wave = fixture(&bytes);
    AudioEntry entry = {.present = true, .data = wave, .dataSize = bytes};
    Sound sound = {.present = true, .name = "generated-tone", .flags = 0x65,
                   .volume = 1, .pitch = 1, .audioGroup = 0, .audioFile = 0};
    DataWin dw = {0};
    dw.sond = (Sond){.count = 1, .sounds = &sound};
    dw.audo = (Audo){.count = 1, .entries = &entry};
    AudioSystem* audio = (AudioSystem*)MaAudioSystem_create(&dw);
    AudioSystemVtable* v = audio->vtable;
    v->init(audio, &dw, &fs->base);
    CHECK(LilSmokeAudio_ready(audio));
    CHECK(!LilSmokeAudio_read(audio, NULL, 1));
    CHECK(!LilSmokeAudio_read(audio, pcm, 441001));
    CHECK(pull(audio) == 0);
    CHECK(v->playSound(audio, -1, 0, false) == -1);
    CHECK(v->playSound(audio, 99999, 0, false) == -1);
    CHECK(v->playSound(audio, INT32_MAX, 0, false) == -1);
    v->setSoundGain(audio, INT32_MAX, 1, 0);
    v->setSoundPitch(audio, INT32_MAX, 1);
    CHECK(v->getSoundGain(audio, INT32_MAX) == 0);
    CHECK(v->getSoundPitch(audio, INT32_MAX) == 1);
    CHECK(!v->groupIsLoaded(audio, -1));
    CHECK(!v->groupIsLoaded(audio, 30000));
    CHECK(v->groupIsLoaded(audio, 0));
    CHECK(fabs(v->getSoundLength(audio, 0) - 1.0) < .001);
    int id = v->playSound(audio, 0, 1, true);
    CHECK(id >= SOUND_INSTANCE_ID_BASE && id < AUDIO_STREAM_INDEX_BASE);
    CHECK(v->isPlaying(audio, id));
    CHECK(pull(audio) > 1000);
    CHECK(LilSmokeAudio_nonzeroFrames(audio) > 0);
    CHECK(LilSmokeAudio_nonzeroFrames(audio) <= LilSmokeAudio_frames(audio));
    float position = v->getTrackPosition(audio, id);
    CHECK(position > .08 && position < .11);
    v->pauseSound(audio, id);
    CHECK(!v->isPlaying(audio, id));
    double paused_energy=pull(audio);unsigned tail=0;
    for(unsigned i=0;i<4096*2;++i)if(pcm[i])tail=i/2+1;
    printf("AUDIO_PAUSE_TAIL rms=%.3f frames=%u\n",paused_energy,tail);
    CHECK(paused_energy == 0);
    CHECK(fabs(v->getTrackPosition(audio, id) - position) < .001);
    v->resumeSound(audio, id);
    CHECK(pull(audio) > 1000);
    v->setMasterGain(audio, 0);
    pull(audio);
    CHECK(pull(audio) == 0);
    v->setMasterGain(audio, 1);
    v->setSoundGain(audio, id, .25, 0);
    CHECK(fabs(v->getSoundGain(audio, id) - .25) < .001);
    pull(audio);
    CHECK(energy() > 100 && energy() < 3000);
    v->setSoundGain(audio, id, 1, 100);
    v->update(audio, .05);
    CHECK(fabs(v->getSoundGain(audio, id) - .625) < .001);
    v->update(audio, .05);
    CHECK(v->getSoundGain(audio, id) == 1);
    v->setGroupGain(audio, 0, 0, 0);
    pull(audio);
    CHECK(pull(audio) == 0);
    v->setGroupGain(audio, 0, 1, 100);
    v->update(audio, .1);
    CHECK(pull(audio) > 1000);
    v->setTrackPosition(audio, id, .25);
    pull(audio);
    CHECK(v->getTrackPosition(audio, id) > .33 && v->getTrackPosition(audio, id) < .37);
    v->setTrackPosition(audio, id, -5);
    v->setTrackPosition(audio, id, NAN);
    v->setSoundPitch(audio, id, 2);
    CHECK(v->getSoundPitch(audio, id) == 2);
    position = v->getTrackPosition(audio, id);
    pull(audio);
    CHECK(v->getTrackPosition(audio, id) - position > .16);
    v->setTrackPosition(audio, id, .98);
    CHECK(pull(audio) > 1000);
    CHECK(v->isPlaying(audio, id));
    CHECK(v->getTrackPosition(audio, id) < .25);
    v->suspend(audio);
    position = v->getTrackPosition(audio, id);
    CHECK(pull(audio) == 0);
    CHECK(v->getTrackPosition(audio, id) == position);
    v->resume(audio);
    CHECK(pull(audio) > 1000);
    v->pauseAll(audio); CHECK(pull(audio) == 0);
    v->resumeAll(audio); CHECK(pull(audio) > 1000);
    v->stopAll(audio);
    CHECK(!v->isPlaying(audio, id));
    id = v->playSound(audio, 0, 0, true);
    CHECK(LilSmokeAudio_read(audio, pcm, 257));
    position = v->getTrackPosition(audio, id);
    v->pauseSound(audio, id);
    CHECK(pull(audio) == 0);
    CHECK(v->getTrackPosition(audio, id) == position);
    v->resumeSound(audio, id);
    CHECK(pull(audio) > 1000);
    int other = v->playSound(audio, 0, 0, true);
    v->pauseSound(audio, id);
    position = v->getTrackPosition(audio, id);
    CHECK(pull(audio) > 1000);
    CHECK(v->getTrackPosition(audio, id) == position);
    CHECK(v->isPlaying(audio, other));
    v->stopSound(audio, other);
    CHECK(!v->isPlaying(audio, other));
    CHECK(pull(audio) == 0);
    v->resumeSound(audio, 0);
    CHECK(v->isPlaying(audio, id));
    v->setMasterGainForListener(audio, 0, 0);
    pull(audio);
    CHECK(pull(audio) == 0);
    v->setMasterGainForListener(audio, 1, 0);
    v->setMasterGainForListener(audio, 0, -1);
    v->setMasterGainForListener(audio, 0, MAX_LISTENERS);
    CHECK(pull(audio) > 1000);
    v->setSoundSpatial(audio, id, 0, 0, 0, 1, 100, 1);
    v->setListenerPosition(audio, 0, 0, 0);
    CHECK(pull(audio) > 1000);
    v->setChannelCount(audio, 32);
    v->groupLoad(audio, -1);
    v->groupLoad(audio, 30000);
    v->stopSound(audio, 0);
    CHECK(!v->isPlaying(audio, 0));
    CHECK(pull(audio) == 0);
    int ids[MAX_SOUND_INSTANCES];
    for (int i = 0; i < MAX_SOUND_INSTANCES; ++i) {
        ids[i] = v->playSound(audio, 0, i, true);
        CHECK(ids[i] >= SOUND_INSTANCE_ID_BASE);
        for (int j = 0; j < i; ++j) CHECK(ids[j] != ids[i]);
    }
    v->pauseAll(audio);
    CHECK(v->playSound(audio, 0, 1000, true) == -1);
    v->resumeAll(audio);
    CHECK(v->isPlaying(audio, ids[0]));
    v->stopAll(audio);
    MaAudioSystem* ma = (MaAudioSystem*)audio;
    ma->nextInstanceCounter = AUDIO_STREAM_INDEX_BASE - SOUND_INSTANCE_ID_BASE - 1;
    int last = v->playSound(audio, 0, 0, true);
    int wrapped = v->playSound(audio, 0, 0, true);
    CHECK(last == AUDIO_STREAM_INDEX_BASE - 1 && wrapped == SOUND_INSTANCE_ID_BASE);
    v->stopAll(audio);
    id = v->playSound(audio, 0, 0, false);
    for (int i = 0; i < 13; ++i) { pull(audio); v->update(audio, .1); }
    CHECK(!v->isPlaying(audio, id));
    CHECK(pull(audio) == 0);
    printf("AUDIO_SELFTEST checks=%u voices=%d struct_bytes=%zu frames=%llu PASS\n",
           checks, MAX_SOUND_INSTANCES, sizeof(*ma), (unsigned long long)LilSmokeAudio_frames(audio));
    v->destroy(audio);
    free(wave);
}

static void proof(AudioSystem* audio, int resource, FILE* capture, const char* kind) {
    AudioSystemVtable* v = audio->vtable;
    v->stopAll(audio);
    int id = v->playSound(audio, resource, 1, true);
    CHECK(id >= SOUND_INSTANCE_ID_BASE);
    double rms = 0;
    for (int i = 0; i < 12; ++i) {
        double next = pull(audio);
        if (next > rms) rms = next;
        if (capture) CHECK(fwrite(pcm, sizeof(pcm), 1, capture) == 1);
    }
    CHECK(rms > 1);
    CHECK(v->getTrackPosition(audio, id) > 0);
    CHECK(v->getSoundLength(audio, resource) > 0);
    printf("AUDIO_PRIVATE_PCM kind=%s frames=49152 max_block_rms=%.3f length_seconds=%.6f PASS\n",
           kind, rms, v->getSoundLength(audio, resource));
    v->pauseSound(audio, id); CHECK(pull(audio) == 0);
    v->resumeSound(audio, id); CHECK(pull(audio) > 0);
    v->setTrackPosition(audio, id, .1); CHECK(pull(audio) > 0);
    v->stopAll(audio);
}

int main(int argc, char** argv) {
    const char* wad = NULL;
    const char* ogg = NULL;
    const char* capturePath = NULL;
    bool automatic = false, failure = false;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--wad") && i + 1 < argc) wad = argv[++i];
        else if (!strcmp(argv[i], "--ogg") && i + 1 < argc) ogg = argv[++i];
        else if (!strcmp(argv[i], "--capture-private") && i + 1 < argc) capturePath = argv[++i];
        else if (!strcmp(argv[i], "--automatic")) automatic = true;
        else if (!strcmp(argv[i], "--expect-device-failure")) failure = true;
        else { fprintf(stderr, "Unknown/incomplete audio probe argument\n"); return 1; }
    }
    ProbeFs fs = {.base = {.vtable = &fsVtable}, .directory = "."};
    if (failure) {
        DataWin dw = {0};
        AudioSystem* audio = (AudioSystem*)MaAudioSystem_create(&dw);
        audio->vtable->init(audio, &dw, &fs.base);
        CHECK(!LilSmokeAudio_ready(audio));
        CHECK(audio->vtable->playSound(audio, 0, 1, false) == -1);
        audio->vtable->stopAll(audio);
        audio->vtable->update(audio, .1);
        audio->vtable->destroy(audio);
        puts("AUDIO_FAILURE_LIFECYCLE PASS");
        return 0;
    }
    setenv("FROGGY_AUDIO_MANUAL", "1", 1);
    selftest(&fs);
    if (!wad && !ogg && !automatic) return 0;
    DataWin* dw;
    DataWin empty = {0};
    if (wad) {
        DataWinParserOptions options = {0};
        options.parseGen8 = options.parseSond = options.parseAgrp = options.parseStrg = options.parseAudo = true;
        options.lazyLoadAudio = true;
        options.loadType = DATAWINLOADTYPE_MAP_FILE;
        dw = DataWin_parse(wad, options);
    } else dw = &empty;
    char* oggPath = ogg ? safeStrdup(ogg) : NULL;
    const char* basename = oggPath;
    if (oggPath) {
        char* slash = strrchr(oggPath, '/');
        if (slash) { *slash = 0; fs.directory = oggPath; basename = slash + 1; }
    }
    if (automatic) setenv("FROGGY_AUDIO_MANUAL", "0", 1);
    AudioSystem* audio = (AudioSystem*)MaAudioSystem_create(dw);
    AudioSystemVtable* v = audio->vtable;
    v->init(audio, dw, &fs.base);
    CHECK(LilSmokeAudio_ready(audio));
    FILE* capture = capturePath ? fopen(capturePath, "wbx") : NULL;
    CHECK(!capturePath || capture);
    if (automatic) {
        CHECK(ogg != NULL);
        CHECK(!LilSmokeAudio_read(audio, pcm, 4096));
        int stream = v->createStream(audio, basename);
        CHECK(stream >= AUDIO_STREAM_INDEX_BASE);
        int id = v->playSound(audio, stream, 1, true);
        CHECK(id >= SOUND_INSTANCE_ID_BASE);
        SDL_Delay(250);
        CHECK(LilSmokeAudio_frames(audio) >= 4096);
        CHECK(v->getTrackPosition(audio, id) > .05);
        v->suspend(audio);
        uint64_t frames = LilSmokeAudio_frames(audio);
        SDL_Delay(50);
        CHECK(LilSmokeAudio_frames(audio) == frames);
        v->resume(audio);
        SDL_Delay(100);
        CHECK(LilSmokeAudio_frames(audio) > frames);
        v->pauseSound(audio, id);
        float position = v->getTrackPosition(audio, id);
        SDL_Delay(50);
        CHECK(v->getTrackPosition(audio, id) == position);
        CHECK(LilSmokeAudio_frames(audio) > frames);
        v->resumeSound(audio, id);
        for (int i = 0; i < 32; ++i) {
            int transient = v->createStream(audio, basename);
            CHECK(transient >= AUDIO_STREAM_INDEX_BASE);
            int voice = v->playSound(audio, transient, 1, true);
            CHECK(voice >= SOUND_INSTANCE_ID_BASE);
            v->setSoundGain(audio, voice, .5, 10);
            v->setSoundPitch(audio, voice, 1.25);
            v->setTrackPosition(audio, voice, .1);
            v->update(audio, .01);
            v->pauseSound(audio, voice);
            v->resumeSound(audio, voice);
            SDL_Delay(2);
            CHECK(v->destroyStream(audio, transient));
            CHECK(!v->isPlaying(audio, voice));
        }
        v->suspend(audio);
        CHECK(((MaAudioSystem*)audio)->pullErrors == 0);
        v->resume(audio);
        CHECK(LilSmokeAudio_nonzeroFrames(audio) > 0);
        printf("AUDIO_SDL_CALLBACK frames=%llu nonzero_frames=%llu churn=32 errors=0 PASS\n",
               (unsigned long long)LilSmokeAudio_frames(audio),
               (unsigned long long)LilSmokeAudio_nonzeroFrames(audio));
    } else {
        if (wad) {
            int embedded = -1;
            for (uint32_t i = 0; i < dw->sond.count; ++i) {
                Sound* s = &dw->sond.sounds[i];
                if (!s->present || s->audioGroup != 0 || s->audioFile < 0 || (uint32_t)s->audioFile >= dw->audo.count) continue;
                bool inAudo = (s->flags & 0x64) != 0x64 || (s->flags & 3);
                if (!inAudo || dw->audo.entries[s->audioFile].dataSize < 1024) continue;
                int id = v->playSound(audio, i, 1, true);
                if (id >= SOUND_INSTANCE_ID_BASE && pull(audio) > 1) embedded = i;
                v->stopAll(audio);
                if (embedded >= 0) break;
            }
            CHECK(embedded >= 0);
            proof(audio, embedded, capture, "WAD_AUDO");
            int external = -1;
            if (ogg) for (uint32_t i = 0; i < dw->sond.count; ++i) {
                Sound* s = &dw->sond.sounds[i];
                if (!s->present || (s->flags & 0x64) != 0x64 || (s->flags & 3) || !s->file) continue;
                if (strcmp(s->file, basename)) continue;
                external = (int)i;
                break;
            }
            if (external >= 0) proof(audio, external, capture, "WAD_SOND_external_OGG");
        }
        if (ogg) {
            int stream = v->createStream(audio, basename);
            CHECK(stream >= AUDIO_STREAM_INDEX_BASE);
            proof(audio, stream, capture, "external_OGG");
            CHECK(v->destroyStream(audio, stream));
            CHECK(!v->destroyStream(audio, stream));
            CHECK(v->playSound(audio, stream, 1, false) == -1);
        }
    }
    if (capture) CHECK(fclose(capture) == 0);
    v->destroy(audio);
    if (wad) DataWin_free(dw);
    free(oggPath);
    struct rusage usage;
    CHECK(getrusage(RUSAGE_SELF, &usage) == 0);
    printf("AUDIO_PROBE checks=%u peak_rss_kib=%ld PASS\n", checks, usage.ru_maxrss);
    return 0;
}
