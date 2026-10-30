/* SPDX-License-Identifier: MPL-2.0
 * Stock libretro ABI. Link ONLY to the supplied glibc, never to musl. */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

extern int fork(void), getpid(void), ftok(const char *, int);
extern int execl(const char *, const char *, ...);
extern int setenv(const char *, const char *, int);
extern int unsetenv(const char *);
extern int usleep(unsigned int);
extern void _exit(int);
extern long write(int, const void *, unsigned long);

#ifndef R36_LAUNCHER
#define R36_LAUNCHER "/mnt/sdcard/lilsmoke/r36-launch.sh"
#endif
static int launched;
static void decimal(int value, char out[16]) {
    char reverse[16]; unsigned n=0, p=0;
    uint32_t v=value<0?0U-(uint32_t)value:(uint32_t)value;
    if(value<0)out[p++]='-';
    do {reverse[n++]=(char)('0'+v%10);v/=10;} while(v);
    while(n)out[p++]=reverse[--n];
    out[p]=0;
}
static bool start(void) {
    if(launched)return true;
    char owner[16], key[16];
    decimal(getpid(),owner);decimal(ftok("/tmp/joy_key",'a'),key);
    int child=fork();
    if(child<0)return false;
    if(!child) {
        unsetenv("LD_PRELOAD");
        setenv("LILSMOKE_R36_STOCK_PID",owner,1);
        setenv("LILSMOKE_R36_IPC_KEY",key,1);
        execl("/bin/sh","sh",R36_LAUNCHER,(char *)0);
        static const char failure[]="R36 bootstrap: launcher exec failed\n";
        write(2,failure,sizeof(failure)-1);_exit(127);
    }
    launched=1;return true;
}
struct retro_system_info {const char *library_name,*library_version,*valid_extensions;bool need_fullpath,block_extract;};
struct retro_game_geometry {unsigned base_width,base_height,max_width,max_height;float aspect_ratio;};
struct retro_system_timing {double fps,sample_rate;};
struct retro_system_av_info {struct retro_game_geometry geometry;struct retro_system_timing timing;};
typedef bool (*environment_t)(unsigned,void *);
typedef void (*video_t)(const void *,unsigned,unsigned,size_t);
typedef void (*audio_t)(int16_t,int16_t);
typedef size_t (*batch_t)(const int16_t *,size_t);
typedef void (*poll_t)(void);
typedef int16_t (*input_t)(unsigned,unsigned,unsigned,unsigned);
void retro_set_environment(environment_t cb){(void)cb;}
void retro_set_video_refresh(video_t cb){(void)cb;}
void retro_set_audio_sample(audio_t cb){(void)cb;}
void retro_set_audio_sample_batch(batch_t cb){(void)cb;}
void retro_set_input_poll(poll_t cb){(void)cb;}
void retro_set_input_state(input_t cb){(void)cb;}
unsigned retro_api_version(void){return 1;}
void retro_get_system_info(struct retro_system_info *i){
    *i=(struct retro_system_info){"LilSmoke R36SX Bootstrap","0.1","md|MD",true,true};
}
void retro_get_system_av_info(struct retro_system_av_info *i){
    *i=(struct retro_system_av_info){{640,480,640,480,4.0f/3.0f},{60.0,48000.0}};
}
void retro_init(void){}
void retro_deinit(void){}
bool retro_load_game(const void *game){(void)game;return start();}
bool retro_load_game_special(unsigned type,const void *game,size_t count){(void)type;(void)game;(void)count;return false;}
void retro_run(void){usleep(33000);}
void retro_reset(void){}
void retro_unload_game(void){}
unsigned retro_get_region(void){return 0;}
size_t retro_serialize_size(void){return 0;}
bool retro_serialize(void *p,size_t n){(void)p;(void)n;return false;}
bool retro_unserialize(const void *p,size_t n){(void)p;(void)n;return false;}
void retro_cheat_reset(void){}
void retro_cheat_set(unsigned i,bool on,const char *code){(void)i;(void)on;(void)code;}
void *retro_get_memory_data(unsigned id){(void)id;return NULL;}
size_t retro_get_memory_size(unsigned id){(void)id;return 0;}
void retro_set_controller_port_device(unsigned p,unsigned d){(void)p;(void)d;}
