#include "od-sdl.h"
#include "Input.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <chrono>
#include <algorithm>
using namespace lilsmoke;
static_assert(static_cast<int>(InputButton::Start)==OD_START,"Common input button ABI");
static std::unique_ptr<Input> input;
static int width=640,height=480,text_mode;
static unsigned held;
static bool quit_chord,menu_chord;
static double cursor_x=320,cursor_y=240;
static std::chrono::steady_clock::time_point cursor_tick;
extern "C" void od_input_viewport(int w,int h)
{
	if(w<1||h<1||w>1920||h>1080)return;
	width=w;height=h;
	if(input)input->setPointerViewport(w,h,0,0,w,h);
}
extern "C" void od_input_text(int enabled)
{
	text_mode=enabled;
	if(input)input->setTextMode(enabled!=0);
}
extern "C" void od_input_pump(void)
{
	if(!od_enabled())return;
	const char *external=std::getenv("LILSMOKE_OD_INPUT_EXTERNAL");
	if(external&&!std::strcmp(external,"1"))return;
	const char *engine=std::getenv("LILSMOKE_OD_ENGINE");
	bool scumm=engine&&!std::strcmp(engine,"scummvm");
	if(!input) {
		input.reset(new Input);
		input->init(width,height);
		input->setTextMode(text_mode!=0);
		std::fprintf(stderr,"OD_SDL_INPUT common=Input/Keymap wire=%zu viewport=%dx%d\n",
		             sizeof(input_event),width,height);
	}
	for(unsigned n=0;n<128;++n) {
		InputButton button;InputEvent event;PointerEvent pointer;
		bool changed=input->poll(button,event,pointer);
		if(!input->text().empty())od_event_text(input->text().c_str());
		if(!changed)break;
		int b=static_cast<int>(button);
		if(b>OD_NONE&&b<=OD_POWER&&event!=InputEvent::None) {
			bool down=event!=InputEvent::Released;
			if(down)held|=1U<<b;else held&=~(1U<<b);
			bool quit=(held&(1U<<OD_START))&&(held&(1U<<OD_SELECT));
			bool menu=(held&(1U<<OD_START))&&(held&(1U<<OD_X));
			if(quit&&!quit_chord) {
				od_event_button(OD_START,0,0);od_event_button(OD_SELECT,0,0);
				od_event_quit();
			} else if(menu&&!menu_chord) {
				od_event_button(OD_START,0,0);od_event_button(OD_X,0,0);
				od_event_menu();
			} else if(!quit&&!menu&&!(scumm&&b>=OD_UP&&b<=OD_RIGHT))
				od_event_button(b,down,event==InputEvent::Repeat);
			quit_chord=quit;menu_chord=menu;
		}
		if(pointer.type!=PointerEventType::None) {
			cursor_x=pointer.x;cursor_y=pointer.y;
			od_event_pointer(static_cast<int>(pointer.type),static_cast<int>(pointer.button),
			                 pointer.x,pointer.y,pointer.dx,pointer.dy,pointer.wheelY);
		}
	}
	if(scumm) {
		auto now=std::chrono::steady_clock::now();
		double dt=cursor_tick.time_since_epoch().count() ? std::min(0.05,std::chrono::duration<double>(now-cursor_tick).count()) : 0.0;
		cursor_tick=now;
		int dx=((held>>OD_RIGHT)&1)-((held>>OD_LEFT)&1);
		int dy=((held>>OD_DOWN)&1)-((held>>OD_UP)&1);
		int oldx=static_cast<int>(cursor_x),oldy=static_cast<int>(cursor_y);
		cursor_x=std::clamp(cursor_x+dx*320*dt,0.0,static_cast<double>(width-1));
		cursor_y=std::clamp(cursor_y+dy*320*dt,0.0,static_cast<double>(height-1));
		int x=static_cast<int>(cursor_x),y=static_cast<int>(cursor_y);
		if(x!=oldx||y!=oldy)od_event_pointer(1,0,x,y,x-oldx,y-oldy,0);
	}
}
extern "C" void od_input_close(void)
{
	input.reset();held=0;quit_chord=menu_chord=false;od_event_reset();
	cursor_tick={};cursor_x=width/2;cursor_y=height/2;
}
