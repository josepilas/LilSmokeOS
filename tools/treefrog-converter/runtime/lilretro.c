/* SPDX-License-Identifier: MPL-2.0
 * Copyright (c) 2026 Jose Pilas.
 * Independent software Libretro host. No PicoArch code or binary dependency. */
#define _GNU_SOURCE
#define JSMN_STRICT
#define JSMN_STATIC
#include "vendor/jsmn.h"
#include "vendor/libretro.h"
#include "gamepad-state.h"
#include <SDL.h>
#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef LILRETRO_ABI
#define LILRETRO_ABI "host-glibc"
#endif
#define MAX_OPTIONS 256
#define MAX_VALUES 128
#define MAX_COMMANDS 128
#define RING_FRAMES 65536
#define MAX_FILE (64u * 1024u * 1024u)
#define HOTKEY_SDL_PADS 16

struct exit_hotkey {
    uint64_t last, neutral_at, held_at;
    bool sampled, neutral, armed, holding;
};
static bool exit_hotkey_step(struct exit_hotkey *key,uint64_t now,uint32_t buttons,uint32_t combo,bool connected) {
    if(!connected || (key->sampled && (now<key->last || now-key->last>150)))
        memset(key,0,sizeof *key);
    if(!connected)return false;
    key->last=now;key->sampled=true;
    if(!(buttons&combo)) {
        if(!key->neutral){key->neutral=true;key->neutral_at=now;}
        if(now-key->neutral_at>=80)key->armed=true;
    } else key->neutral=false;
    if((buttons&combo)!=combo) {
        if(key->holding)key->armed=false;
        key->holding=false;return false;
    }
    if(!key->armed)return false;
    if(!key->holding){key->holding=true;key->held_at=now;}
    if(now-key->held_at<500)return false;
    key->armed=key->holding=false;return true;
}

struct sdl_exit_pad {
    SDL_Joystick *joystick;
#if SDL_MAJOR_VERSION >= 2
    SDL_GameController *controller;
    SDL_JoystickID instance;
#endif
    struct exit_hotkey key;
};

struct option {
    char key[128], value[256], description[512];
    char choices[MAX_VALUES][128];
    unsigned count;
    bool configured;
};
static struct {
    void *library;
    void (*init)(void), (*deinit)(void), (*run)(void), (*reset)(void);
    unsigned (*api_version)(void);
    void (*get_system_info)(struct retro_system_info *);
    void (*get_system_av_info)(struct retro_system_av_info *);
    void (*set_environment)(retro_environment_t);
    void (*set_video_refresh)(retro_video_refresh_t);
    void (*set_audio_sample)(retro_audio_sample_t);
    void (*set_audio_sample_batch)(retro_audio_sample_batch_t);
    void (*set_input_poll)(retro_input_poll_t);
    void (*set_input_state)(retro_input_state_t);
    void (*set_controller_port_device)(unsigned, unsigned);
    bool (*load_game)(const struct retro_game_info *);
    void (*unload_game)(void);
    size_t (*serialize_size)(void);
    bool (*serialize)(void *, size_t), (*unserialize)(const void *, size_t);
    void *(*get_memory_data)(unsigned);
    size_t (*get_memory_size)(unsigned);
} core;
static struct {
    char root[PATH_MAX], system[PATH_MAX], saves[PATH_MAX], diagnostic[PATH_MAX];
    char error[512];
    const char *core_path;
    unsigned width, height, audio_limit, latency, port, rotation;
    uint64_t frames, video_frames, duplicate_frames, audio_frames, audio_dropped;
    uint64_t video_hash, audio_hash, nonzero_audio;
    unsigned frame_limit, software_requests;
    bool probe, headless, stop, no_game, loaded, initialized, sdl, audio_open;
    bool hw_requested, option_update, pixel_seen[3], profile_abi, first_probe;
    unsigned scan_left;
    enum retro_pixel_format format;
    struct retro_system_info info;
    struct retro_system_av_info av;
    struct option options[MAX_OPTIONS];
    unsigned option_count;
    struct { unsigned command, calls, accepted; } commands[MAX_COMMANDS];
    unsigned command_count;
    uint8_t *framebuffer;
    size_t framebuffer_size;
    void *driver;
    int (*driver_video_init)(void);
    int (*driver_video_frame)(void *,int,int,int);
    void (*driver_video_close)(void);
    int (*driver_audio_init)(void *,int,int);
    int (*driver_audio_frame)(const int16_t *,int);
    int (*driver_audio_close)(void);
    bool driver_video, driver_audio;
    uint16_t *driver_pixels;
    size_t driver_pixel_count;
    unsigned audio_hash_budget;
    uint16_t buttons[4];
    int16_t axes[4][4];
    struct ls_gp_reader gamepad;
    struct ls_gp_state hotkey_previous;
    uint64_t hotkey_generation, hotkey_observed;
    struct exit_hotkey hotkeys[LS_GP_SLOTS], keyboard_hotkey;
    struct sdl_exit_pad sdl_hotkeys[HOTKEY_SDL_PADS];
    int hotkey_sdl_count;
    int16_t ring[RING_FRAMES * 2];
    unsigned ring_read, ring_write, ring_count;
    SDL_Joystick *joystick;
    SDL_Surface *screen;
#if SDL_MAJOR_VERSION >= 2
    SDL_Window *window;
#endif
} h;
static volatile sig_atomic_t interrupted;

static void fail(const char *format, ...) {
    if (h.error[0]) return;
    va_list ap; va_start(ap, format); vsnprintf(h.error, sizeof h.error, format, ap); va_end(ap);
    fprintf(stderr, "lilretro: %s\n", h.error); h.stop=true;
}
static void stop_signal(int sig) { interrupted=sig; }
static uint64_t micros(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) return 0;
    return (uint64_t)t.tv_sec*1000000+(unsigned long)t.tv_nsec/1000;
}
static void json_string(FILE *f, const char *s) {
    fputc('"', f);
    if(s) for(const unsigned char *p=(const unsigned char*)s; *p; ++p) {
        if(*p=='"' || *p=='\\') { fputc('\\',f); fputc(*p,f); }
        else if(*p<32 || *p>=127) fprintf(f,"\\u%04x",*p);
        else fputc(*p,f);
    }
    fputc('"',f);
}
static bool path_join(char *out, size_t n, const char *base, const char *path) {
    int count=path[0]=='/' ? snprintf(out,n,"%s",path) : snprintf(out,n,"%s/%s",base,path);
    return count>=0 && (size_t)count<n;
}
static bool make_directory(const char *path) {
    char buf[PATH_MAX]; struct stat st;
    if(strlen(path)>=sizeof buf) return false;
    strcpy(buf,path);
    for(char *p=buf+1; ; ++p) {
        if(*p!='/' && *p)continue;
        char c=*p; *p=0;
        if(mkdir(buf,0700) && errno!=EEXIST) return false;
        if(stat(buf,&st) || !S_ISDIR(st.st_mode)) return false;
        *p=c; if(!c) break;
    }
    return true;
}
static struct option *get_option(const char *key, bool create) {
    if(!key || strlen(key)>=sizeof h.options[0].key) return NULL;
    for(unsigned i=0;i<h.option_count;i++) if(!strcmp(key,h.options[i].key)) return &h.options[i];
    if(!create || h.option_count==MAX_OPTIONS) return NULL;
    struct option *o=&h.options[h.option_count++]; strcpy(o->key,key); return o;
}
static bool token_string(const char *text, const jsmntok_t *t, char *out, size_t n) {
    if(t->type!=JSMN_STRING || (size_t)(t->end-t->start)>=n) return false;
    size_t j=0;
    for(int i=t->start;i<t->end;i++) {
        char c=text[i];
        if(c=='\\') {
            if(++i>=t->end)return false;
            c=text[i];
            if(c=='n') c='\n'; else if(c=='r')c='\r'; else if(c=='t')c='\t';
            else if(c!='"' && c!='\\' && c!='/')return false;
        }
        if((unsigned char)c<32)return false;
        out[j++]=c;
    }
    out[j]=0; return true;
}
static bool profile(const char *path) {
    char resolved[PATH_MAX],text[65537]; jsmntok_t tokens[4096]; jsmn_parser parser;
    if(!realpath(path,resolved)) {fail("Cannot resolve profile: %s",strerror(errno));return false;}
    char *slash=strrchr(resolved,'/'); *slash=0; strcpy(h.root,resolved);
    FILE *f=fopen(path,"rb"); if(!f) return false;
    size_t size=fread(text,1,sizeof text-1,f); bool bad=ferror(f) || !feof(f); fclose(f); text[size]=0;
    jsmn_init(&parser); int count=jsmn_parse(&parser,text,size,tokens,4096);
    if(bad || count<1 || tokens[0].type!=JSMN_OBJECT || tokens[0].end!=(int)size-(int)(size && text[size-1]=='\n')) {
        /* jsmn validates tokens; trailing JSON values are rejected below. */
        if(bad || count<1 || tokens[0].type!=JSMN_OBJECT) {fail("Invalid or oversized JSON profile");return false;}
    }
    for(size_t p=(size_t)tokens[0].end;p<size;p++) if(text[p]!=' '&&text[p]!='\n'&&text[p]!='\r'&&text[p]!='\t') {fail("Trailing profile JSON");return false;}
    bool schema=false;
    for(int i=1;i<count;) {
        char key[128],value[PATH_MAX];
        if(!token_string(text,&tokens[i++],key,sizeof key) || i>=count) {fail("Invalid profile key");return false;}
        jsmntok_t *t=&tokens[i++];
        if(!strcmp(key,"options") || !strcmp(key,"core_options")) {
            if(t->type!=JSMN_OBJECT) {fail("options must be an object");return false;}
            while(i<count && tokens[i].start<t->end) {
                char k[128],v[256];
                if(i+1>=count || !token_string(text,&tokens[i++],k,sizeof k) || !token_string(text,&tokens[i++],v,sizeof v)) {fail("Invalid core option");return false;}
                struct option *o=get_option(k,true); if(!o) {fail("Too many options");return false;}
                strcpy(o->value,v);o->configured=true;
            }
        } else if(!strcmp(key,"renderer") || !strcmp(key,"input") || !strcmp(key,"port_id")) {
            if(!token_string(text,t,value,sizeof value) || (!strcmp(key,"renderer") && strcmp(value,"software")) || (!strcmp(key,"input") && strcmp(value,"retropad"))) {fail("Unsupported profile renderer/input");return false;}
        } else if(!strcmp(key,"hardware_rendering") || !strcmp(key,"probe_on_first_launch")) {
            bool yes=t->type==JSMN_PRIMITIVE && t->end-t->start==4 && !memcmp(text+t->start,"true",4);
            bool no=t->type==JSMN_PRIMITIVE && t->end-t->start==5 && !memcmp(text+t->start,"false",5);
            if((!yes&&!no) || (!strcmp(key,"hardware_rendering") && yes)) {fail("GPU rendering is unsupported");return false;}
            if(!strcmp(key,"probe_on_first_launch"))h.first_probe=yes;
        } else if(!strcmp(key,"abi") || !strcmp(key,"system_directory") || !strcmp(key,"save_directory")) {
            if(!token_string(text,t,value,sizeof value)) {fail("Invalid path/ABI string");return false;}
            if(!strcmp(key,"abi")) { if(strcmp(value,LILRETRO_ABI)) {fail("Profile ABI %s does not match runner %s",value,LILRETRO_ABI);return false;} h.profile_abi=true; }
            else if(!path_join(!strcmp(key,"system_directory")?h.system:h.saves,PATH_MAX,h.root,value)) {fail("Path too long");return false;}
        } else {
            if(t->type!=JSMN_PRIMITIVE || t->end-t->start>10) {fail("Unknown or nonnumeric profile key: %s",key);return false;}
            memcpy(value,text+t->start,(size_t)(t->end-t->start)); value[t->end-t->start]=0;
            char *end; unsigned long n=strtoul(value,&end,10);
            if(!*value || *end || value[0]=='-' || n>1000000) {fail("Invalid profile number");return false;}
            if(!strcmp(key,"schema"))schema=n==1;
            else if(!strcmp(key,"max_width"))h.width=(unsigned)n;
            else if(!strcmp(key,"max_height"))h.height=(unsigned)n;
            else if(!strcmp(key,"max_audio_frames"))h.audio_limit=(unsigned)n;
            else if(!strcmp(key,"audio_latency_ms"))h.latency=(unsigned)n;
            else if(!strcmp(key,"controller_port"))h.port=(unsigned)n;
            else {fail("Unknown profile key: %s",key);return false;}
        }
    }
    if(!schema || !h.width || !h.height || h.width>2048 || h.height>2048 ||
       h.width*h.height>2097152 || !h.audio_limit || h.audio_limit>32768 || h.latency<8 || h.latency>500 || h.port>3) {fail("Profile schema/ABI/budget outside supported bounds");return false;}
    if(!h.system[0])path_join(h.system,PATH_MAX,h.root,"system");
    if(!h.saves[0])path_join(h.saves,PATH_MAX,h.root,"saves");
    if(!h.diagnostic[0])path_join(h.diagnostic,PATH_MAX,h.root,"runtime-capabilities.json");
    if(h.first_probe && access(h.diagnostic,F_OK))h.scan_left=180;
    return true;
}
static void core_log(enum retro_log_level level, const char *fmt, ...) {
    (void)level; va_list ap;va_start(ap,fmt);vfprintf(stderr,fmt,ap);va_end(ap);
}
static void register_option(const char *key,const char *desc,const char *def,const struct retro_core_option_value *values) {
    struct option *o=get_option(key,true);
    if(!o) {fail("Core exceeded option budget");return;}
    snprintf(o->description,sizeof o->description,"%s",desc?desc:""); o->count=0;
    if(values)for(unsigned j=0;j<MAX_VALUES && values[j].value;j++) {
        if(strlen(values[j].value)>=sizeof o->choices[0]) {fail("Core option value too long");return;}
        strcpy(o->choices[o->count++],values[j].value);
    }
    if(!o->configured)snprintf(o->value,sizeof o->value,"%s",def?def:(o->count?o->choices[0]:""));
    bool found=o->count==0;
    for(unsigned j=0;j<o->count;j++)if(!strcmp(o->value,o->choices[j]))found=true;
    if(!found)fail("Invalid configured value for option %s",key);
}
static bool valid_av(const struct retro_system_av_info *av) {
    return av->geometry.base_width && av->geometry.base_height && av->geometry.base_width<=h.width &&
      av->geometry.base_height<=h.height && av->geometry.max_width<=h.width && av->geometry.max_height<=h.height &&
      isfinite(av->timing.fps) && av->timing.fps>=1 && av->timing.fps<=240 &&
      isfinite(av->timing.sample_rate) && av->timing.sample_rate>=8000 && av->timing.sample_rate<=192000;
}
static bool environment_impl(unsigned cmd,void *data) {
    switch(cmd) {
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: if(!data)return false; *(bool*)data=true;return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: if(!data)return false; *(const char**)data=h.system;return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: if(!data)return false; *(const char**)data=h.saves;return true;
    case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY: if(!data)return false; *(const char**)data=h.root;return true;
    case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH: if(!data)return false; *(const char**)data=h.core_path;return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: if(!data)return false; ((struct retro_log_callback*)data)->log=core_log;return true;
    case RETRO_ENVIRONMENT_GET_OVERSCAN: if(!data)return false; *(bool*)data=false;return true;
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME: if(!data)return false; h.no_game=*(bool*)data;return true;
    case RETRO_ENVIRONMENT_SHUTDOWN: h.stop=true;return true;
    case RETRO_ENVIRONMENT_SET_ROTATION: if(!data || *(unsigned*)data!=0)return false;h.rotation=0;return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        if(!data)return false; enum retro_pixel_format p=*(enum retro_pixel_format*)data;
        if(p<RETRO_PIXEL_FORMAT_0RGB1555 || p>RETRO_PIXEL_FORMAT_RGB565)return false;
        h.format=p;h.pixel_seen[p]=true;return true;
    }
    case RETRO_ENVIRONMENT_SET_HW_RENDER: h.hw_requested=true;return false;
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER: if(!data)return false; *(unsigned*)data=RETRO_HW_CONTEXT_NONE;return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS: return true;
    case RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES: if(!data)return false; *(uint64_t*)data=(UINT64_C(1)<<RETRO_DEVICE_JOYPAD)|(UINT64_C(1)<<RETRO_DEVICE_ANALOG);return true;
    case RETRO_ENVIRONMENT_GET_INPUT_MAX_USERS: if(!data)return false;*(unsigned*)data=4;return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE: if(!data)return false;*(unsigned*)data=RETRO_LANGUAGE_ENGLISH;return true;
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS: return data!=NULL;
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO: return data!=NULL;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: if(!data)return false;*(unsigned*)data=2;return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: if(!data)return false;*(bool*)data=h.option_update;h.option_update=false;return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        if(!data)return false;struct retro_variable *v=data;struct option *o=get_option(v->key,false);
        v->value=o?o->value:NULL;return o!=NULL;
    }
    case RETRO_ENVIRONMENT_SET_VARIABLES: {
        if(!data)return false;const struct retro_variable *v=data;
        for(unsigned i=0;i<MAX_OPTIONS && v[i].key;i++) {
            char buf[8192];if(!v[i].value || strlen(v[i].value)>=sizeof buf)return false;
            strcpy(buf,v[i].value);char *choices=strchr(buf,';');if(!choices)return false;*choices++=0;if(*choices==' ')choices++;
            struct retro_core_option_value vals[MAX_VALUES+1];memset(vals,0,sizeof vals);
            unsigned n=0;char *save=NULL;
            for(char *p=strtok_r(choices,"|",&save);p && n<MAX_VALUES;p=strtok_r(NULL,"|",&save))vals[n++].value=p;
            register_option(v[i].key,buf,n?vals[0].value:NULL,vals);
        }
        return true;
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL: {
        const struct retro_core_option_definition *d=cmd==RETRO_ENVIRONMENT_SET_CORE_OPTIONS?data:(data?((struct retro_core_options_intl*)data)->us:NULL);
        if(!d)return false;
        for(unsigned i=0;i<MAX_OPTIONS && d[i].key;i++)register_option(d[i].key,d[i].desc,d[i].default_value,d[i].values);
        return true;
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL: {
        const struct retro_core_options_v2 *v=cmd==RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2?data:(data?((struct retro_core_options_v2_intl*)data)->us:NULL);
        if(!v || !v->definitions)return false;
        for(unsigned i=0;i<MAX_OPTIONS && v->definitions[i].key;i++) {
            const struct retro_core_option_v2_definition *d=&v->definitions[i];register_option(d->key,d->desc,d->default_value,d->values);
        }
        /* Options are installed; false correctly declares no category UI support. */
        return false;
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY: return false;
    case RETRO_ENVIRONMENT_SET_GEOMETRY: {
        if(!data)return false; const struct retro_game_geometry *g=data;
        if(!g->base_width || !g->base_height || g->base_width>h.width || g->base_height>h.height)return false;
        h.av.geometry=*g;return true;
    }
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        /* Audio reconfiguration during callbacks is deliberately unsupported. */
        return false;
    case RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER: {
        if(!data)return false;struct retro_framebuffer *f=data;
        if(!f->width || !f->height || f->width>h.width || f->height>h.height || (f->access_flags&RETRO_MEMORY_ACCESS_READ))return false;
        size_t pitch=(size_t)f->width*(h.format==RETRO_PIXEL_FORMAT_XRGB8888?4:2),need=pitch*f->height;
        if(need>h.framebuffer_size) {void *p=realloc(h.framebuffer,need);if(!p)return false;h.framebuffer=p;h.framebuffer_size=need;}
        f->data=h.framebuffer;f->pitch=pitch;f->format=h.format;f->memory_flags=RETRO_MEMORY_TYPE_CACHED;h.software_requests++;return true;
    }
    case RETRO_ENVIRONMENT_GET_FASTFORWARDING: if(!data)return false;*(bool*)data=false;return true;
    case RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE: if(!data)return false;*(float*)data=60.0f;return true;
    default: return false;
    }
}
static bool environment(unsigned cmd,void *data) {
    bool accepted=environment_impl(cmd,data); unsigned i;
    for(i=0;i<h.command_count;i++)if(h.commands[i].command==cmd)break;
    if(i==h.command_count && i<MAX_COMMANDS){h.commands[i].command=cmd;h.command_count++;}
    if(i<MAX_COMMANDS){h.commands[i].calls++;if(accepted)h.commands[i].accepted++;}
    return accepted;
}
static uint64_t hash_bytes(uint64_t hash,const void *data,size_t size) {
    const uint8_t *p=data;if(!hash)hash=UINT64_C(14695981039346656037);
    for(size_t i=0;i<size;i++){hash^=p[i];hash*=UINT64_C(1099511628211);}return hash;
}
static void video(const void *data,unsigned w,unsigned height,size_t pitch) {
    if(!data){h.duplicate_frames++;return;}
    if(data==RETRO_HW_FRAME_BUFFER_VALID){fail("Core submitted unsupported GPU frame");return;}
    unsigned bytes=h.format==RETRO_PIXEL_FORMAT_XRGB8888?4:2;
    if(!w || !height || w>h.width || height>h.height || pitch<(size_t)w*bytes || pitch>16u*1024u*1024u) {fail("Core video exceeds profile budget");return;}
    h.video_frames++;
    if((h.probe || h.scan_left) && (h.frames==0 || h.frames==59 || h.frames==179)) {
        /* Probe work is bounded: at most 1024 sampled bytes, three frames. */
        for(unsigned i=0;i<16;i++) {
            unsigned y=i*(height-1)/15;size_t n=(size_t)w*bytes;if(n>64)n=64;
            h.video_hash=hash_bytes(h.video_hash,(const uint8_t*)data+y*pitch,n);
        }
    }
    if(h.headless)return;
    if(h.driver_video) {
        const void *pixels=data;size_t driver_pitch=pitch;
        if(h.format!=RETRO_PIXEL_FORMAT_RGB565) {
            size_t count=(size_t)w*height;
            if(count>h.driver_pixel_count) {
                void *p=realloc(h.driver_pixels,count*sizeof(uint16_t));
                if(!p){fail("Cannot allocate RGB565 conversion buffer");return;}
                h.driver_pixels=p;h.driver_pixel_count=count;
            }
            for(unsigned y=0;y<height;y++)for(unsigned x=0;x<w;x++) {
                uint32_t pixel=0;memcpy(&pixel,(const uint8_t*)data+y*pitch+x*bytes,bytes);
                h.driver_pixels[y*w+x]=h.format==RETRO_PIXEL_FORMAT_XRGB8888 ?
                    (uint16_t)(((pixel>>8)&0xf800)|((pixel>>5)&0x07e0)|((pixel>>3)&0x001f)) :
                    (uint16_t)(((pixel&0x7fe0)<<1)|((pixel>>4)&0x20)|(pixel&0x1f));
            }
            pixels=h.driver_pixels;driver_pitch=(size_t)w*2;
        }
        if(h.driver_video_frame((void*)pixels,(int)w,(int)height,(int)driver_pitch)<0)fail("OS board driver rejected software frame");
        return;
    }
    uint32_t r=h.format==RETRO_PIXEL_FORMAT_XRGB8888?0xff0000:(h.format==RETRO_PIXEL_FORMAT_RGB565?0xf800:0x7c00);
    uint32_t g=h.format==RETRO_PIXEL_FORMAT_XRGB8888?0x00ff00:(h.format==RETRO_PIXEL_FORMAT_RGB565?0x07e0:0x03e0);
    uint32_t b=h.format==RETRO_PIXEL_FORMAT_XRGB8888?0x0000ff:0x001f;
    SDL_Surface *surface=SDL_CreateRGBSurfaceFrom((void*)data,(int)w,(int)height,(int)bytes*8,(int)pitch,r,g,b,0);
    if(!surface){fail("SDL software surface: %s",SDL_GetError());return;}
#if SDL_MAJOR_VERSION >= 2
    h.screen=SDL_GetWindowSurface(h.window);
    if(!h.screen || SDL_BlitScaled(surface,NULL,h.screen,NULL) || SDL_UpdateWindowSurface(h.window))fail("SDL software video: %s",SDL_GetError());
#else
    SDL_Surface *converted=SDL_ConvertSurface(surface,h.screen->format,0);
    if(!converted || SDL_SoftStretch(converted,NULL,h.screen,NULL) || SDL_Flip(h.screen))fail("SDL software video: %s",SDL_GetError());
    if(converted)SDL_FreeSurface(converted);
#endif
    SDL_FreeSurface(surface);
}
static void audio_callback(void *unused,Uint8 *stream,int length) {
    (void)unused;memset(stream,0,(size_t)length);unsigned frames=(unsigned)length/4;
    if(frames>h.ring_count)frames=h.ring_count;
    for(unsigned i=0;i<frames;i++) {memcpy(stream+i*4,h.ring+h.ring_read*2,4);h.ring_read=(h.ring_read+1)%RING_FRAMES;}
    h.ring_count-=frames;
}
static size_t audio_batch(const int16_t *samples,size_t frames) {
    if(!samples || frames>h.audio_limit) {fail("Core audio batch exceeds profile budget");return 0;}
    h.audio_frames+=frames;
    if((h.probe || h.scan_left) && h.audio_hash_budget) {
        size_t bytes=frames*4;if(bytes>h.audio_hash_budget)bytes=h.audio_hash_budget;
        h.audio_hash=hash_bytes(h.audio_hash,samples,bytes);h.audio_hash_budget-=(unsigned)bytes;
        for(size_t i=0;i<bytes/2;i++)if(samples[i])h.nonzero_audio++;
    }
    if(h.driver_audio) {
        if(h.driver_audio_frame(samples,(int)frames)<0){fail("OS board driver rejected PCM batch");return 0;}
        return frames;
    }
    if(!h.audio_open)return frames;
    SDL_LockAudio();
    size_t space=RING_FRAMES-h.ring_count,accepted=frames<space?frames:space;
    for(size_t i=0;i<accepted;i++){memcpy(h.ring+h.ring_write*2,samples+i*2,4);h.ring_write=(h.ring_write+1)%RING_FRAMES;}
    h.ring_count+=(unsigned)accepted;SDL_UnlockAudio();h.audio_dropped+=frames-accepted;
    return accepted;
}
static void audio_sample(int16_t left,int16_t right) {int16_t samples[2]={left,right};(void)audio_batch(samples,1);}
static void hotkey_exit(const char *source,unsigned slot,uint32_t buttons) {
    if(!h.stop)fprintf(stderr,"LILRETRO_EXIT_HOTKEY source=%s slot=%u buttons=%08x hold_ms=500\n",source,slot,buttons);
    h.stop=true;
}
static void close_sdl_exit_pad(struct sdl_exit_pad *pad) {
#if SDL_MAJOR_VERSION >= 2
    if(pad->controller)SDL_GameControllerClose(pad->controller);
    else
#endif
    if(pad->joystick)SDL_JoystickClose(pad->joystick);
    memset(pad,0,sizeof *pad);
}
static void poll_sdl_hotkeys(uint64_t now) {
    int count=SDL_NumJoysticks();
    for(int i=0;i<HOTKEY_SDL_PADS;i++) {
        struct sdl_exit_pad *pad=&h.sdl_hotkeys[i];
#if SDL_MAJOR_VERSION >= 2
        SDL_JoystickID instance=i<count?SDL_JoystickGetDeviceInstanceID(i):-1;
        if(pad->joystick && (!SDL_JoystickGetAttached(pad->joystick) || pad->instance!=instance))
            close_sdl_exit_pad(pad);
#else
        /* SDL1 has no instance IDs or attached query. A changed enumeration
           invalidates all holds; SDL2 additionally detects same-count swaps. */
        if(h.hotkey_sdl_count!=count)close_sdl_exit_pad(pad);
#endif
        if(i>=count){close_sdl_exit_pad(pad);continue;}
        if(!pad->joystick) {
#if SDL_MAJOR_VERSION >= 2
            if(SDL_IsGameController(i)) {
                pad->controller=SDL_GameControllerOpen(i);
                if(pad->controller)pad->joystick=SDL_GameControllerGetJoystick(pad->controller);
            } else
#endif
            pad->joystick=SDL_JoystickOpen(i);
#if SDL_MAJOR_VERSION >= 2
            pad->instance=instance;
#endif
        }
        uint32_t buttons=0;
        if(pad->joystick) {
#if SDL_MAJOR_VERSION >= 2
            if(pad->controller) {
                if(SDL_GameControllerGetButton(pad->controller,SDL_CONTROLLER_BUTTON_BACK))buttons|=1;
                if(SDL_GameControllerGetButton(pad->controller,SDL_CONTROLLER_BUTTON_START))buttons|=2;
            } else
#endif
            {
                /* Unmapped SDL joysticks retain the runtime's RetroPad ABI. */
                if(SDL_JoystickNumButtons(pad->joystick)>RETRO_DEVICE_ID_JOYPAD_SELECT &&
                   SDL_JoystickGetButton(pad->joystick,RETRO_DEVICE_ID_JOYPAD_SELECT))buttons|=1;
                if(SDL_JoystickNumButtons(pad->joystick)>RETRO_DEVICE_ID_JOYPAD_START &&
                   SDL_JoystickGetButton(pad->joystick,RETRO_DEVICE_ID_JOYPAD_START))buttons|=2;
            }
        }
        if(exit_hotkey_step(&pad->key,now,buttons,3,pad->joystick!=NULL))hotkey_exit("sdl",(unsigned)i,buttons);
    }
    h.hotkey_sdl_count=count;
}
/* 0: invalid; 1: writer busy, defer; 2: recovered coherent live packet. */
static int exit_snapshot_pending(const struct ls_gp_reader *reader,const struct ls_gp_state *previous,
                                 uint64_t generation,uint64_t observed,uint64_t now,struct ls_gp_state *recovered) {
    uint64_t fresh=ls_gp_now();
    if(!reader->map || reader->fd<0 || reader->generation!=generation || previous->magic!=LS_GP_MAGIC ||
       now<observed || now-observed>150 || previous->millis>fresh || fresh-previous->millis>500)return false;
    const char *path=getenv("LILSMOKE_GAMEPAD_STATE");
    struct stat named,opened;
    if(!path)path="/run/lilsmoke-gamepads.state";
    if(stat(path,&named) || fstat(reader->fd,&opened) || named.st_dev!=opened.st_dev ||
       named.st_ino!=opened.st_ino || named.st_size!=sizeof *previous)return false;
    uint32_t sequence=__atomic_load_n(&reader->map->sequence,__ATOMIC_ACQUIRE);
    if(sequence&1)return true;
    struct ls_gp_state current;memcpy(&current,reader->map,sizeof current);
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if(sequence!=__atomic_load_n(&reader->map->sequence,__ATOMIC_ACQUIRE))return true;
    fresh=ls_gp_now();
    if(current.magic!=LS_GP_MAGIC || current.abi!=1 ||
       current.millis>fresh || fresh-current.millis>500)return false;
    *recovered=current;return 2;
}
static void poll_shared_hotkeys(const struct ls_gp_state *s,bool valid,uint64_t now) {
    /* A failed seqlock read is not a release or unplug. Never fire from cache. */
    struct ls_gp_state recovered;
    if(!valid) {
        int recovery=exit_snapshot_pending(&h.gamepad,&h.hotkey_previous,h.hotkey_generation,h.hotkey_observed,now,&recovered);
        if(recovery==1)return;
        if(recovery==2){s=&recovered;valid=true;}
    }
    for(unsigned p=0;p<LS_GP_SLOTS;p++) {
        const struct ls_gp_pad *pad=&s->pads[p],*old=&h.hotkey_previous.pads[p];
        if(h.hotkey_generation!=h.gamepad.generation || h.hotkey_previous.pid!=s->pid ||
           old->instance!=pad->instance || old->vendor!=pad->vendor ||
           old->product!=pad->product || old->version!=pad->version)
            memset(&h.hotkeys[p],0,sizeof h.hotkeys[p]);
        if(exit_hotkey_step(&h.hotkeys[p],now,pad->buttons,
                           (1u<<LS_GP_BACK)|(1u<<LS_GP_START),valid && pad->connected))hotkey_exit("shared",p,pad->buttons);
    }
    h.hotkey_previous=*s;h.hotkey_generation=h.gamepad.generation;
    if(valid)h.hotkey_observed=now;
}
static void poll_input(void) {
    uint64_t now=ls_gp_now();
    memset(h.buttons,0,sizeof h.buttons);memset(h.axes,0,sizeof h.axes);
    if(h.sdl) {
        SDL_Event e;
        while(SDL_PollEvent(&e))if(e.type==SDL_QUIT)h.stop=true;
#if SDL_MAJOR_VERSION >= 2
        const Uint8 *keys=SDL_GetKeyboardState(NULL);
        const int keyboard[16]={SDL_SCANCODE_Z,SDL_SCANCODE_A,SDL_SCANCODE_RSHIFT,SDL_SCANCODE_RETURN,SDL_SCANCODE_UP,SDL_SCANCODE_DOWN,SDL_SCANCODE_LEFT,SDL_SCANCODE_RIGHT,SDL_SCANCODE_X,SDL_SCANCODE_S,SDL_SCANCODE_Q,SDL_SCANCODE_W,SDL_SCANCODE_E,SDL_SCANCODE_R,SDL_SCANCODE_1,SDL_SCANCODE_2};
        if(keys[SDL_SCANCODE_ESCAPE])h.stop=true;
        if(h.joystick && !SDL_JoystickGetAttached(h.joystick)){SDL_JoystickClose(h.joystick);h.joystick=NULL;}
#else
        const Uint8 *keys=SDL_GetKeyState(NULL);
        const int keyboard[16]={SDLK_z,SDLK_a,SDLK_RSHIFT,SDLK_RETURN,SDLK_UP,SDLK_DOWN,SDLK_LEFT,SDLK_RIGHT,SDLK_x,SDLK_s,SDLK_q,SDLK_w,SDLK_e,SDLK_r,SDLK_1,SDLK_2};
        if(keys[SDLK_ESCAPE])h.stop=true;
#endif
        for(unsigned i=0;i<16;i++)if(keys[keyboard[i]])h.buttons[h.port]|=(uint16_t)(1u<<i);
        if(exit_hotkey_step(&h.keyboard_hotkey,now,h.buttons[h.port],
                           (1u<<RETRO_DEVICE_ID_JOYPAD_SELECT)|(1u<<RETRO_DEVICE_ID_JOYPAD_START),true))hotkey_exit("keyboard",h.port,h.buttons[h.port]);
        if(!h.joystick && SDL_NumJoysticks()>0)h.joystick=SDL_JoystickOpen(0);
        SDL_JoystickUpdate();
        poll_sdl_hotkeys(now);
        if(h.joystick) {
            for(int i=0;i<16 && i<SDL_JoystickNumButtons(h.joystick);i++)if(SDL_JoystickGetButton(h.joystick,i))h.buttons[h.port]|=(uint16_t)(1u<<i);
            for(int i=0;i<4 && i<SDL_JoystickNumAxes(h.joystick);i++)h.axes[h.port][i]=SDL_JoystickGetAxis(h.joystick,i);
        }
    }
    struct ls_gp_state s;
    bool valid=ls_gp_read(&h.gamepad,&s)!=0;
    poll_shared_hotkeys(&s,valid,now);
    if(valid)for(unsigned p=0;p<4;p++) {
        if(!s.pads[p].connected)continue;
        static const unsigned map[15]={RETRO_DEVICE_ID_JOYPAD_A,RETRO_DEVICE_ID_JOYPAD_B,RETRO_DEVICE_ID_JOYPAD_X,RETRO_DEVICE_ID_JOYPAD_Y,RETRO_DEVICE_ID_JOYPAD_SELECT,16,RETRO_DEVICE_ID_JOYPAD_START,RETRO_DEVICE_ID_JOYPAD_L3,RETRO_DEVICE_ID_JOYPAD_R3,RETRO_DEVICE_ID_JOYPAD_L,RETRO_DEVICE_ID_JOYPAD_R,RETRO_DEVICE_ID_JOYPAD_UP,RETRO_DEVICE_ID_JOYPAD_DOWN,RETRO_DEVICE_ID_JOYPAD_LEFT,RETRO_DEVICE_ID_JOYPAD_RIGHT};
        for(unsigned i=0;i<15;i++)if(map[i]<16 && (s.pads[p].buttons&(1u<<i)))h.buttons[p]|=(uint16_t)(1u<<map[i]);
        memcpy(h.axes[p],s.pads[p].axes,sizeof h.axes[p]);
        if(s.pads[p].axes[4]>16384)h.buttons[p]|=1u<<RETRO_DEVICE_ID_JOYPAD_L2;
        if(s.pads[p].axes[5]>16384)h.buttons[p]|=1u<<RETRO_DEVICE_ID_JOYPAD_R2;
    }
}
static int16_t input_state(unsigned port,unsigned device,unsigned index,unsigned id) {
    if(port>=4)return 0;
    if(device==RETRO_DEVICE_JOYPAD && index==0) {
        if(id==RETRO_DEVICE_ID_JOYPAD_MASK)return (int16_t)h.buttons[port];
        if(id<16)return (h.buttons[port]>>id)&1;
    }
    if(device==RETRO_DEVICE_ANALOG && index<=RETRO_DEVICE_INDEX_ANALOG_RIGHT && id<=RETRO_DEVICE_ID_ANALOG_Y)return h.axes[port][index*2+id];
    return 0;
}
static bool load_symbols(const char *path) {
    FILE *f=fopen(path,"rb");unsigned char header[20];
    if(!f || fread(header,1,sizeof header,f)!=sizeof header || memcmp(header,ELFMAG,SELFMAG)) {if(f)fclose(f);fail("Core is not an ELF binary");return false;}
#ifdef __mips__
    if(header[EI_CLASS]!=ELFCLASS32 || header[EI_DATA]!=ELFDATA2LSB || header[18]!=EM_MIPS || header[19]) {fclose(f);fail("Core is not MIPS32 little endian");return false;}
#endif
    /* Reject known foreign libc before dlopen can run ELF constructors. Static
       converter closure validation remains required for transitive libraries. */
    char scan[8225];size_t retained=0,n;
    rewind(f);
    while((n=fread(scan+retained,1,8192,f))) {
        size_t length=retained+n;scan[length]=0;
        for(size_t i=0;i<length;i++) {
#ifdef __GLIBC__
            if(i+9<=length && !memcmp(scan+i,"libc.musl",9)){fclose(f);fail("musl core cannot enter glibc process");return false;}
#else
            if((i+6<=length && !memcmp(scan+i,"GLIBC_",6)) || (i+9<=length && !memcmp(scan+i,"libc.so.6",9))) {fclose(f);fail("glibc core cannot enter musl process");return false;}
#endif
        }
        retained=length<16?length:16;memmove(scan,scan+length-retained,retained);
    }
    fclose(f);
    core.library=dlopen(path,RTLD_NOW|RTLD_LOCAL);
    if(!core.library){fail("dlopen: %s",dlerror());return false;}
#define LOAD(name) do { void *p=dlsym(core.library,"retro_" #name);if(!p){fail("Missing retro_" #name);return false;}memcpy(&core.name,&p,sizeof p); } while(0)
    LOAD(init);LOAD(deinit);LOAD(run);LOAD(reset);LOAD(api_version);LOAD(get_system_info);LOAD(get_system_av_info);
    LOAD(set_environment);LOAD(set_video_refresh);LOAD(set_audio_sample);LOAD(set_audio_sample_batch);LOAD(set_input_poll);LOAD(set_input_state);
    LOAD(set_controller_port_device);LOAD(load_game);LOAD(unload_game);LOAD(serialize_size);LOAD(serialize);LOAD(unserialize);LOAD(get_memory_data);LOAD(get_memory_size);
#undef LOAD
    return true;
}
static bool state_file(bool save) {
    if(!h.loaded)return false;
    size_t size=core.serialize_size();if(!size || size>MAX_FILE)return false;
    void *data=malloc(size);if(!data)return false;
    char path[PATH_MAX],temp[PATH_MAX];bool ok=false;
    if(!path_join(path,sizeof path,h.saves,"state.bin") || snprintf(temp,sizeof temp,"%s.tmp.%ld",path,(long)getpid())>=(int)sizeof temp){free(data);return false;}
    if(save) {
        if(core.serialize(data,size)) {
            FILE *f=fopen(temp,"wb");
            if(f){ok=fwrite(data,1,size,f)==size;if(fclose(f))ok=false;if(ok)ok=rename(temp,path)==0;if(!ok)unlink(temp);}
        }
    } else {
        FILE *f=fopen(path,"rb");if(f){ok=fread(data,1,size,f)==size && fgetc(f)==EOF;if(ferror(f))ok=false;fclose(f);if(ok)ok=core.unserialize(data,size);}
    }
    free(data);return ok;
}
static bool save_ram(bool save,unsigned id,const char *name) {
    size_t size=core.get_memory_size(id);void *data=core.get_memory_data(id);
    if(!size)return true;
    if(size>MAX_FILE || !data)return false;
    char path[PATH_MAX],temp[PATH_MAX];if(!path_join(path,sizeof path,h.saves,name))return false;
    if(!save){FILE *f=fopen(path,"rb");if(!f)return errno==ENOENT;struct stat st;bool ok=!fstat(fileno(f),&st)&&(uint64_t)st.st_size==size;if(ok)ok=fread(data,1,size,f)==size;fclose(f);return ok;}
    if(snprintf(temp,sizeof temp,"%s.tmp.%ld",path,(long)getpid())>=(int)sizeof temp)return false;
    FILE *f=fopen(temp,"wb");if(!f)return false;bool ok=fwrite(data,1,size,f)==size;if(fclose(f))ok=false;if(ok)ok=rename(temp,path)==0;if(!ok)unlink(temp);return ok;
}
static bool init_sdl(void) {
    if(h.headless)return true;
    const char *driver=getenv("TF_DRIVER"),*present=getenv("TF_PRESENT");
    if(driver && *driver) {
#ifndef __GLIBC__
        fail("TF_DRIVER is a glibc board ABI; musl host refuses foreign driver");return false;
#else
        if(present && strcmp(present,"dispframe")){fail("Unsupported TF_PRESENT (expected dispframe)");return false;}
        if(driver[0]!='/'){fail("TF_DRIVER must be an absolute OS-provided path");return false;}
        h.driver=dlopen(driver,RTLD_NOW|RTLD_LOCAL);
        if(!h.driver){fail("OS board driver dlopen: %s",dlerror());return false;}
#define DRIVER(field,symbol) do {void *p=dlsym(h.driver,symbol);if(!p){fail("OS board driver lacks " symbol);return false;}memcpy(&h.field,&p,sizeof p);} while(0)
        DRIVER(driver_video_init,"video_drivers_init");DRIVER(driver_video_frame,"video_driver_disp_frame");DRIVER(driver_video_close,"video_driver_deinit");
        DRIVER(driver_audio_init,"sound_driver_init");DRIVER(driver_audio_frame,"sound_driver_playframe");DRIVER(driver_audio_close,"sound_driver_deinit");
#undef DRIVER
        if(h.driver_video_init()<=0){fail("OS board video initialization failed");return false;}h.driver_video=true;
        if(h.driver_audio_init(NULL,(int)h.av.timing.sample_rate,2)<0){fail("OS board PCM initialization failed");return false;}h.driver_audio=true;
        /* No SDL display/audio initialization on the board-driver path. */
        if(SDL_Init(SDL_INIT_JOYSTICK|SDL_INIT_TIMER
#if SDL_MAJOR_VERSION >= 2
                    |SDL_INIT_GAMECONTROLLER
#endif
                    )){fail("SDL input initialization: %s",SDL_GetError());return false;}h.sdl=true;
        return true;
#endif
    }
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_JOYSTICK
#if SDL_MAJOR_VERSION >= 2
                |SDL_INIT_GAMECONTROLLER
#endif
                )){fail("SDL initialization: %s",SDL_GetError());return false;}h.sdl=true;
#if SDL_MAJOR_VERSION >= 2
    h.window=SDL_CreateWindow("LilRetro",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,640,480,0);
    if(!h.window){fail("SDL window: %s",SDL_GetError());return false;}
#else
    h.screen=SDL_SetVideoMode(640,480,16,SDL_SWSURFACE);
    if(!h.screen){fail("SDL framebuffer: %s",SDL_GetError());return false;}
#endif
    SDL_AudioSpec want,got;memset(&want,0,sizeof want);want.freq=(int)h.av.timing.sample_rate;want.channels=2;want.format=AUDIO_S16SYS;
    unsigned target=(unsigned)(h.av.timing.sample_rate*h.latency/1000.0/2.0),samples=256;
    while(samples<target && samples<8192)samples*=2;
    want.samples=(Uint16)samples;want.callback=audio_callback;
    if(SDL_OpenAudio(&want,&got)){fail("SDL PCM audio: %s",SDL_GetError());return false;}
    h.audio_open=true;
    if(got.freq!=want.freq || got.format!=want.format || got.channels!=2){fail("PCM device changed format/rate without a resampler");return false;}
    SDL_PauseAudio(0);return true;
}
static bool report(void) {
    FILE *f=h.diagnostic[0]?fopen(h.diagnostic,"wb"):stdout;
    if(!f){fprintf(stderr,"lilretro diagnostic: %s\n",strerror(errno));return false;}
    fprintf(f,"{\n\"schema\":1,\"runtime\":\"lilretro\",\"abi\":");json_string(f,LILRETRO_ABI);
    fprintf(f,",\"ok\":%s,\"error\":",h.error[0]?"false":"true");json_string(f,h.error);
    fprintf(f,",\"core\":{\"name\":");json_string(f,h.info.library_name);fprintf(f,",\"version\":");json_string(f,h.info.library_version);
    fprintf(f,",\"extensions\":");json_string(f,h.info.valid_extensions);
    fprintf(f,",\"need_fullpath\":%s,\"block_extract\":%s,\"supports_no_game\":%s}",h.info.need_fullpath?"true":"false",h.info.block_extract?"true":"false",h.no_game?"true":"false");
    fprintf(f,",\"capabilities\":{\"hardware_render\":false,\"software_formats\":[\"0RGB1555\",\"XRGB8888\",\"RGB565\"],\"analog\":true,\"input_bitmasks\":true,\"core_options_version\":2,\"vfs\":false,\"audio_callback\":false,\"savestate\":%s,\"sram\":true,\"software_framebuffer\":true,\"sdl_major\":%d}",h.loaded&&core.serialize_size&&core.serialize_size()>0?"true":"false",SDL_MAJOR_VERSION);
    fprintf(f,",\"av\":{\"width\":%u,\"height\":%u,\"max_width\":%u,\"max_height\":%u,\"fps\":%.8g,\"sample_rate\":%.8g}",h.av.geometry.base_width,h.av.geometry.base_height,h.av.geometry.max_width,h.av.geometry.max_height,isfinite(h.av.timing.fps)?h.av.timing.fps:0,isfinite(h.av.timing.sample_rate)?h.av.timing.sample_rate:0);
    fprintf(f,",\"sinks\":{\"video\":\"%s\",\"audio\":\"%s\",\"hashes\":\"sampled: video <=1024 bytes on frames 1/60/180; audio <=256 bytes/frame\"}",h.driver_video?"dispframe":(h.headless?"headless":"sdl-software"),h.driver_audio?"board-pcm":(h.headless?"discard":"sdl-pcm"));
    fprintf(f,",\"observed\":{\"frames\":%llu,\"video_frames\":%llu,\"duplicates\":%llu,\"audio_frames\":%llu,\"audio_dropped\":%llu,\"nonzero_audio_samples\":%llu,\"video_hash\":\"%016llx\",\"audio_hash\":\"%016llx\",\"hardware_requested\":%s,\"software_framebuffer_requests\":%u}",(unsigned long long)h.frames,(unsigned long long)h.video_frames,(unsigned long long)h.duplicate_frames,(unsigned long long)h.audio_frames,(unsigned long long)h.audio_dropped,(unsigned long long)h.nonzero_audio,(unsigned long long)h.video_hash,(unsigned long long)h.audio_hash,h.hw_requested?"true":"false",h.software_requests);
    fprintf(f,",\"environment\":[");
    for(unsigned i=0;i<h.command_count;i++)fprintf(f,"%s{\"command\":%u,\"calls\":%u,\"accepted\":%u}",i?",":"",h.commands[i].command,h.commands[i].calls,h.commands[i].accepted);
    fprintf(f,"],\"options\":[");
    for(unsigned i=0;i<h.option_count;i++) {
        struct option *o=&h.options[i];if(i)fputc(',',f);fprintf(f,"{\"key\":");json_string(f,o->key);fprintf(f,",\"description\":");json_string(f,o->description);fprintf(f,",\"value\":");json_string(f,o->value);fprintf(f,",\"choices\":[");
        for(unsigned j=0;j<o->count;j++){if(j)fputc(',',f);json_string(f,o->choices[j]);}fprintf(f,"]}");
    }
    fprintf(f,"]\n}\n");bool ok=!ferror(f);if(f!=stdout && fclose(f))ok=false;return ok;
}
int main(int argc,char **argv) {
    h.width=1024;h.height=768;h.audio_limit=8192;h.latency=64;
    h.format=RETRO_PIXEL_FORMAT_0RGB1555;h.gamepad.fd=-1;
    const char *core_arg=NULL,*content_arg=NULL,*profile_arg=NULL;bool save_state=false,load_state=false;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--profile") && i+1<argc)profile_arg=argv[++i];
        else if(!strcmp(argv[i],"--diagnostics") && i+1<argc) {if(strlen(argv[++i])>=sizeof h.diagnostic)return 2;strcpy(h.diagnostic,argv[i]);}
        else if(!strcmp(argv[i],"--frames") && i+1<argc) {char *end;unsigned long n=strtoul(argv[++i],&end,10);if(!*argv[i]||*end||!n||n>1000000)return 2;h.frame_limit=(unsigned)n;}
        else if(!strcmp(argv[i],"--probe"))h.probe=true;
        else if(!strcmp(argv[i],"--headless"))h.headless=true;
        else if(!strcmp(argv[i],"--save-state"))save_state=true;
        else if(!strcmp(argv[i],"--load-state"))load_state=true;
        else if(argv[i][0]=='-') {fprintf(stderr,"Unknown option: %s\n",argv[i]);return 2;}
        else if(!core_arg)core_arg=argv[i];else if(!content_arg)content_arg=argv[i];else return 2;
    }
    if(!core_arg || !profile_arg){fprintf(stderr,"Usage: lilretro CORE [CONTENT] --profile FILE [--probe --frames N --diagnostics FILE --headless --save-state --load-state]\n");return 2;}
    if(h.probe && !h.frame_limit)h.frame_limit=180;
    signal(SIGINT,stop_signal);signal(SIGTERM,stop_signal);signal(SIGHUP,stop_signal);
    char core_path[PATH_MAX],content_path[PATH_MAX];void *content_data=NULL;
    if(!profile(profile_arg))goto finish;
    if(!realpath(core_arg,core_path)){fail("Cannot resolve core");goto finish;}h.core_path=core_path;
    if(!make_directory(h.system)||!make_directory(h.saves)){fail("Cannot create app-local directories: %s",strerror(errno));goto finish;}
    if(!load_symbols(core_path))goto finish;
    if(core.api_version()!=RETRO_API_VERSION){fail("Incompatible Libretro API");goto finish;}
    core.get_system_info(&h.info);core.set_environment(environment);
    core.set_video_refresh(video);core.set_audio_sample(audio_sample);core.set_audio_sample_batch(audio_batch);core.set_input_poll(poll_input);core.set_input_state(input_state);
    core.init();h.initialized=true;if(h.error[0])goto finish;
    struct retro_game_info game;memset(&game,0,sizeof game);
    if(content_arg) {
        if(!realpath(content_arg,content_path)){fail("Cannot resolve content");goto finish;}game.path=content_path;
        if(!h.info.need_fullpath) {
            FILE *f=fopen(content_path,"rb");struct stat st;
            if(!f || fstat(fileno(f),&st) || st.st_size<0 || (uint64_t)st.st_size>MAX_FILE) {if(f)fclose(f);fail("Content exceeds memory budget or is unavailable");goto finish;}
            game.size=(size_t)st.st_size;content_data=malloc(game.size?game.size:1);
            if(!content_data || fread(content_data,1,game.size,f)!=game.size){fclose(f);fail("Cannot read content");goto finish;}fclose(f);game.data=content_data;
        }
    } else if(!h.no_game){fail("Core requires content");goto finish;}
    if(!core.load_game(content_arg?&game:NULL)){fail("Core rejected content/no-game launch");goto finish;}h.loaded=true;
    core.get_system_av_info(&h.av);
    if(!valid_av(&h.av)){fail("Invalid or over-budget core AV information");goto finish;}
    for(unsigned port=0;port<4;port++)core.set_controller_port_device(port,RETRO_DEVICE_JOYPAD);
    if(!h.probe && (!save_ram(false,RETRO_MEMORY_SAVE_RAM,"sram.bin") || !save_ram(false,RETRO_MEMORY_RTC,"rtc.bin"))){fail("Invalid existing save RAM");goto finish;}
    if(load_state && !state_file(false)){fail("Core save-state restore unsupported or failed");goto finish;}
    if(!init_sdl())goto finish;
    uint64_t next=micros();
    while(!h.stop && !interrupted && (!h.frame_limit || h.frames<h.frame_limit)) {
        /* A core need not call retro_input_poll for the host's exit to work. */
        poll_input();if(h.stop || interrupted)break;
        h.audio_hash_budget=256;core.run();h.frames++;
        if(h.scan_left && --h.scan_left==0 && !report())fail("Could not write first-launch diagnostic JSON");
        if(!h.probe) {
            next+=(uint64_t)(1000000.0/h.av.timing.fps);uint64_t now=micros();
            while(next>now && !h.stop && !interrupted) {
                uint64_t wait=next-now;if(wait>8000)wait=8000;
                struct timespec delay={0,(long)wait*1000};nanosleep(&delay,NULL);
                poll_input();now=micros();
            }
            if(now>next && now-next>250000)next=now;
        }
    }
    if(!h.error[0] && save_state && !state_file(true))fail("Core save-state serialization unsupported or failed");
    if(!h.probe && !h.error[0] && (!save_ram(true,RETRO_MEMORY_SAVE_RAM,"sram.bin") || !save_ram(true,RETRO_MEMORY_RTC,"rtc.bin")))fail("Cannot persist app-local save RAM");
finish:
    if(!report() && !h.error[0])fail("Could not write diagnostic JSON");
    if(h.audio_open)SDL_CloseAudio();
    memset(h.buttons,0,sizeof h.buttons);memset(h.axes,0,sizeof h.axes);
    for(unsigned p=0;p<HOTKEY_SDL_PADS;p++)close_sdl_exit_pad(&h.sdl_hotkeys[p]);
    if(h.joystick)SDL_JoystickClose(h.joystick);
    ls_gp_close(&h.gamepad);
    if(h.loaded)core.unload_game();if(h.initialized)core.deinit();
    if(h.sdl)SDL_Quit();if(core.library)dlclose(core.library);
    if(h.driver_audio)h.driver_audio_close();if(h.driver_video)h.driver_video_close();if(h.driver)dlclose(h.driver);
    free(h.driver_pixels);free(content_data);free(h.framebuffer);return h.error[0]?1:0;
}
