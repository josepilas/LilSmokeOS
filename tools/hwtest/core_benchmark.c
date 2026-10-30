/* Diagnostic libretro host: fixed emulated frames, no pacing or frameskip. */
#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>
#include "libretro.h"

static unsigned frame_no, video_frames, duplicate_frames, format;
static uint64_t audio_frames;
static uint64_t audio_verify_limit;
static uint32_t video_hash, audio_hash = 2166136261u;
static const void *last_frame;
static unsigned last_width, last_height;
static size_t last_pitch;
static int verify;
static int play_input;

static uint32_t hash_bytes(uint32_t h, const void *data, size_t n) {
    const unsigned char *p = data;
    while (n--) h = (h ^ *p++) * 16777619u;
    return h;
}
static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static bool environment(unsigned cmd, void *data) {
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        format = *(unsigned *)data;
        return format == RETRO_PIXEL_FORMAT_RGB565 || format == RETRO_PIXEL_FORMAT_XRGB8888;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool *)data = true; return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *)data = false; return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = data;
        if (strstr(v->key, "frameskip")) {
            v->value = strstr(v->key, "threshold") ? "33" : "disabled";
            return true;
        }
        v->value = NULL;
        return false;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **)data = "."; return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO: return true;
    default: return false;
    }
}
static void video(const void *data, unsigned w, unsigned h, size_t pitch) {
    if (!data) { duplicate_frames++; return; }
    video_frames++;
    last_frame = data; last_width = w; last_height = h; last_pitch = pitch;
    if (verify) {
        unsigned y;
        for (y = 0; y < h; y++)
            video_hash = hash_bytes(video_hash, (const char *)data + y * pitch,
                                   w * (format == RETRO_PIXEL_FORMAT_XRGB8888 ? 4 : 2));
    }
}
static size_t audio_batch(const int16_t *data, size_t n) {
    size_t hashed=n;
    if(audio_verify_limit && audio_frames+hashed>audio_verify_limit)
        hashed=audio_frames>=audio_verify_limit?0:(size_t)(audio_verify_limit-audio_frames);
    if (verify) audio_hash = hash_bytes(audio_hash, data, hashed * 4);
    audio_frames += n;
    return n;
}
static void audio_sample(int16_t l, int16_t r) {
    int16_t data[2] = {l, r}; audio_batch(data, 1);
}
static void poll_input(void) {}
static int16_t input(unsigned port, unsigned device, unsigned index, unsigned id) {
    (void)index;
    if (!play_input) return 0;
    if (port || device != RETRO_DEVICE_JOYPAD) return 0;
    if (play_input == 2) return id == RETRO_DEVICE_ID_JOYPAD_START && frame_no < 8;
    if (play_input == 3 && frame_no >= 3000) {
        // Crash-specific continuation: undo the last blind title Start pulse,
        // confirm the first map node, then run/jump/spin inside the level.
        if (id == RETRO_DEVICE_ID_JOYPAD_START) return frame_no >= 3100 && frame_no < 3120;
        if (id == RETRO_DEVICE_ID_JOYPAD_UP) return frame_no >= 3600;
        if (id == RETRO_DEVICE_ID_JOYPAD_Y) return frame_no >= 3700 && frame_no % 180 < 14;
        return id == RETRO_DEVICE_ID_JOYPAD_B && frame_no >= 3200 && frame_no % 120 < 18;
    }
    /* Repeatable title/menu navigation, then movement and jumps. */
    if (frame_no < 3000)
        return id == RETRO_DEVICE_ID_JOYPAD_START && frame_no % 240 < 8;
    if (id == RETRO_DEVICE_ID_JOYPAD_RIGHT) return 1;
    return id == RETRO_DEVICE_ID_JOYPAD_B && frame_no % 120 < 20;
}
static void *symbol(void *lib, const char *name) {
    void *p = dlsym(lib, name);
    if (!p) { fprintf(stderr, "Missing %s: %s\n", name, dlerror()); exit(2); }
    return p;
}
#define LOAD(name) __typeof__(&name) fn_##name = symbol(lib, #name)

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: core_benchmark core.so ROM frames [verify] [state-in] [state-out]\n");
        return 2;
    }
    unsigned frames = (unsigned)strtoul(argv[3], NULL, 10);
    if (!frames || frames > 1000000) return 2;
    verify = argc > 4 && atoi(argv[4]);
    play_input = getenv("LILSMOKE_BENCH_INPUT") != NULL;
    if (getenv("LILSMOKE_BENCH_PSX") && !strcmp(getenv("LILSMOKE_BENCH_PSX"), "1")) play_input = 3;
    void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) { fprintf(stderr, "%s\n", dlerror()); return 2; }
    LOAD(retro_set_environment); LOAD(retro_set_video_refresh);
    LOAD(retro_set_audio_sample); LOAD(retro_set_audio_sample_batch);
    LOAD(retro_set_input_poll); LOAD(retro_set_input_state);
    LOAD(retro_init); LOAD(retro_get_system_info); LOAD(retro_get_system_av_info);
    LOAD(retro_load_game); LOAD(retro_run); LOAD(retro_unload_game); LOAD(retro_deinit);
    LOAD(retro_serialize_size); LOAD(retro_serialize); LOAD(retro_unserialize);
    fn_retro_set_environment(environment);
    fn_retro_set_video_refresh(video);
    fn_retro_set_audio_sample(audio_sample);
    fn_retro_set_audio_sample_batch(audio_batch);
    fn_retro_set_input_poll(poll_input); fn_retro_set_input_state(input);
    fn_retro_init();
    struct retro_system_info info = {0};
    fn_retro_get_system_info(&info);
    FILE *f = fopen(argv[2], "rb");
    if (!f || fseek(f, 0, SEEK_END)) return 3;
    long size = ftell(f);
    if (size <= 0 || size > 64 * 1024 * 1024 || fseek(f, 0, SEEK_SET)) return 3;
    void *rom = malloc((size_t)size);
    if (!rom || fread(rom, 1, (size_t)size, f) != (size_t)size) return 3;
    fclose(f);
    struct retro_game_info game = { argv[2], info.need_fullpath ? NULL : rom,
                                   info.need_fullpath ? 0 : (size_t)size, NULL };
    if (!fn_retro_load_game(&game)) return 4;
    struct retro_system_av_info av = {0}; fn_retro_get_system_av_info(&av);
    size_t state_size = fn_retro_serialize_size();
    void *state = malloc(state_size);
    if (!state || !state_size) return 5;
    if (argc > 5 && strcmp(argv[5], "-")) {
        f = fopen(argv[5], "rb");
        if (!f || fread(state, 1, state_size, f) != state_size) return 5;
        fclose(f);
        if (!fn_retro_unserialize(state, state_size)) return 5;
    }
    printf("CORE name=%s version=%s fps=%.6f audio_rate=%.2f state_bytes=%zu verify=%d\n",
           info.library_name, info.library_version, av.timing.fps,
           av.timing.sample_rate, state_size, verify);
    fflush(stdout);
    double start = now(), block = start;
    for (frame_no = 0; frame_no < frames; frame_no++) {
        fn_retro_run();
        if ((frame_no + 1) % 600 == 0) {
            double t = now();
            printf("BLOCK frames=%u seconds=%.6f fps=%.3f\n", frame_no+1, t-block, 600/(t-block));
            fflush(stdout); block = t;
        }
    }
    double elapsed = now() - start;
    if (!fn_retro_serialize(state, state_size)) return 5;
    uint32_t state_hash = hash_bytes(2166136261u, state, state_size);
    if (argc > 6) {
        f = fopen(argv[6], "wb");
        if (!f || fwrite(state, 1, state_size, f) != state_size) return 5;
        fclose(f);
    }
    if (last_frame) {
        f = fopen("last-frame.ppm", "wb");
        if (!f) return 6;
        fprintf(f, "P6\n%u %u\n255\n", last_width, last_height);
        for (unsigned y = 0; y < last_height; y++) for (unsigned x = 0; x < last_width; x++) {
            unsigned c;
            const char *row = (const char *)last_frame + y * last_pitch;
            unsigned char rgb[3];
            if (format == RETRO_PIXEL_FORMAT_RGB565) {
                c = ((const uint16_t *)row)[x];
                rgb[0] = ((c >> 11) & 31) * 255 / 31;
                rgb[1] = ((c >> 5) & 63) * 255 / 63;
                rgb[2] = (c & 31) * 255 / 31;
            } else {
                c = ((const uint32_t *)row)[x];
                rgb[0] = c >> 16; rgb[1] = c >> 8; rgb[2] = c;
            }
            fwrite(rgb, 1, 3, f);
        }
        fclose(f);
    }
    struct rusage usage; getrusage(RUSAGE_SELF, &usage);
    printf("CORE_USAGE user_s=%.6f system_s=%.6f\n",
           usage.ru_utime.tv_sec+usage.ru_utime.tv_usec*1e-6,
           usage.ru_stime.tv_sec+usage.ru_stime.tv_usec*1e-6);
    printf("RESULT frames=%u rendered=%u duplicates=%u audio_frames=%llu seconds=%.6f fps=%.3f speed=%.4f peak_rss_kib=%ld video_hash=%08x audio_hash=%08x state_hash=%08x\n",
           frames, video_frames, duplicate_frames, (unsigned long long)audio_frames,
           elapsed, frames/elapsed, frames/elapsed/av.timing.fps, usage.ru_maxrss,
           video_hash, audio_hash, state_hash);
    if (argc > 7 && !strcmp(argv[7], "selftest")) {
        uint32_t expected_video=0, expected_audio=0;
        uint64_t expected_samples=0;
        int audio_mismatch=0;
        verify=1;
        /* Compare equal-length audio prefixes. The legacy serializer omits
         * audio history (including Echo/Loop and the fractional accumulator).
         * Report that mismatch, but still exercise restored video and input. */
        audio_verify_limit=64000;
        for (unsigned pass=0;pass<3;pass++) {
            if (!fn_retro_unserialize(state,state_size)) return 7;
            video_hash=0; audio_hash=2166136261u; audio_frames=0;
            play_input=pass==2?2:0;
            for(frame_no=0;frame_no<120;frame_no++) fn_retro_run();
            printf("RESTORE pass=%u video=%08x audio=%08x samples=%llu\n",pass,video_hash,audio_hash,(unsigned long long)audio_frames);
            if(pass==0) { expected_video=video_hash; expected_audio=audio_hash; expected_samples=audio_frames; }
            if(audio_frames<audio_verify_limit) return 7;
            if(pass==1) {
                if(video_hash!=expected_video) {
                    fprintf(stderr,"SAVE_VIDEO_ROUNDTRIP_FAILED\n"); return 7;
                }
                printf("SAVE_VIDEO_ROUNDTRIP_OK\n");
                if(audio_hash!=expected_audio || audio_frames!=expected_samples) {
                    fprintf(stderr,"SAVE_AUDIO_MISMATCH samples_expected=%llu got=%llu\n",(unsigned long long)expected_samples,(unsigned long long)audio_frames);
                    audio_mismatch=1;
                }
            }
            if(pass==2 && video_hash==expected_video) {
                fprintf(stderr,"START_INPUT_NO_VISIBLE_EFFECT\n"); return 8;
            }
        }
        printf("CORE_SAVE_INPUT_OK resumed_frames=120 start_press_frames=8\n");
        if(audio_mismatch) return 7;
    }
    fn_retro_unload_game(); fn_retro_deinit();
    free(state); free(rom); dlclose(lib);
    return 0;
}
