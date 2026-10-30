/* SPDX-License-Identifier: AGPL-3.0-only */
#include "port.h"
#include "gamepad-state.h"
#include <dirent.h>
#include <errno.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>

static struct ls_gp_reader transport = LS_GP_READER_INIT;
static SDL_GameController* controllers[8];
static bool keyboard[256];
static FtPad current;
static const volatile uint32_t* shared;
static unsigned next_scan;
static struct { int fd; unsigned node; bool down[256]; bool dropped; } devices[16];

/* Read the stock 4.4 o32 evdev wire ABI, not musl's 64-bit-time timeval. */
#if defined(__mips__) && __SIZEOF_POINTER__ == 4
typedef struct { int32_t sec, usec; uint16_t type, code; int32_t value; } FtEvent;
typedef char ft_event_size_check[sizeof(FtEvent) == 16 ? 1 : -1];
#else
typedef struct input_event FtEvent;
#endif

static int sdl_key(SDL_Keycode key) {
    if (key >= SDLK_a && key <= SDLK_z) return 'A' + key - SDLK_a;
    if (key >= SDLK_0 && key <= SDLK_9) return key;
    switch (key) {
    case SDLK_UP: return VK_UP; case SDLK_DOWN: return VK_DOWN;
    case SDLK_LEFT: return VK_LEFT; case SDLK_RIGHT: return VK_RIGHT;
    case SDLK_RETURN: return VK_ENTER; case SDLK_SPACE: return VK_SPACE;
    case SDLK_LSHIFT: case SDLK_RSHIFT: return VK_SHIFT;
    case SDLK_LCTRL: case SDLK_RCTRL: return VK_CONTROL;
    case SDLK_ESCAPE: return VK_ESCAPE;
    default: return 0;
    }
}

static int evdev_key(unsigned code) {
    switch(code) {
    case KEY_UP: return VK_UP; case KEY_DOWN: return VK_DOWN;
    case KEY_LEFT: return VK_LEFT; case KEY_RIGHT: return VK_RIGHT;
    case KEY_ENTER: case BTN_START: return VK_ENTER;
    case KEY_SPACE: case BTN_SOUTH: return 'Z';
    case KEY_LEFTALT: case BTN_EAST: return 'X';
    case KEY_LEFTCTRL: return 'Z'; case KEY_LEFTSHIFT: case BTN_NORTH: return 'C';
    case KEY_ESC: return VK_ESCAPE; case BTN_SELECT: return 'C';
    case KEY_Z: return 'Z'; case KEY_X: return 'X'; case KEY_C: return 'C';
    default: return 0;
    }
}

static void scan(void) {
    const char* dir = getenv("FROGGY_INPUT_DIR");
    if (!dir) dir = "/dev/input";
    for (unsigned n=0; n<32; ++n) {
        unsigned slot=16; bool known=false;
        for (unsigned i=0; i<16; ++i) {
            if (devices[i].fd>=0 && devices[i].node==n) known=true;
            if (devices[i].fd<0) slot=i;
        }
        if (known || slot==16) continue;
        char path[512]; snprintf(path,sizeof path,"%s/event%u",dir,n);
        int fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC);
        if (fd<0) continue;
        devices[slot].fd=fd; devices[slot].node=n;
        memset(devices[slot].down,0,sizeof devices[slot].down);
        devices[slot].dropped=false;
    }
    for (int n=0; n<SDL_NumJoysticks() && n<8; ++n) {
        if (controllers[n] && !SDL_GameControllerGetAttached(controllers[n])) {
            SDL_GameControllerClose(controllers[n]); controllers[n]=NULL;
        }
        if (!controllers[n] && SDL_IsGameController(n)) controllers[n]=SDL_GameControllerOpen(n);
    }
    if (!shared && !getenv("FROGGY_NO_SHARED_INPUT")) {
        key_t key=ftok("/tmp/joy_key",'a');
        int id=key==(key_t)-1 ? -1 : shmget(key,sizeof(uint32_t),0);
        if (id>=0) { void* p=shmat(id,NULL,SHM_RDONLY); if(p!=(void*)-1) shared=p; }
    }
}

void ft_input_init(void) {
    for (unsigned i=0;i<16;++i) devices[i].fd=-1;
    scan(); next_scan=SDL_GetTicks()+1000;
}
void ft_input_clear(void) {
    memset(keyboard,0,sizeof keyboard); memset(&current,0,sizeof current);
    for(unsigned i=0;i<16;++i) memset(devices[i].down,0,sizeof devices[i].down);
}
void ft_input_close(void) {
    for(unsigned i=0;i<16;++i) if(devices[i].fd>=0) {close(devices[i].fd);devices[i].fd=-1;}
    for(unsigned i=0;i<8;++i) if(controllers[i]) {SDL_GameControllerClose(controllers[i]);controllers[i]=NULL;}
    if(shared) {shmdt((const void*)shared);shared=NULL;}
    ls_gp_close(&transport); ft_input_clear();
}

bool ft_input_poll(void) {
    bool quit=false;
    SDL_Event event;
    while(SDL_PollEvent(&event)) {
        if(event.type==SDL_QUIT) quit=true;
        if(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_FOCUS_LOST) ft_input_clear();
        if(event.type==SDL_KEYDOWN || event.type==SDL_KEYUP) {
            int key=sdl_key(event.key.keysym.sym);
            if(key) keyboard[key]=event.type==SDL_KEYDOWN;
        }
    }
    if((int32_t)(SDL_GetTicks()-next_scan)>=0) {scan();next_scan=SDL_GetTicks()+1000;}
    for(unsigned i=0;i<16;++i) if(devices[i].fd>=0) {
        FtEvent e; ssize_t bytes=-1; unsigned processed=0;
        while(processed<512 && (bytes=read(devices[i].fd,&e,sizeof e))==(ssize_t)sizeof e) {
            ++processed;
            if(e.type==EV_SYN && e.code==SYN_DROPPED) {
                devices[i].dropped=true; memset(devices[i].down,0,sizeof devices[i].down);
            } else if(devices[i].dropped) {
                if(e.type==EV_SYN && e.code==SYN_REPORT) devices[i].dropped=false;
            } else if(e.type==EV_KEY) {
                int key=evdev_key(e.code); if(key) devices[i].down[key]=e.value!=0;
            }
        }
        if(processed==512) continue;
        if(bytes==0 || (bytes<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR)) {
            close(devices[i].fd);devices[i].fd=-1;memset(devices[i].down,0,sizeof devices[i].down);
        }
    }
    memset(&current,0,sizeof current);
    const int map[FT_COUNT]={LS_GP_UP,LS_GP_DOWN,LS_GP_LEFT,LS_GP_RIGHT,LS_GP_A,LS_GP_B,
        LS_GP_X,LS_GP_Y,LS_GP_LB,LS_GP_RB,LS_GP_BACK,LS_GP_START};
    struct ls_gp_state state;
    if(ls_gp_read(&transport,&state)) for(unsigned s=0;s<LS_GP_SLOTS;++s) if(state.pads[s].connected) {
        current.connected=true;
        for(unsigned b=0;b<FT_COUNT;++b) current.buttons[b]|=(state.pads[s].buttons&(1u<<map[b]))!=0;
        for(unsigned a=0;a<4;++a) {
            float v=state.pads[s].axes[a]/32768.0f;
            if(fabsf(v)>fabsf(current.axes[a]))current.axes[a]=v;
        }
    }
    const SDL_GameControllerButton sb[FT_COUNT]={SDL_CONTROLLER_BUTTON_DPAD_UP,SDL_CONTROLLER_BUTTON_DPAD_DOWN,
        SDL_CONTROLLER_BUTTON_DPAD_LEFT,SDL_CONTROLLER_BUTTON_DPAD_RIGHT,SDL_CONTROLLER_BUTTON_A,SDL_CONTROLLER_BUTTON_B,
        SDL_CONTROLLER_BUTTON_X,SDL_CONTROLLER_BUTTON_Y,SDL_CONTROLLER_BUTTON_LEFTSHOULDER,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
        SDL_CONTROLLER_BUTTON_BACK,SDL_CONTROLLER_BUTTON_START};
    for(unsigned s=0;s<8;++s) if(controllers[s] && SDL_GameControllerGetAttached(controllers[s])) {
        current.connected=true;
        for(unsigned b=0;b<FT_COUNT;++b) current.buttons[b]|=SDL_GameControllerGetButton(controllers[s],sb[b])!=0;
        for(unsigned a=0;a<4;++a) {
            float v=SDL_GameControllerGetAxis(controllers[s],(SDL_GameControllerAxis)a)/32768.0f;
            if(fabsf(v)>fabsf(current.axes[a])) current.axes[a]=v;
        }
    }
    if(shared) {
        static const int bits[FT_COUNT]={4,6,7,5,13,14,12,15,10,11,0,3};
        uint32_t raw=*shared; current.connected=true;
        for(unsigned b=0;b<FT_COUNT;++b) current.buttons[b]|=(raw&(1u<<bits[b]))!=0;
    }
    return quit || ft_stop || (current.buttons[FT_START] && current.buttons[FT_SELECT]);
}

void ft_input_apply(RunnerKeyboardState* kb,RunnerGamepadState* gp,const FtPad* p,const bool* keys) {
    FtPad neutral={0};if(!p->connected)p=&neutral;
    bool next[256]={false};
    if(keys) memcpy(next,keys,sizeof next);
    next[VK_UP]|=p->buttons[FT_UP] || p->axes[1]<-0.35f;
    next[VK_DOWN]|=p->buttons[FT_DOWN] || p->axes[1]>0.35f;
    next[VK_LEFT]|=p->buttons[FT_LEFT] || p->axes[0]<-0.35f;
    next[VK_RIGHT]|=p->buttons[FT_RIGHT] || p->axes[0]>0.35f;
    /* One canonical key per action. Do not triple-inject Z+Enter+Space. */
    next['Z']|=p->buttons[FT_A]; next['X']|=p->buttons[FT_B];
    next['C']|=p->buttons[FT_X] || p->buttons[FT_Y] || p->buttons[FT_SELECT];
    next[VK_ENTER]|=p->buttons[FT_START];
    if(kb) for(unsigned k=2;k<256;++k) {
        if(next[k] && !kb->keyDown[k]) RunnerKeyboard_onKeyDown(kb,k);
        else if(!next[k] && kb->keyDown[k]) RunnerKeyboard_onKeyUp(kb,k);
    }
    if(gp) {
        GamepadSlot* g=&gp->slots[0]; g->connected=p->connected;
        gp->connectedCount=p->connected?1:0;
        const int map[16]={FT_A,FT_B,FT_X,FT_Y,FT_L,FT_R,-1,-1,FT_SELECT,FT_START,-1,-1,FT_UP,FT_DOWN,FT_LEFT,FT_RIGHT};
        for(unsigned b=0;b<GP_BUTTON_COUNT;++b) {
            bool down=p->connected && b<16 && map[b]>=0 && p->buttons[map[b]];
            g->buttonPressed[b]=down && !g->buttonDownPrev[b];
            g->buttonReleased[b]=!down && g->buttonDownPrev[b];
            g->buttonDown[b]=g->buttonDownPrev[b]=down;g->buttonValue[b]=down?1:0;
        }
        for(unsigned a=0;a<4;++a) g->axisValue[a]=p->connected?p->axes[a]:0;
        snprintf(g->description,sizeof g->description,"LilSmoke native/controller transport");
        snprintf(g->guid,sizeof g->guid,"lilsmoke-native-0");
    }
}
/* Used only by the built-in regression fixture; ownership transfers to this reader. */
int ft_input_fixture_fd(int fd) {
    for(unsigned i=0;i<16;++i) if(devices[i].fd<0) {
        if(fcntl(fd,F_SETFL,O_NONBLOCK)<0)return -1;
        devices[i].fd=fd;devices[i].node=1000+i;return 0;
    }
    return -1;
}
bool ft_input_cancel_requested(void) {
    if(keyboard[VK_ESCAPE] || current.buttons[FT_B])return true;
    for(unsigned i=0;i<16;++i) if(devices[i].down[VK_ESCAPE] || devices[i].down['X'])return true;
    return false;
}
void ft_input_runner(Runner* runner) {
    bool merged[256]; memcpy(merged,keyboard,sizeof merged);
    for(unsigned d=0;d<16;++d) for(unsigned k=0;k<256;++k) merged[k]|=devices[d].down[k];
    ft_input_apply(runner->keyboard,runner->gamepads,&current,merged);
}
