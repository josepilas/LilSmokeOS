#include <assert.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro.h"

static bool environment(unsigned command, void *data) {
    if (command == RETRO_ENVIRONMENT_SET_PIXEL_FORMAT || command == RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS ||
        command == RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS || command == RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL)
        return true;
    if (command == RETRO_ENVIRONMENT_GET_CAN_DUPE) { *(bool *)data = true; return true; }
    if (command == RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY || command == RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY) {
        *(const char **)data = "/bench"; return true;
    }
    if (command == RETRO_ENVIRONMENT_GET_VARIABLE) { ((struct retro_variable *)data)->value = NULL; return false; }
    return false;
}
static void video(const void *p, unsigned w, unsigned h, size_t pitch) { (void)p; (void)w; (void)h; (void)pitch; }
static void audio(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t batch(const int16_t *p, size_t n) { (void)p; return n; }
static void poll(void) {}
static int16_t input(unsigned p, unsigned d, unsigned i, unsigned id) { (void)p; (void)d; (void)i; (void)id; return 0; }

int main(int argc, char **argv) {
    assert(argc == 2);
    void *core = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!core) { fprintf(stderr, "%s\n", dlerror()); return 1; }
#define API(type, name, args) type (*name) args = (type (*) args)dlsym(core, #name); assert(name)
    API(void, retro_set_environment, (retro_environment_t));
    API(void, retro_set_video_refresh, (retro_video_refresh_t));
    API(void, retro_set_audio_sample, (retro_audio_sample_t));
    API(void, retro_set_audio_sample_batch, (retro_audio_sample_batch_t));
    API(void, retro_set_input_poll, (retro_input_poll_t));
    API(void, retro_set_input_state, (retro_input_state_t));
    API(void, retro_init, (void));
    API(bool, retro_load_game, (const struct retro_game_info *));
    API(void, retro_run, (void));
    API(size_t, retro_serialize_size, (void));
    API(bool, retro_serialize, (void *, size_t));
    API(bool, retro_unserialize, (const void *, size_t));
    API(void *, retro_get_memory_data, (unsigned));
    API(size_t, retro_get_memory_size, (unsigned));
    API(void, retro_unload_game, (void));
    API(void, retro_deinit, (void));
    retro_set_environment(environment); retro_set_video_refresh(video);
    retro_set_audio_sample(audio); retro_set_audio_sample_batch(batch);
    retro_set_input_poll(poll); retro_set_input_state(input);
    retro_init();
    API(void, retro_get_system_info, (struct retro_system_info *));
    struct retro_system_info info; retro_get_system_info(&info);
    fprintf(stderr, "CORE name=%s fullpath=%d\n", info.library_name, info.need_fullpath);
    unsigned char rom[16 + 16384 + 8192] = {'N', 'E', 'S', 0x1a, 1, 1};
    // Original public-domain QA program: count CPU instructions in zero-page.
    const unsigned char code[] = {0x78, 0xd8, 0xa2, 0xff, 0x9a, 0xa9, 1, 0x85, 0, 0xe6, 0, 0x4c, 9, 0x80};
    memcpy(rom + 16, code, sizeof code);
    for (int i = 0; i < 3; ++i) { rom[16 + 0x3ffa + i * 2] = 0; rom[16 + 0x3ffb + i * 2] = 0x80; }
    struct retro_game_info game = {"/bench/state-qa.nes", rom, sizeof rom, NULL};
    FILE *rom_file = fopen(game.path, "wb"); assert(rom_file);
    assert(fwrite(rom, 1, sizeof rom, rom_file) == sizeof rom && fclose(rom_file) == 0);
    assert(retro_load_game(&game));
    for (int i = 0; i < 30; ++i) retro_run();
    size_t size = retro_serialize_size(); assert(size > 0 && size < 8 * 1024 * 1024);
    void *state = malloc(size); assert(state && retro_serialize(state, size));
    size_t ram_size = retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
    assert(ram_size > 0 && ram_size <= 8192);
    void *ram = retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM); assert(ram);
    void *saved_ram = malloc(ram_size); assert(saved_ram); memcpy(saved_ram, ram, ram_size);
    for (int i = 0; i < 17; ++i) retro_run();
    assert(memcmp(saved_ram, ram, ram_size) != 0);
    FILE *file = fopen("/bench/fceumm.state", "wb"); assert(file);
    assert(fwrite(state, 1, size, file) == size && fclose(file) == 0);
    memset(state, 0, size);
    file = fopen("/bench/fceumm.state", "rb"); assert(file);
    assert(fread(state, 1, size, file) == size && fclose(file) == 0);
    assert(retro_unserialize(state, size));
    assert(memcmp(saved_ram, ram, ram_size) == 0);
    for (int i = 0; i < 3; ++i) retro_run();
    assert(memcmp(saved_ram, ram, ram_size) != 0);
    retro_unload_game(); retro_deinit();
    printf("FCEUMM_REAL_STATE_ROUNDTRIP_PASS bytes=%zu ram=%zu advanced=1 restored=1 resumed=1\n", size, ram_size);
    return 0;
}
