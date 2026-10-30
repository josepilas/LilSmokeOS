/* SPDX-License-Identifier: MPL-2.0
 * Copyright (c) 2026 Jose Pilas. */
#include "vendor/libretro.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static retro_environment_t env;
static retro_video_refresh_t video;
static retro_audio_sample_batch_t audio;
static retro_input_poll_t poll;
static retro_input_state_t input;
static unsigned frame;
static unsigned char ram[16];
static int mode;
static void lifecycle(const char *event) {
    const char *path=getenv("TEST_CORE_LIFECYCLE");if(!path)return;
    FILE *f=fopen(path,"a");assert(f);assert(fprintf(f,"%s\n",event)>0);assert(fclose(f)==0);
}
void retro_set_environment(retro_environment_t cb) {env=cb;bool no_game=true;assert(env(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME,&no_game));}
void retro_set_video_refresh(retro_video_refresh_t cb) {video=cb;}
void retro_set_audio_sample(retro_audio_sample_t cb) {(void)cb;}
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {audio=cb;}
void retro_set_input_poll(retro_input_poll_t cb) {poll=cb;}
void retro_set_input_state(retro_input_state_t cb) {input=cb;}
unsigned retro_api_version(void) {return RETRO_API_VERSION;}
void retro_init(void) {
    const char *m=getenv("TEST_CORE_MODE");mode=m?atoi(m):0;
    unsigned version=0;assert(env(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION,&version)&&version==2);
    struct retro_variable vars[]={{"test_color","Color; red|green"},{NULL,NULL}};
    assert(env(RETRO_ENVIRONMENT_SET_VARIABLES,vars));
    struct retro_variable v={"test_color",NULL};assert(env(RETRO_ENVIRONMENT_GET_VARIABLE,&v));assert(v.value);
    const char *path=NULL;assert(env(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY,&path)&&path);
    bool dupe=false;assert(env(RETRO_ENVIRONMENT_GET_CAN_DUPE,&dupe)&&dupe);
    assert(env(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS,NULL));
    assert(!env(RETRO_ENVIRONMENT_GET_VFS_INTERFACE,NULL));
    assert(!env(RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK,NULL));
    struct retro_hw_render_callback hw;memset(&hw,0,sizeof hw);hw.context_type=RETRO_HW_CONTEXT_OPENGL;
    assert(!env(RETRO_ENVIRONMENT_SET_HW_RENDER,&hw));
    enum retro_pixel_format format=mode==1?RETRO_PIXEL_FORMAT_XRGB8888:RETRO_PIXEL_FORMAT_RGB565;
    assert(env(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT,&format));
}
void retro_deinit(void) {lifecycle("deinit");}
void retro_reset(void) {frame=0;}
void retro_get_system_info(struct retro_system_info *i) {*i=(struct retro_system_info){"Independent test core","1","bin",false,false};}
void retro_get_system_av_info(struct retro_system_av_info *i) {*i=(struct retro_system_av_info){{32,24,32,24,4.0f/3.0f},{mode==10?1:60,44100}};}
void retro_set_controller_port_device(unsigned p,unsigned d) {(void)p;assert(d==RETRO_DEVICE_JOYPAD);}
bool retro_load_game(const struct retro_game_info *g) {(void)g;lifecycle("load");return true;}
void retro_unload_game(void) {lifecycle("unload");}
void retro_run(void) {
    static uint32_t pixels32[24][36];static uint16_t pixels16[24][36];
    static int16_t pcm[1024*2];if(mode!=9 && mode!=10)poll();
    if(frame==0)lifecycle("run");
    if(mode==5) {assert(input(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_A)==1);assert(input(0,RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_RIGHT,RETRO_DEVICE_ID_ANALOG_X)==12345);}
    if(mode==6) {assert(input(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK)==0);assert(input(0,RETRO_DEVICE_ANALOG,0,0)==0);}
    for(unsigned y=0;y<24;y++)for(unsigned x=0;x<32;x++){pixels32[y][x]=((x+frame)%32)*0x80000+y*0x800;pixels16[y][x]=(uint16_t)((x+frame)%32*2048+y*32);}
    for(unsigned i=0;i<2048;i++)pcm[i]=(int16_t)(i*7+frame);
    if(mode==2)video(pixels16,65535,24,72);
    else if(mode==3)audio(pcm,8193);
    else if(mode==4) {
        struct retro_framebuffer fb;memset(&fb,0,sizeof fb);fb.width=32;fb.height=24;fb.access_flags=RETRO_MEMORY_ACCESS_WRITE;
        assert(env(RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER,&fb));assert(fb.format==RETRO_PIXEL_FORMAT_RGB565);
        for(unsigned y=0;y<24;y++)memcpy((char*)fb.data+y*fb.pitch,pixels16[y],64);
        video(fb.data,32,24,fb.pitch);audio(pcm,735);
    } else {if(frame%3==2)video(NULL,32,24,0);else if(mode==1)video(pixels32,32,24,sizeof pixels32[0]);else video(pixels16,32,24,sizeof pixels16[0]);audio(pcm,735);}
    frame++;ram[0]=(unsigned char)frame;
}
size_t retro_serialize_size(void) {return mode==7?0:sizeof frame;}
bool retro_serialize(void *p,size_t n) {if(mode==8)return false;if(n!=sizeof frame)return false;memcpy(p,&frame,n);return true;}
bool retro_unserialize(const void *p,size_t n) {if(n!=sizeof frame)return false;memcpy(&frame,p,n);return true;}
void *retro_get_memory_data(unsigned id) {return id==RETRO_MEMORY_SAVE_RAM?ram:NULL;}
size_t retro_get_memory_size(unsigned id) {return id==RETRO_MEMORY_SAVE_RAM?sizeof ram:0;}
