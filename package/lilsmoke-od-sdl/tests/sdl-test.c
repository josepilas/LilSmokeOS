#include "od-sdl.h"
#include "input-wire.h"
#include <SDL.h>
#include <assert.h>
#include <dlfcn.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static atomic_uint callbacks;
void qa_pcm_reset(void);
void qa_pcm_fail(int);
unsigned qa_pcm_packets(void);
unsigned qa_pcm_nonzero(void);
static void callback(void *userdata,unsigned char *buffer,int bytes)
{
    assert(userdata==(void *)0x1234&&bytes==256);
    memset(buffer,0x91,(size_t)bytes);atomic_fetch_add(&callbacks,1);
}
static void wait_packets(unsigned minimum)
{
    for(int i=0;i<200&&qa_pcm_packets()<minimum;++i)SDL_Delay(5);
    assert(qa_pcm_packets()>=minimum);
}
int main(int argc,char **argv)
{
    SDL_AudioSpec spec,got;
    SDL_Event event;
    assert(sizeof(struct input_event)==16);
    assert(argc==2);
    if(!strcmp(argv[1],"generic")) {
        assert(!od_enabled());
        assert(SDL_Init(SDL_INIT_AUDIO)==0);
        assert(SDL_WasInit(SDL_INIT_AUDIO)&SDL_INIT_AUDIO);
        memset(&spec,0,sizeof(spec));spec.freq=44100;spec.format=AUDIO_S16SYS;
        spec.channels=2;spec.samples=64;spec.callback=callback;spec.userdata=(void *)0x1234;
        assert(SDL_OpenAudio(&spec,&got)==0);
        SDL_CloseAudio();SDL_Quit();
        puts("PASS generic SDL passthrough and hardware gate");return 0;
    }
    assert(od_enabled());
    assert(SDL_Init(SDL_INIT_AUDIO|SDL_INIT_VIDEO)==0);
    assert(SDL_WasInit(SDL_INIT_AUDIO)&SDL_INIT_AUDIO);
    qa_pcm_reset();
    memset(&spec,0,sizeof(spec));spec.freq=22050;spec.format=AUDIO_U8;
    spec.channels=1;spec.samples=256;spec.callback=callback;spec.userdata=(void *)0x1234;
    assert(SDL_OpenAudio(&spec,&got)==0);
    assert(got.freq==22050&&got.channels==1&&got.size==256);
    assert(SDL_GetAudioStatus()==SDL_AUDIO_PAUSED);
    assert(SDL_OpenAudio(&spec,NULL)==-1);
    SDL_Delay(30);assert(atomic_load(&callbacks)==0);
    SDL_PauseAudio(0);wait_packets(3);
    SDL_LockAudio();unsigned before=atomic_load(&callbacks);SDL_Delay(30);
    assert(atomic_load(&callbacks)==before);SDL_UnlockAudio();
    SDL_PauseAudio(1);assert(SDL_GetAudioStatus()==SDL_AUDIO_PAUSED);
    assert(qa_pcm_nonzero()>0);
    SDL_CloseAudio();assert(SDL_GetAudioStatus()==SDL_AUDIO_STOPPED);
    qa_pcm_fail(1);assert(SDL_OpenAudio(&spec,NULL)==-1);qa_pcm_reset();
#if OD_SDL_API == 2
    spec.freq=44100;spec.format=AUDIO_S16SYS;spec.channels=2;spec.samples=128;spec.callback=NULL;
    SDL_AudioDeviceID id=SDL_OpenAudioDevice(NULL,0,&spec,&got,0);
    assert(id==1);
    unsigned char data[2048];memset(data,0x5a,sizeof(data));
    assert(SDL_QueueAudio(id,data,3)==-1);
    assert(SDL_QueueAudio(9,data,sizeof(data))==-1);
    assert(SDL_QueueAudio(id,data,50000)==-1);
    assert(SDL_QueueAudio(id,data,sizeof(data))==0);
    assert(SDL_GetQueuedAudioSize(id)==sizeof(data));
    SDL_ClearQueuedAudio(id);assert(SDL_GetQueuedAudioSize(id)==0);
    assert(SDL_QueueAudio(id,data,sizeof(data))==0);
    SDL_PauseAudioDevice(id,0);wait_packets(3);
    for(int i=0;i<100&&SDL_GetQueuedAudioSize(id);++i)SDL_Delay(5);
    assert(SDL_GetQueuedAudioSize(id)==0);
    qa_pcm_fail(0);SDL_Delay(50);assert(SDL_GetAudioDeviceStatus(id)==SDL_AUDIO_STOPPED);
    assert(SDL_QueueAudio(id,data,sizeof(data))==-1);
    SDL_CloseAudioDevice(id);
#else
    char driver[32];assert(SDL_AudioInit(NULL)==0);
    assert(SDL_AudioDriverName(driver,sizeof(driver))==driver&&!strcmp(driver,"m8-native"));
#endif
    setenv("LILSMOKE_OD_ENGINE","scummvm",1);od_event_reset();od_event_menu();
    assert(SDL_PollEvent(&event)&&event.type==SDL_KEYDOWN&&event.key.keysym.sym==SDLK_LCTRL);
    assert(SDL_PollEvent(&event)&&event.key.keysym.sym==SDLK_F5&&(event.key.keysym.mod&KMOD_LCTRL));
    while(SDL_PollEvent(&event)){}
    od_event_pointer(2,1,10,20,0,0,0);
    assert(SDL_PollEvent(&event)&&event.type==SDL_MOUSEBUTTONDOWN&&event.button.x==10);
    int x,y;assert(SDL_GetMouseState(&x,&y)&SDL_BUTTON(SDL_BUTTON_LEFT));assert(x==10&&y==20);
    setenv("LILSMOKE_OD_ENGINE","easyrpg",1);od_event_reset();od_event_button(OD_A,1,0);
    assert(SDL_PollEvent(&event)&&event.key.keysym.sym==SDLK_RETURN);
    od_event_quit();assert(SDL_PollEvent(&event)&&event.type==SDL_QUIT);
    setenv("LILSMOKE_OD_ENGINE","race",1);od_event_reset();od_event_button(OD_B,1,0);
    assert(SDL_PollEvent(&event)&&event.key.keysym.sym==SDLK_LALT);
    od_event_button(OD_X,1,0);assert(SDL_PollEvent(&event)&&event.key.keysym.sym==SDLK_SPACE);
    od_event_button(OD_Y,1,0);assert(SDL_PollEvent(&event)&&event.key.keysym.sym==SDLK_LSHIFT);
    od_event_button(OD_L1,1,0);assert(SDL_PollEvent(&event)&&event.key.keysym.sym==SDLK_TAB);
    od_event_button(OD_R1,1,0);assert(SDL_PollEvent(&event)&&event.key.keysym.sym==SDLK_BACKSPACE);
    setenv("LILSMOKE_OD_ENGINE","independent",1);od_event_reset();od_event_button(OD_A,1,0);
    assert(SDL_PollEvent(&event)&&event.key.keysym.sym==SDLK_LCTRL);
    od_event_button(OD_B,1,0);assert(SDL_PollEvent(&event)&&event.key.keysym.sym==SDLK_LALT);
    od_event_button(OD_X,1,0);assert(SDL_PollEvent(&event)&&event.key.keysym.sym==SDLK_SPACE);
    od_event_button(OD_Y,1,0);assert(SDL_PollEvent(&event)&&event.key.keysym.sym==SDLK_LSHIFT);
    od_event_menu();assert(SDL_PollEvent(&event)&&event.key.keysym.sym==SDLK_ESCAPE);
    while(SDL_PollEvent(&event)){}
    setenv("LILSMOKE_OD_ENGINE","mgba",1);od_event_reset();od_event_button(OD_A,1,0);
    assert(SDL_PollEvent(&event)&&event.key.keysym.sym==SDLK_x);
    od_event_button(OD_B,1,0);assert(SDL_PollEvent(&event)&&event.key.keysym.sym==SDLK_z);
    od_event_menu();assert(SDL_PollEvent(&event)&&event.key.keysym.sym==SDLK_ESCAPE);
    while(SDL_PollEvent(&event)){}
    setenv("LILSMOKE_OD_ENGINE","regba",1);od_event_reset();od_event_menu();
    assert(SDL_PollEvent(&event)&&event.key.keysym.sym==SDLK_END);
    while(SDL_PollEvent(&event)){}
    setenv("LILSMOKE_OD_ENGINE","fceu320",1);od_event_reset();od_event_menu();
    assert(SDL_PollEvent(&event)&&event.key.keysym.sym==SDLK_END);
#if OD_SDL_API == 1
    assert(SDL_GetKeyState(NULL)[SDLK_END]);
    SDL_Delay(180);assert(!SDL_GetKeyState(NULL)[SDLK_END]);
#else
    assert(SDL_GetKeyboardState(NULL)[SDL_SCANCODE_END]);
    SDL_Delay(180);assert(!SDL_GetKeyboardState(NULL)[SDL_SCANCODE_END]);
#endif
    while(SDL_PollEvent(&event)){}
    setenv("LILSMOKE_NATIVE_PICKER","1",1);od_event_button(OD_A,1,0);
    assert(SDL_PollEvent(&event)&&event.key.keysym.sym==SDLK_RETURN);
    unsetenv("LILSMOKE_NATIVE_PICKER");
    setenv("LILSMOKE_OD_ENGINE","picodrive",1);od_event_reset();od_event_button(OD_X,1,0);
    assert(SDL_PollEvent(&event)&&event.key.keysym.sym==SDLK_c);
    od_event_menu();assert(SDL_PollEvent(&event)&&event.key.keysym.sym==SDLK_ESCAPE);
    while(SDL_PollEvent(&event)){}
    SDL_ShowCursor(SDL_ENABLE);
    assert(SDL_ShowCursor(SDL_QUERY)==SDL_DISABLE);
    memset(&event,0,sizeof(event));event.type=SDL_MOUSEMOTION;
    SDL_PushEvent(&event);
    memset(&event,0,sizeof(event));event.type=SDL_QUIT;
    SDL_PushEvent(&event);
    assert(SDL_PollEvent(&event)&&event.type==SDL_QUIT);
    SDL_Quit();
    printf("PASS SDL%d callback/resampling/locking/events/PCM failure%s\n",OD_SDL_API,
        OD_SDL_API==2?"/bounded queued audio":"/public SDL1.2 audio");
    return 0;
}
