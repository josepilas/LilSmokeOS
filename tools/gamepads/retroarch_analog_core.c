#include <libretro.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static retro_environment_t environment;
static retro_video_refresh_t video;
static retro_input_poll_t poll;
static retro_input_state_t input;
static unsigned frames;
static int digital, seen;
static uint16_t pixels[64 * 64];
void retro_set_environment(retro_environment_t cb) { environment=cb; bool yes=true; cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME,&yes); }
void retro_set_video_refresh(retro_video_refresh_t cb) { video=cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { (void)cb; }
void retro_set_input_poll(retro_input_poll_t cb) { poll=cb; }
void retro_set_input_state(retro_input_state_t cb) { input=cb; }
unsigned retro_api_version(void) { return RETRO_API_VERSION; }
void retro_init(void) {
    enum retro_pixel_format format=RETRO_PIXEL_FORMAT_RGB565;
    environment(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT,&format);
    digital=getenv("QA_DIGITAL_ONLY")!=NULL;
    for(unsigned i=0;i<64*64;++i)pixels[i]=(uint16_t)(0x04a0+(i%64));
}
void retro_deinit(void) {}
void retro_get_system_info(struct retro_system_info *info) {
    memset(info,0,sizeof(*info));info->library_name="LilSmoke Analog QA";info->library_version="1";info->valid_extensions="gba";
}
void retro_get_system_av_info(struct retro_system_av_info *info) {
    memset(info,0,sizeof(*info));info->geometry=(struct retro_game_geometry){64,64,64,64,1.0f};
    info->timing=(struct retro_system_timing){60.0,44100.0};
}
void retro_set_controller_port_device(unsigned port,unsigned device) {(void)port;(void)device;}
void retro_reset(void) {}
void retro_run(void) {
    poll(); int valid=1;
    for(unsigned port=0;port<4;++port) {
        if(digital) {
            int right=input(port,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT);
            int up=input(port,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_UP);
            valid &= right && up;
        } else {
            int x=input(port,RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_LEFT,RETRO_DEVICE_ID_ANALOG_X);
            int y=input(port,RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_LEFT,RETRO_DEVICE_ID_ANALOG_Y);
            int rx=input(port,RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_RIGHT,RETRO_DEVICE_ID_ANALOG_X);
            int ry=input(port,RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_RIGHT,RETRO_DEVICE_ID_ANALOG_Y);
            valid &= x>2000 && y<-2000 && rx<-2000 && ry>2000;
            if(frames==30)fprintf(stderr,"QA_ANALOG port=%u axes=%d,%d,%d,%d\n",port,x,y,rx,ry);
        }
    }
    seen |= valid;
    video(pixels,64,64,64*2);
    if(++frames==120) {
        fprintf(stderr,"QA_ANALOG_CORE mode=%s passed=%d frames=%u\n",digital?"digital":"analog",seen,frames);
        fflush(stderr);environment(RETRO_ENVIRONMENT_SHUTDOWN,NULL);
    }
}
bool retro_load_game(const struct retro_game_info *info) {(void)info;return true;}
bool retro_load_game_special(unsigned type,const struct retro_game_info *info,size_t count) {(void)type;(void)info;(void)count;return false;}
void retro_unload_game(void) {}
size_t retro_serialize_size(void) {return 0;}
bool retro_serialize(void *data,size_t size) {(void)data;(void)size;return false;}
bool retro_unserialize(const void *data,size_t size) {(void)data;(void)size;return false;}
void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned i,bool enabled,const char *code) {(void)i;(void)enabled;(void)code;}
unsigned retro_get_region(void) {return RETRO_REGION_NTSC;}
void *retro_get_memory_data(unsigned id) {(void)id;return NULL;}
size_t retro_get_memory_size(unsigned id) {(void)id;return 0;}
