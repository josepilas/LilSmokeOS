/* Transparent diagnostic proxy. Metrics exclude load/startup; no game changes. */
#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <sys/resource.h>
#include "libretro.h"
static void *core;
static unsigned frames, rendered, duplicates;
static double start, end, run_time, video_time, audio_time, poll_time, input_time, env_time;
static unsigned polls, inputs, environments;
static unsigned pixel_format;
static uint32_t video_hash, audio_hash=2166136261u;
static int verify;
static uint32_t hash_bytes(uint32_t h,const void *data,size_t n) {
    const unsigned char *p=data;
    while(n--) h=(h^*p++)*16777619u;
    return h;
}
static retro_input_poll_t frontend_poll;
static retro_input_state_t frontend_input;
static retro_environment_t frontend_environment;
static retro_video_refresh_t frontend_video;
static retro_audio_sample_batch_t frontend_audio_batch;
static retro_audio_sample_t frontend_audio;
static double now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static void *symbol(const char *name) {
    if (!core) core = dlopen("/tmp/actual-core.so", RTLD_NOW | RTLD_LOCAL);
    if (!core) { fprintf(stderr,"CORE_PROFILE_ERROR %s\n",dlerror()); abort(); }
    void *fn=dlsym(core,name);
    if (!fn) { fprintf(stderr,"CORE_PROFILE_ERROR %s\n",name); abort(); }
    return fn;
}
#define FORWARD(type, name, args, call) \
type name args { static type (*fn) args; if (!fn) fn=symbol(#name); return fn call; }
static bool environment(unsigned cmd, void *data) {
    double t=now(); bool result=frontend_environment(cmd,data);
    if(cmd==RETRO_ENVIRONMENT_SET_PIXEL_FORMAT && result) pixel_format=*(unsigned *)data;
    env_time+=now()-t; environments++; return result;
}
static void poll(void) {
    double t=now(); frontend_poll(); poll_time+=now()-t; polls++;
}
static int16_t input(unsigned port,unsigned device,unsigned index,unsigned id) {
    double t=now(); int16_t result=frontend_input(port,device,index,id);
    input_time+=now()-t; inputs++; return result;
}
void retro_set_environment(retro_environment_t cb) {
    void (*fn)(retro_environment_t)=symbol("retro_set_environment");
    frontend_environment=cb; fn(environment);
}
void retro_set_input_poll(retro_input_poll_t cb) {
    void (*fn)(retro_input_poll_t)=symbol("retro_set_input_poll");
    frontend_poll=cb; fn(poll);
}
void retro_set_input_state(retro_input_state_t cb) {
    void (*fn)(retro_input_state_t)=symbol("retro_set_input_state");
    frontend_input=cb; fn(input);
}
FORWARD(void,retro_init,(void),())
FORWARD(void,retro_deinit,(void),())
FORWARD(unsigned,retro_api_version,(void),())
FORWARD(void,retro_get_system_info,(struct retro_system_info *info),(info))
FORWARD(void,retro_get_system_av_info,(struct retro_system_av_info *info),(info))
FORWARD(void,retro_set_controller_port_device,(unsigned port,unsigned device),(port,device))
FORWARD(void,retro_reset,(void),())
FORWARD(size_t,retro_serialize_size,(void),())
FORWARD(bool,retro_serialize,(void *data,size_t size),(data,size))
FORWARD(bool,retro_unserialize,(const void *data,size_t size),(data,size))
FORWARD(void,retro_cheat_reset,(void),())
FORWARD(void,retro_cheat_set,(unsigned index,bool enabled,const char *code),(index,enabled,code))
FORWARD(bool,retro_load_game,(const struct retro_game_info *info),(info))
FORWARD(bool,retro_load_game_special,(unsigned type,const struct retro_game_info *info,size_t n),(type,info,n))
FORWARD(unsigned,retro_get_region,(void),())
FORWARD(void *,retro_get_memory_data,(unsigned id),(id))
FORWARD(size_t,retro_get_memory_size,(unsigned id),(id))
static void video(const void *data,unsigned w,unsigned h,size_t pitch) {
    if (data) rendered++; else duplicates++;
    double t = now();
    if (verify && data) for(unsigned y=0;y<h;y++)
        video_hash=hash_bytes(video_hash,(const char *)data+y*pitch,w*(pixel_format==RETRO_PIXEL_FORMAT_XRGB8888?4:2));
    frontend_video(data,w,h,pitch);
    video_time += now()-t;
}
static size_t audio_batch(const int16_t *data,size_t n) {
    if(verify) audio_hash=hash_bytes(audio_hash,data,n*4);
    double t=now(); size_t result=frontend_audio_batch(data,n);
    audio_time+=now()-t; return result;
}
static void audio(int16_t l,int16_t r) {
    double t=now(); frontend_audio(l,r); audio_time+=now()-t;
}
void retro_set_audio_sample(retro_audio_sample_t cb) {
    void (*fn)(retro_audio_sample_t)=symbol("retro_set_audio_sample");
    frontend_audio=cb; fn(audio);
}
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
    void (*fn)(retro_audio_sample_batch_t)=symbol("retro_set_audio_sample_batch");
    frontend_audio_batch=cb; fn(audio_batch);
}
void retro_set_video_refresh(retro_video_refresh_t cb) {
    void (*fn)(retro_video_refresh_t)=symbol("retro_set_video_refresh");
    frontend_video=cb; fn(video);
}
void retro_run(void) {
    static void (*fn)(void);
    if (!fn) fn=symbol("retro_run");
    double t=now();
    if (!frames) { start=t; env_time=0; environments=0; verify=getenv("LILSMOKE_VERIFY")!=NULL; }
    fn(); end=now(); run_time+=end-t; frames++;
}
void retro_unload_game(void) {
    void (*fn)(void)=symbol("retro_unload_game");
    struct rusage usage; getrusage(RUSAGE_SELF,&usage);
    fprintf(stderr,"CORE_USAGE user_s=%.6f system_s=%.6f peak_rss_kib=%ld\n",
        usage.ru_utime.tv_sec+usage.ru_utime.tv_usec*1e-6,
        usage.ru_stime.tv_sec+usage.ru_stime.tv_usec*1e-6,usage.ru_maxrss);
    fprintf(stderr,"CORE_PROFILE frames=%u rendered=%u duplicates=%u seconds=%.6f fps=%.3f run_s=%.6f outside_s=%.6f video_s=%.6f audio_s=%.6f poll_s=%.6f input_s=%.6f env_s=%.6f engine_s=%.6f polls=%u inputs=%u environments=%u\n",
            frames,rendered,duplicates,end-start,frames/(end-start),run_time,end-start-run_time,video_time,audio_time,poll_time,input_time,env_time,run_time-video_time-audio_time-poll_time-input_time-env_time,polls,inputs,environments);
    fn();
    if(verify) fprintf(stderr,"CORE_HASH video_hash=%08x audio_hash=%08x\n",video_hash,audio_hash);
}
