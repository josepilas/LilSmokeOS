/* SPDX-License-Identifier: MPL-2.0
 * Freestanding libretro loader for stock MIPS O32. No libc, TLS, CRT or heap. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifndef R36_BOOTSTRAP_TEST
#include "r36-bootlog.h"
#else
#define r36_boot_event(event) ((void)(event))
#endif
#ifndef R36_EXEC_SHELL
#define R36_EXEC_SHELL "/bin/sh"
#endif
#ifndef R36_EXEC_LAUNCHER
#define R36_EXEC_LAUNCHER "/mnt/sdcard/lilsmoke/r36-launch.sh"
#endif
#ifdef R36_BOOTSTRAP_TEST
extern long r36_test_syscall(long,long,long,long);
#define call3 r36_test_syscall
#else
#define call3 r36_raw
#endif
struct retro_system_info{const char *library_name,*library_version,*valid_extensions;bool need_fullpath,block_extract;};
struct retro_game_geometry{unsigned base_width,base_height,max_width,max_height;float aspect_ratio;};
struct retro_system_timing{double fps,sample_rate;};
struct retro_system_av_info{struct retro_game_geometry geometry;struct retro_system_timing timing;};
typedef bool(*environment_t)(unsigned,void *);
typedef void(*video_t)(const void *,unsigned,unsigned,size_t);
typedef void(*audio_t)(int16_t,int16_t);
typedef size_t(*batch_t)(const int16_t *,size_t);
typedef void(*poll_t)(void);
typedef int16_t(*input_t)(unsigned,unsigned,unsigned,unsigned);
void retro_set_environment(environment_t cb){(void)cb;}
void retro_set_video_refresh(video_t cb){(void)cb;}
void retro_set_audio_sample(audio_t cb){(void)cb;}
void retro_set_audio_sample_batch(batch_t cb){(void)cb;}
void retro_set_input_poll(poll_t cb){(void)cb;}
void retro_set_input_state(input_t cb){(void)cb;}
unsigned retro_api_version(void){return 1;}
void retro_get_system_info(struct retro_system_info *i){
    i->library_name="LilSmoke R36SX Bootstrap";i->library_version="0.3";i->valid_extensions="md|MD";
    i->need_fullpath=true;i->block_extract=true;
}
void retro_get_system_av_info(struct retro_system_av_info *i){
    i->geometry.base_width=640;i->geometry.base_height=480;i->geometry.max_width=640;i->geometry.max_height=480;
    i->geometry.aspect_ratio=4.0f/3.0f;i->timing.fps=60.0;i->timing.sample_rate=48000.0;
}
__attribute__((constructor)) static void r36_loaded(void){r36_boot_event("PLUGIN_DLOPEN");}
void retro_init(void){r36_boot_event("PLUGIN_INIT");}
void retro_deinit(void){}
bool retro_load_game(const void *game){
    (void)game;
    r36_boot_event("PLUGIN_LOAD_GAME");
    const char *argv[]={"sh",R36_EXEC_LAUNCHER,NULL};
    const char *env[]={"PATH=/bin:/sbin:/usr/bin:/usr/sbin","LILSMOKE_R36_BOOTSTRAP_MODE=exec",NULL};
    /* Stock framebuffer/decoder handles must not keep devices busy after the
     * address-space replacement. CLOEXEC leaves them intact if exec fails. */
    int descriptors[1024];
    for(int fd=3;fd<1024;fd++){
        descriptors[fd]=(int)call3(4055,fd,1,0); /* fcntl(F_GETFD) */
        if(descriptors[fd]>=0)(void)call3(4055,fd,2,descriptors[fd]|1);
    }
    /* execve success does not return. Failure leaves the stock frontend alive. */
    r36_boot_event("PLUGIN_EXEC_SHELL");
    (void)call3(4011,(long)R36_EXEC_SHELL,(long)argv,(long)env);
    r36_boot_event("PLUGIN_EXEC_FAILED");
    for(int fd=3;fd<1024;fd++)
        if(descriptors[fd]>=0)(void)call3(4055,fd,2,descriptors[fd]);
    return false;
}
bool retro_load_game_special(unsigned t,const void *g,size_t n){(void)t;(void)g;(void)n;return false;}
void retro_run(void){const int32_t delay[]={0,33000000};(void)call3(4166,(long)delay,0,0);}
void retro_reset(void){}
void retro_unload_game(void){}
unsigned retro_get_region(void){return 0;}
size_t retro_serialize_size(void){return 0;}
bool retro_serialize(void *p,size_t n){(void)p;(void)n;return false;}
bool retro_unserialize(const void *p,size_t n){(void)p;(void)n;return false;}
void retro_cheat_reset(void){}
void retro_cheat_set(unsigned n,bool on,const char *c){(void)n;(void)on;(void)c;}
void *retro_get_memory_data(unsigned n){(void)n;return NULL;}
size_t retro_get_memory_size(unsigned n){(void)n;return 0;}
void retro_set_controller_port_device(unsigned p,unsigned d){(void)p;(void)d;}
