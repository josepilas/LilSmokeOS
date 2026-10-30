/* SPDX-License-Identifier: MPL-2.0 */
/* QA observer only: all emulation is performed by the installed FCEUmm core. */
#include <dlfcn.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "libretro.h"

static void *engine;
static unsigned frames;

static void *symbol(const char *name)
{
    if (!engine) {
        engine = dlopen("/usr/lib/libretro/fceumm-engine.so", RTLD_NOW | RTLD_LOCAL);
        if (!engine) {
            fprintf(stderr, "QA_OBSERVER_FATAL %s\n", dlerror());
            exit(90);
        }
    }
    void *address = dlsym(engine, name);
    if (!address) {
        fprintf(stderr, "QA_OBSERVER_FATAL missing %s\n", name);
        exit(91);
    }
    return address;
}

#define CALL(type, name, args) ((type)symbol(#name)) args
#define VOID1(name, type) void name(type value) { CALL(void (*)(type), name, (value)); }

VOID1(retro_set_environment, retro_environment_t)
VOID1(retro_set_video_refresh, retro_video_refresh_t)
VOID1(retro_set_audio_sample, retro_audio_sample_t)
VOID1(retro_set_audio_sample_batch, retro_audio_sample_batch_t)
VOID1(retro_set_input_poll, retro_input_poll_t)
VOID1(retro_set_input_state, retro_input_state_t)
void retro_init(void) { CALL(void (*)(void), retro_init, ()); }
void retro_deinit(void) { CALL(void (*)(void), retro_deinit, ()); }
/* System-info strings belong to the real DSO until the frontend unloads this
 * wrapper too. Closing it inside retro_deinit invalidates those borrowed strings. */
__attribute__((destructor)) static void release_engine(void)
{
    if (engine) dlclose(engine);
    engine = NULL;
}
unsigned retro_api_version(void) { return CALL(unsigned (*)(void), retro_api_version, ()); }
void retro_get_system_info(struct retro_system_info *info)
{
    CALL(void (*)(struct retro_system_info *), retro_get_system_info, (info));
    /* A separate identity probe, not a claim to test a second emulator. */
    if (getenv("QA_OTHER_ID")) info->library_name = "FCEUmm QA alternate identity";
}
void retro_get_system_av_info(struct retro_system_av_info *info)
{ CALL(void (*)(struct retro_system_av_info *), retro_get_system_av_info, (info)); }
void retro_set_controller_port_device(unsigned port, unsigned device)
{ CALL(void (*)(unsigned, unsigned), retro_set_controller_port_device, (port, device)); }
void retro_reset(void) { CALL(void (*)(void), retro_reset, ()); }
unsigned retro_get_region(void) { return CALL(unsigned (*)(void), retro_get_region, ()); }
void *retro_get_memory_data(unsigned id)
{ return CALL(void *(*)(unsigned), retro_get_memory_data, (id)); }
size_t retro_get_memory_size(unsigned id)
{ return CALL(size_t (*)(unsigned), retro_get_memory_size, (id)); }

static uint32_t ram_hash(void)
{
    const uint8_t *data = retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    size_t size = retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
    if (!data || !size || size > 8192) {
        fprintf(stderr, "QA_OBSERVER_FATAL invalid RAM %zu\n", size);
        exit(92);
    }
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < size; ++i) hash = (hash ^ data[i]) * 16777619u;
    return hash;
}

void retro_run(void)
{
    if (!frames) fprintf(stderr, "QA_FIRST_FRAME hash=%08" PRIx32 "\n", ram_hash());
    CALL(void (*)(void), retro_run, ());
    ++frames;
}
size_t retro_serialize_size(void)
{
    if (getenv("QA_NO_STATE")) return 0;
    return CALL(size_t (*)(void), retro_serialize_size, ());
}
bool retro_serialize(void *data, size_t size)
{
    bool ok = CALL(bool (*)(void *, size_t), retro_serialize, (data, size));
    fprintf(stderr, "QA_SERIALIZE ok=%d hash=%08" PRIx32 " bytes=%zu frames=%u\n",
            ok, ram_hash(), size, frames);
    return ok;
}
bool retro_unserialize(const void *data, size_t size)
{
    bool ok = CALL(bool (*)(const void *, size_t), retro_unserialize, (data, size));
    fprintf(stderr, "QA_UNSERIALIZE ok=%d hash=%08" PRIx32 " bytes=%zu frames=%u\n",
            ok, ram_hash(), size, frames);
    return ok;
}
void retro_cheat_reset(void) { CALL(void (*)(void), retro_cheat_reset, ()); }
void retro_cheat_set(unsigned index, bool enabled, const char *code)
{ CALL(void (*)(unsigned, bool, const char *), retro_cheat_set, (index, enabled, code)); }
bool retro_load_game(const struct retro_game_info *game)
{
    frames = 0;
    bool ok = CALL(bool (*)(const struct retro_game_info *), retro_load_game, (game));
    fprintf(stderr, "QA_LOAD ok=%d\n", ok);
    return ok;
}
bool retro_load_game_special(unsigned type, const struct retro_game_info *games, size_t count)
{ return CALL(bool (*)(unsigned, const struct retro_game_info *, size_t), retro_load_game_special, (type, games, count)); }
void retro_unload_game(void)
{
    fprintf(stderr, "QA_UNLOAD frames=%u hash=%08" PRIx32 "\n", frames, ram_hash());
    CALL(void (*)(void), retro_unload_game, ());
}
