#define _GNU_SOURCE
#include "od-sdl.h"
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
static void *handles[3];
static int api,enabled=-1,unicode;
int od_api(void)
{
	if(!api) {
		handles[1]=dlopen("libSDL-1.2.so.0",RTLD_LAZY|RTLD_NOLOAD);
		api=handles[1]?1:2;
	}
	return api;
}
void *od_real(const char *name,int version)
{
	if(!version)version=od_api();
	if(version<1||version>2)return NULL;
	if(!handles[version])handles[version]=dlopen(version==1?"libSDL-1.2.so.0":"libSDL2-2.0.so.0",
		RTLD_LAZY|RTLD_LOCAL);
	return handles[version]?dlsym(handles[version],name):NULL;
}
int od_enabled(void)
{
	const char *flag=getenv("LILSMOKE_OD_NATIVE"),*hcfb=getenv("LILSMOKE_HCFB");
	if(!flag||strcmp(flag,"1"))return 0;
#ifdef OD_SDL_QA
	if(getenv("LILSMOKE_OD_QA")&&!strcmp(getenv("LILSMOKE_OD_QA"),"1"))return 1;
#endif
	if(enabled<0) {
		struct fb_fix_screeninfo fix;
		int fd=hcfb&&!strcmp(hcfb,"1")?open("/dev/fb0",O_RDONLY|O_CLOEXEC):-1;
		enabled=fd>=0&&!ioctl(fd,FBIOGET_FSCREENINFO,&fix)&&!memcmp(fix.id,"hcfb",4);
		if(fd>=0)close(fd);
		fprintf(stderr,"OD_SDL_GATE native=%d api=%d verified_hcfb=%d\n",enabled,od_api(),enabled);
	}
	return enabled;
}
void od_error(const char *text)
{
	int (*set)(const char *,...)=od_real("SDL_SetError",0);
	if(set)set("%s",text);
	fprintf(stderr,"OD_SDL_ERROR %s\n",text);
}
#define DISPATCH(fn,args) do { if(od_api()==1)od1_##fn args;else od2_##fn args; } while(0)
void od_event_button(int b,int down,int repeat){DISPATCH(button,(b,down,repeat));}
void od_event_pointer(int t,int b,int x,int y,int dx,int dy,int w){DISPATCH(pointer,(t,b,x,y,dx,dy,w));}
void od_event_text(const char *s){DISPATCH(text,(s));}
void od_event_quit(void){DISPATCH(quit,());}
void od_event_menu(void){DISPATCH(menu,());}
void od_event_reset(void){DISPATCH(reset,());}
int od_event_take(void *out){return od_api()==1?od1_take(out):od2_take(out);}
unsigned char *od_event_keys(int api,int *n){return api==1?od1_keys(n):od2_keys(n);}
unsigned od_event_mouse(int *x,int *y,int relative){return od_api()==1?od1_mouse(x,y,relative):od2_mouse(x,y,relative);}
int od_events_peep(void *p,int n,int a,unsigned min,unsigned max)
{
	return od_api()==1?od1_peep(p,n,a,min,max):od2_peep(p,n,a,min,max);
}
int SDL_Init(unsigned flags)
{
	int (*real)(unsigned)=od_real("SDL_Init",0);
	if(od_enabled()&&(flags&0x10))od_audio_mark_initialized();
	return real?real(od_enabled()?flags&~0x10U:flags):-1;
}
int SDL_InitSubSystem(unsigned flags)
{
	int (*real)(unsigned)=od_real("SDL_InitSubSystem",0);
	if(od_enabled()&&(flags&0x10))od_audio_mark_initialized();
	return real?real(od_enabled()?flags&~0x10U:flags):-1;
}
unsigned SDL_WasInit(unsigned flags)
{
	unsigned (*real)(unsigned)=od_real("SDL_WasInit",0);
	unsigned result=real?real(flags):0;
	if(od_enabled()&&od_audio_initialized()&&(!flags||(flags&0x10)))result|=0x10;
	return result;
}
void SDL_QuitSubSystem(unsigned flags)
{
	void (*real)(unsigned)=od_real("SDL_QuitSubSystem",0);
	if(od_enabled()&&(flags&0x10)){od_audio_shutdown();flags&=~0x10U;}
	if(real)real(flags);
}
void SDL_Quit(void)
{
	void (*real)(void)=dlsym(RTLD_NEXT,"SDL_Quit");
	if(od_enabled()){od_audio_shutdown();od_input_close();od_video_shutdown();}
	if(real)real();
}
void SDL_PumpEvents(void)
{
	void (*real)(void)=od_real("SDL_PumpEvents",0);
	if(od_enabled())od_input_pump();
	if(real)real();
}
int SDL_PollEvent(void *event)
{
	int (*real)(void *)=od_real("SDL_PollEvent",0);
	if(od_enabled()) {
		od_input_pump();
		if(od_event_take(event))return 1;
	}
	if(!real)return 0;
	/* The common evdev path owns pointer input. Discard DirectFB's duplicate
	 * mouse stream, including gamepad-as-mouse events, but keep window/quit. */
	if(od_enabled()&&event) {
		for(unsigned n=0;n<128;++n) {
			int result=real(event);
			if(!result)return 0;
			if(!(od_api()==1?od1_backend_pointer(event):od2_backend_pointer(event)))return result;
		}
		return 0;
	}
	return real(event);
}
static int wait_event(void *event,int timeout)
{
	struct timespec start,now,delay={0,2000000};
	clock_gettime(CLOCK_MONOTONIC,&start);
	for(;;) {
		if(SDL_PollEvent(event))return 1;
		if(!timeout)return 0;
		if(timeout>0) {
			clock_gettime(CLOCK_MONOTONIC,&now);
			int64_t ms=(now.tv_sec-start.tv_sec)*1000LL+(now.tv_nsec-start.tv_nsec)/1000000;
			if(ms>=timeout)return 0;
		}
		nanosleep(&delay,NULL);
	}
}
int SDL_WaitEvent(void *event)
{
	int (*real)(void *)=od_real("SDL_WaitEvent",0);
	return od_enabled()?wait_event(event,-1):(real?real(event):0);
}
int SDL_WaitEventTimeout(void *event,int timeout)
{
	int (*real)(void *,int)=od_real("SDL_WaitEventTimeout",2);
	return od_enabled()?wait_event(event,timeout):(real?real(event,timeout):0);
}
int SDL_PeepEvents(void *events,int n,int action,unsigned minimum,...)
{
	unsigned maximum=minimum;
	if(od_api()==2){va_list args;va_start(args,minimum);maximum=va_arg(args,unsigned);va_end(args);}
	if(od_enabled()) {
		int count;
		od_input_pump();count=od_events_peep(events,n,action,minimum,maximum);
		if(count||action==0)return count;
	}
	if(od_api()==1) {
		int (*real)(void *,int,int,unsigned)=od_real("SDL_PeepEvents",1);
		return real?real(events,n,action,minimum):-1;
	} else {
		int (*real)(void *,int,int,unsigned,unsigned)=od_real("SDL_PeepEvents",2);
		return real?real(events,n,action,minimum,maximum):-1;
	}
}
const unsigned char *SDL_GetKeyboardState(int *n)
{
	const unsigned char *(*real)(int *)=od_real("SDL_GetKeyboardState",2);
	if(od_enabled()){od_input_pump();return od_event_keys(2,n);}
	return real?real(n):NULL;
}
unsigned char *SDL_GetKeyState(int *n)
{
	unsigned char *(*real)(int *)=od_real("SDL_GetKeyState",1);
	if(od_enabled()){od_input_pump();return od_event_keys(1,n);}
	return real?real(n):NULL;
}
unsigned SDL_GetModState(void)
{
	unsigned (*real)(void)=od_real("SDL_GetModState",0);
	if(od_enabled())return od_api()==1?od1_modifiers():od2_modifiers();
	return real?real():0;
}
unsigned SDL_GetMouseState(int *x,int *y)
{
	unsigned (*real)(int *,int *)=od_real("SDL_GetMouseState",0);
	if(od_enabled()){od_input_pump();return od_event_mouse(x,y,0);}
	return real?real(x,y):0;
}
unsigned SDL_GetRelativeMouseState(int *x,int *y)
{
	unsigned (*real)(int *,int *)=od_real("SDL_GetRelativeMouseState",0);
	if(od_enabled()){od_input_pump();return od_event_mouse(x,y,1);}
	return real?real(x,y):0;
}
int SDL_EnableUNICODE(int enable)
{
	int (*real)(int)=od_real("SDL_EnableUNICODE",1);
	if(od_enabled()){int old=unicode;if(enable>=0){unicode=enable!=0;od_input_text(unicode);}return old;}
	return real?real(enable):0;
}
void SDL_StartTextInput(void)
{
	void (*real)(void)=od_real("SDL_StartTextInput",2);
	if(od_enabled())od_input_text(1);else if(real)real();
}
void SDL_StopTextInput(void)
{
	void (*real)(void)=od_real("SDL_StopTextInput",2);
	if(od_enabled())od_input_text(0);else if(real)real();
}
int SDL_RenderSetLogicalSize(void *renderer,int w,int h)
{
	int (*real)(void *,int,int)=od_real("SDL_RenderSetLogicalSize",2);
	if(od_video_logical(renderer,w,h)<0)return -1;
	int result=real?real(renderer,w,h):-1;
	if(!result&&od_enabled())od_input_viewport(w,h);
	return result;
}

int SDL_ShowCursor(int toggle)
{
	int (*real)(int)=od_real("SDL_ShowCursor",0);
	if(od_enabled())return real?real(0):0;
	return real?real(toggle):0;
}
