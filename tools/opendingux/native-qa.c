/* Original disposable QA app. No game assets, GPU calls or SDL extensions. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static FILE *journal;
static unsigned events;
static int dimension(const char *key,int fallback,int low,int high)
{
	const char *text=getenv(key);
	char *end;
	long value;
	if(!text||!*text)return fallback;
	value=strtol(text,&end,10);
	return !*end&&value>=low&&value<=high?(int)value:fallback;
}
static void log_event(const char *what,int a,int b)
{
	if(events++>=64)return;
	fprintf(stdout,"OPK_QA_%s %d %d\n",what,a,b);fflush(stdout);
	if(journal){fprintf(journal,"OPK_QA_%s %d %d\n",what,a,b);fflush(journal);}
}
int main(void)
{
	SDL_Window *window=NULL;
	SDL_Renderer *renderer=NULL;
	SDL_Joystick *joystick[2]={NULL,NULL};
	SDL_Event e;
	SDL_DisplayMode display;
	unsigned begin,now;
	int alive=1,start=0,select=0,x=1,y=1,n=0,i,code=0,width=854,height=480;
	char path[1024];
	const char *home=getenv("HOME");
	if(home&&snprintf(path,sizeof(path),"%s/opk-qa.log",home)<(int)sizeof(path))
		journal=fopen(path,"a");
	log_event("START",(int)getpid(),0);
	if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_JOYSTICK|SDL_INIT_GAMECONTROLLER|SDL_INIT_EVENTS))goto fail;
	if(getenv("LILSMOKE_HCFB")&&!strcmp(getenv("LILSMOKE_HCFB"),"1")){width=1280;height=720;}
	if(!SDL_GetCurrentDisplayMode(0,&display)&&display.w>=320&&display.w<=1920&&
	   display.h>=240&&display.h<=1080){width=display.w;height=display.h;}
	width=dimension("LILSMOKE_SCREEN_WIDTH",width,320,1920);
	height=dimension("LILSMOKE_SCREEN_HEIGHT",height,240,1080);
	window=SDL_CreateWindow("Native OPK QA",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,
		width,height,SDL_WINDOW_FULLSCREEN);
	if(!window)goto fail;
	renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
	if(!renderer)goto fail;
	SDL_RenderSetLogicalSize(renderer,320,240);
	n=SDL_NumJoysticks();if(n>2)n=2;
	for(i=0;i<n;++i)joystick[i]=SDL_JoystickOpen(i);
	log_event("READY",width,height);
	begin=SDL_GetTicks();
	while(alive) {
		while(SDL_PollEvent(&e)) {
			if(e.type==SDL_QUIT)alive=0;
			else if(e.type==SDL_KEYDOWN||e.type==SDL_KEYUP) {
				int down=e.type==SDL_KEYDOWN;
				SDL_Keycode key=e.key.keysym.sym;
				log_event("KEY",(int)key,down);
				if(key==SDLK_RETURN)start=down;
				if(key==SDLK_RSHIFT||key==SDLK_BACKSPACE)select=down;
				if(down) {
					if(key==SDLK_ESCAPE)alive=0;
					if(key==SDLK_LEFT&&x>0)--x;
					if(key==SDLK_RIGHT&&x<3)++x;
					if(key==SDLK_UP&&y>0)--y;
					if(key==SDLK_DOWN&&y<2)++y;
				}
			} else if(e.type==SDL_JOYBUTTONDOWN||e.type==SDL_JOYBUTTONUP) {
				int down=e.type==SDL_JOYBUTTONDOWN;
				log_event("JOY",e.jbutton.button,down);
				if(e.jbutton.button==9||e.jbutton.button==7)start=down;
				if(e.jbutton.button==8||e.jbutton.button==6)select=down;
			} else if(e.type==SDL_CONTROLLERBUTTONDOWN||e.type==SDL_CONTROLLERBUTTONUP) {
				int down=e.type==SDL_CONTROLLERBUTTONDOWN;
				log_event("PAD",e.cbutton.button,down);
				if(e.cbutton.button==SDL_CONTROLLER_BUTTON_START)start=down;
				if(e.cbutton.button==SDL_CONTROLLER_BUTTON_BACK)select=down;
			}
		}
		if(start&&select){log_event("CHORD",1,1);alive=0;}
		now=SDL_GetTicks();
		if(now-begin>45000){log_event("TIMEOUT",45,0);alive=0;}
		SDL_SetRenderDrawColor(renderer,18,24,22,255);SDL_RenderClear(renderer);
		for(i=0;i<12;++i) {
			SDL_Rect tile={12+(i%4)*76,32+(i/4)*60,68,52};
			SDL_SetRenderDrawColor(renderer,i%3==0?220:40,i%3==1?210:60,i%3==2?230:80,255);
			SDL_RenderFillRect(renderer,&tile);
		}
		{
			SDL_Rect selected={10+x*76,30+y*60,72,56};
			SDL_Rect progress={12,220,(int)(((now-begin)/100)%297),8};
			SDL_SetRenderDrawColor(renderer,255,255,255,255);
			SDL_RenderDrawRect(renderer,&selected);SDL_RenderFillRect(renderer,&progress);
		}
		SDL_RenderPresent(renderer);SDL_Delay(25);
	}
	goto done;
fail:
	fprintf(stderr,"OPK_QA_ERROR %s\n",SDL_GetError());
	if(journal)fprintf(journal,"OPK_QA_ERROR %s\n",SDL_GetError());
	code=1;
done:
	for(i=0;i<n;++i)if(joystick[i])SDL_JoystickClose(joystick[i]);
	if(renderer)SDL_DestroyRenderer(renderer);
	if(window)SDL_DestroyWindow(window);
	SDL_Quit();log_event("EXIT",code,0);
	if(journal)fclose(journal);
	return code;
}
