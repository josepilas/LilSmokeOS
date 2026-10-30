#define _POSIX_C_SOURCE 200809L
#include <libretro.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static retro_environment_t env;
static retro_video_refresh_t video;
static retro_audio_sample_batch_t audio;
static retro_input_poll_t poll;
static unsigned frames;
static uint16_t pixels[320 * 240];
void retro_set_environment(retro_environment_t cb) { env=cb; bool yes=true; cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME,&yes); }
void retro_set_video_refresh(retro_video_refresh_t cb) { video=cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio=cb; }
void retro_set_input_poll(retro_input_poll_t cb) { poll=cb; }
void retro_set_input_state(retro_input_state_t cb) { (void)cb; }
void retro_init(void) { enum retro_pixel_format fmt=RETRO_PIXEL_FORMAT_RGB565; env(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT,&fmt); }
void retro_deinit(void) { printf("QA_CORE_DONE frames=%u\n",frames); fflush(stdout); }
unsigned retro_api_version(void) { return RETRO_API_VERSION; }
void retro_get_system_info(struct retro_system_info *info) { memset(info,0,sizeof(*info)); info->library_name="Duplicate Regression"; info->library_version="1"; info->valid_extensions="test"; }
void retro_get_system_av_info(struct retro_system_av_info *info) { memset(info,0,sizeof(*info)); info->geometry.base_width=info->geometry.max_width=320; info->geometry.base_height=info->geometry.max_height=240; info->geometry.aspect_ratio=4.0f/3.0f; info->timing.fps=60; info->timing.sample_rate=32000; }
void retro_set_controller_port_device(unsigned port,unsigned device) { (void)port;(void)device; }
void retro_reset(void) { frames=0; }
bool retro_load_game(const struct retro_game_info *game) { (void)game; return true; }
bool retro_load_game_special(unsigned type,const struct retro_game_info *g,size_t n) { (void)type;(void)g;(void)n;return false; }
void retro_unload_game(void) {}
unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }
size_t retro_serialize_size(void) { return 0; }
bool retro_serialize(void *data,size_t size) { (void)data;(void)size;return false; }
bool retro_unserialize(const void *data,size_t size) { (void)data;(void)size;return false; }
void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index,bool enabled,const char *code) { (void)index;(void)enabled;(void)code; }
void *retro_get_memory_data(unsigned id) { (void)id;return NULL; }
size_t retro_get_memory_size(unsigned id) { (void)id;return 0; }
void retro_run(void) {
    static const int16_t silence[1068]={0};
    const struct timespec delay={0,10000000};
    nanosleep(&delay,NULL); // Leave time for isolated QMP menu input; not a throughput benchmark.
    if(!frames){puts("QA_FIRST_RUN");fflush(stdout);}
    const bool held=frames>=1000 || (frames==100 && access("/bench/continue-core",F_OK)!=0);
    poll(); if(!held)++frames;
    if(frames==1){puts("QA_FIRST_POLL_DONE");fflush(stdout);}
    if (frames==1 || frames%120==0) {
        for(unsigned i=0;i<320*240;++i) pixels[i]=(uint16_t)(0xf800u | (frames&31));
        video(pixels,320,240,640);
    } else video(NULL,320,240,640);
    if(audio)audio(silence,534);
    if(frames==1){puts("QA_FIRST_VIDEO_AUDIO_DONE");fflush(stdout);}
    if(!held&&frames%100==0){printf("QA_CORE_FRAME=%u\n",frames);fflush(stdout);}
}
