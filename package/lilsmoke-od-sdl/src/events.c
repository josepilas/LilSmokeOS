#include "od-sdl.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if OD_SDL_API == 1
#define FN_(x) od1_##x
#define KEYCOUNT SDLK_LAST
typedef SDLKey key_type;
#else
#define FN_(x) od2_##x
#define KEYCOUNT SDL_NUM_SCANCODES
typedef SDL_Keycode key_type;
#endif
#define FN(x) FN_(x)
static SDL_Event queue[256];
static unsigned head,count,buttons;
static unsigned char keys[KEYCOUNT];
static unsigned modifiers;
static key_type menu_key;
static unsigned long long menu_until;
static int mouse_x,mouse_y,relative_x,relative_y;
int FN(backend_pointer)(void *opaque)
{
    const SDL_Event *e=opaque;
    if(!e)return 0;
    return e->type==SDL_MOUSEMOTION||e->type==SDL_MOUSEBUTTONDOWN||e->type==SDL_MOUSEBUTTONUP
#if OD_SDL_API == 2
        ||e->type==SDL_MOUSEWHEEL
#endif
        ;
}
static void enqueue(const SDL_Event *event)
{
	if(count&&event->type==SDL_MOUSEMOTION&&
	   queue[(head+count-1)%256].type==SDL_MOUSEMOTION) {
		SDL_Event *old=&queue[(head+count-1)%256];
		int dx=old->motion.xrel+event->motion.xrel,dy=old->motion.yrel+event->motion.yrel;
		*old=*event;old->motion.xrel=dx;old->motion.yrel=dy;return;
	}
	if(count==256){fprintf(stderr,"OD_SDL_INPUT bounded event queue full\n");return;}
	queue[(head+count++)%256]=*event;
}
static void key(key_type symbol,int down,int repeat)
{
	SDL_Event event;
	int index=(int)symbol;
	memset(&event,0,sizeof(event));event.type=down?SDL_KEYDOWN:SDL_KEYUP;
	event.key.state=down?SDL_PRESSED:SDL_RELEASED;event.key.keysym.sym=symbol;
	unsigned mask=symbol==SDLK_LCTRL?KMOD_LCTRL:symbol==SDLK_LSHIFT?KMOD_LSHIFT:
		symbol==SDLK_LALT?KMOD_LALT:0;
	if(down)modifiers|=mask;else modifiers&=~mask;
	event.key.keysym.mod=modifiers;
#if OD_SDL_API == 2
	{
		SDL_Scancode (*scan)(SDL_Keycode)=od_real("SDL_GetScancodeFromKey",2);
		if(scan)event.key.keysym.scancode=scan(symbol);
		index=event.key.keysym.scancode;event.key.repeat=repeat!=0;
	}
#else
	(void)repeat;
#endif
	if(index>=0&&index<KEYCOUNT)keys[index]=(unsigned char)(down!=0);
	enqueue(&event);
}
static unsigned long long milliseconds(void)
{
	struct timespec now;
	if(clock_gettime(CLOCK_MONOTONIC,&now))return 0;
	return (unsigned long long)now.tv_sec*1000+now.tv_nsec/1000000;
}
static void release_menu(void)
{
	if(menu_until&&milliseconds()>=menu_until) {
		key(menu_key,0,0);menu_until=0;
	}
}
static void menu_pulse(key_type symbol)
{
	if(menu_until)key(menu_key,0,0);
	menu_key=symbol;menu_until=milliseconds()+150;
	key(symbol,1,0);
}
static key_type binding(int button)
{
	const char *engine=getenv("LILSMOKE_OD_ENGINE");
	int nx=engine&&!strcmp(engine,"nxengine");
	int rpg=engine&&!strcmp(engine,"easyrpg");
	int pico=engine&&!strcmp(engine,"picodrive"),gba=engine&&!strcmp(engine,"mgba");
	if(getenv("LILSMOKE_NATIVE_PICKER")) {
		if(button==OD_A)return SDLK_RETURN;
		if(button==OD_B)return SDLK_ESCAPE;
	}
	int dingux=engine&&(!strcmp(engine,"fceu320")||!strcmp(engine,"regba")||
		!strcmp(engine,"gnuboy")||!strcmp(engine,"temper")||!strcmp(engine,"handy")||
		!strcmp(engine,"stella")||!strcmp(engine,"prosystem")||!strcmp(engine,"atari800")||
		!strcmp(engine,"race")||!strcmp(engine,"oswan")||!strcmp(engine,"pokemini")||
		!strcmp(engine,"mame4all")||!strcmp(engine,"openbor")||!strcmp(engine,"independent"));
	switch(button) {
		case OD_UP:return SDLK_UP;case OD_DOWN:return SDLK_DOWN;
		case OD_LEFT:return SDLK_LEFT;case OD_RIGHT:return SDLK_RIGHT;
		case OD_A:return gba?SDLK_x:pico?SDLK_z:nx?SDLK_z:rpg?SDLK_RETURN:SDLK_LCTRL;
		case OD_B:return gba?SDLK_z:pico?SDLK_x:nx?SDLK_x:rpg?SDLK_ESCAPE:dingux?SDLK_LALT:SDLK_SPACE;
		case OD_X:return pico?SDLK_c:nx?SDLK_a:dingux?SDLK_SPACE:SDLK_LSHIFT;
		case OD_Y:return pico?SDLK_a:nx?SDLK_s:dingux?SDLK_LSHIFT:SDLK_LALT;
		case OD_L1:return gba?SDLK_a:pico?SDLK_s:nx?SDLK_q:dingux?SDLK_TAB:SDLK_COMMA;
		case OD_R1:return gba?SDLK_s:pico?SDLK_d:nx?SDLK_w:dingux?SDLK_BACKSPACE:SDLK_PERIOD;
		case OD_L2:return SDLK_PAGEUP;case OD_R2:return SDLK_PAGEDOWN;
		case OD_SELECT:return gba?SDLK_BACKSPACE:SDLK_ESCAPE;
		case OD_HOME:return SDLK_ESCAPE;
		case OD_START:return SDLK_RETURN;
		default:return SDLK_UNKNOWN;
	}
}
int FN(button)(int button,int down,int repeat)
{
	const char *engine=getenv("LILSMOKE_OD_ENGINE");
	if(engine&&!strcmp(engine,"scummvm")&&(button==OD_A||button==OD_B)) {
		FN(pointer)(down?2:3,button==OD_A?1:2,mouse_x,mouse_y,0,0,0);return 1;
	}
	{
		key_type symbol=binding(button);
		if(symbol==SDLK_UNKNOWN)return 0;
		key(symbol,down,repeat);return 1;
	}
}
void FN(pointer)(int type,int button,int x,int y,int dx,int dy,int wheel)
{
	SDL_Event event;
	int physical=button==1?SDL_BUTTON_LEFT:button==2?SDL_BUTTON_RIGHT:SDL_BUTTON_MIDDLE;
	memset(&event,0,sizeof(event));
	mouse_x=x;mouse_y=y;relative_x+=dx;relative_y+=dy;
	if(type==1) {
		event.type=SDL_MOUSEMOTION;event.motion.x=x;event.motion.y=y;
		event.motion.xrel=dx;event.motion.yrel=dy;event.motion.state=buttons;enqueue(&event);
	} else if(type==2||type==3) {
		unsigned mask=SDL_BUTTON(physical);
		if(type==2)buttons|=mask;else buttons&=~mask;
		event.type=type==2?SDL_MOUSEBUTTONDOWN:SDL_MOUSEBUTTONUP;
		event.button.button=(Uint8)physical;event.button.state=type==2?SDL_PRESSED:SDL_RELEASED;
		event.button.x=x;event.button.y=y;enqueue(&event);
	} else if(type==4&&wheel) {
#if OD_SDL_API == 2
		event.type=SDL_MOUSEWHEEL;event.wheel.y=wheel;enqueue(&event);
#else
		int n=wheel<0?-wheel:wheel;if(n>16)n=16;
		event.button.x=x;event.button.y=y;event.button.button=wheel>0?4:5;
		while(n--) {
			event.type=SDL_MOUSEBUTTONDOWN;event.button.state=SDL_PRESSED;enqueue(&event);
			event.type=SDL_MOUSEBUTTONUP;event.button.state=SDL_RELEASED;enqueue(&event);
		}
#endif
	}
}
void FN(text)(const char *text)
{
#if OD_SDL_API == 2
	SDL_Event event;memset(&event,0,sizeof(event));event.type=SDL_TEXTINPUT;
	snprintf(event.text.text,sizeof(event.text.text),"%s",text);enqueue(&event);
#else
	for(;*text;++text) {
		SDL_Event event;memset(&event,0,sizeof(event));event.type=SDL_KEYDOWN;
		event.key.state=SDL_PRESSED;event.key.keysym.sym=(SDLKey)(unsigned char)*text;
		event.key.keysym.unicode=(Uint16)(unsigned char)*text;enqueue(&event);
		event.type=SDL_KEYUP;event.key.state=SDL_RELEASED;enqueue(&event);
	}
#endif
}
void FN(quit)(void)
{
	SDL_Event event;memset(&event,0,sizeof(event));event.type=SDL_QUIT;enqueue(&event);
}
void FN(menu)(void)
{
	const char *engine=getenv("LILSMOKE_OD_ENGINE");
	if(engine&&!strcmp(engine,"scummvm")) {
		key(SDLK_LCTRL,1,0);key(SDLK_F5,1,0);key(SDLK_F5,0,0);key(SDLK_LCTRL,0,0);
	} else {
		if(engine&&!strcmp(engine,"mame4all")) {menu_pulse(SDLK_TAB);return;}
		if(engine&&!strcmp(engine,"fceu320")) {menu_pulse(SDLK_END);return;}
		if(engine&&!strcmp(engine,"regba")) {menu_pulse(SDLK_END);return;}
		key_type symbol=engine&&(!strcmp(engine,"nxengine")||!strcmp(engine,"prboom")||
			!strcmp(engine,"easyrpg")||!strcmp(engine,"openbor")||!strcmp(engine,"picodrive")||
			!strcmp(engine,"mgba")||!strcmp(engine,"handy")||!strcmp(engine,"temper")||
			!strcmp(engine,"gnuboy")||!strcmp(engine,"stella")||!strcmp(engine,"prosystem")||
			!strcmp(engine,"atari800")||!strcmp(engine,"race")||!strcmp(engine,"oswan")||
			!strcmp(engine,"pokemini")||!strcmp(engine,"independent"))?SDLK_ESCAPE:SDLK_PAUSE;
		menu_pulse(symbol);
	}
}
int FN(take)(void *out)
{
	release_menu();
	if(!count)return 0;
	if(out){memcpy(out,&queue[head],sizeof(SDL_Event));head=(head+1)%256;--count;}
	return 1;
}
void FN(reset)(void)
{
	head=count=buttons=modifiers=0;relative_x=relative_y=0;memset(keys,0,sizeof(keys));
	menu_until=0;
}
unsigned FN(modifiers)(void){return modifiers;}
unsigned char *FN(keys)(int *n)
{
	release_menu();
	if(n)*n=KEYCOUNT;
	return keys;
}
unsigned FN(mouse)(int *x,int *y,int relative)
{
	if(x)*x=relative?relative_x:mouse_x;
	if(y)*y=relative?relative_y:mouse_y;
	if(relative)relative_x=relative_y=0;
	return buttons;
}
int FN(peep)(void *out,int n,int action,unsigned minimum,unsigned maximum)
{
	release_menu();
	int copied=0;
	if(n<0||n>256)return -1;
	if(action==SDL_ADDEVENT) {
		const SDL_Event *events=out;
		if(!out)return -1;
		while(copied<n&&count<256)enqueue(&events[copied++]);
		return copied;
	}
	if(action!=SDL_PEEKEVENT&&action!=SDL_GETEVENT)return -1;
	for(unsigned i=0;i<count&&copied<n;) {
		SDL_Event *event=&queue[(head+i)%256];
#if OD_SDL_API == 1
		int match=event->type<32&&(minimum&(1U<<event->type));(void)maximum;
#else
		int match=event->type>=minimum&&event->type<=maximum;
#endif
		if(!match){++i;continue;}
		if(out)memcpy((SDL_Event *)out+copied,event,sizeof(*event));
		++copied;
		if(action==SDL_GETEVENT) {
			for(unsigned k=i+1;k<count;++k)queue[(head+k-1)%256]=queue[(head+k)%256];
			--count;
		} else ++i;
	}
	return copied;
}
