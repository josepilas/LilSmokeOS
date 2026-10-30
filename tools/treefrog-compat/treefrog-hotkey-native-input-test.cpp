// SPDX-License-Identifier: MPL-2.0
// Native bridge audit. --require-camera promotes diagnosed camera gaps to failure.
#include "profile-input.cpp"
#include <cassert>
#include <fstream>
#include <sstream>

namespace lilsmoke {
struct InputTestAccess {
    static void reset(Input &in) {
        in.devices_.clear();in.queued_.clear();in.refreshState();
        in.inputDir_="/nonexistent/treefrog-hotkey-input";
    }
    static unsigned pad(Input &in,bool sdl=false) {
        Input::Device device;device.fd=sdl?-3:-2;device.gamepad=true;
        device.path=sdl?"sdl-controller:123":"audit-pad";
        in.devices_.push_back(std::move(device));return (unsigned)in.devices_.size()-1;
    }
    static void button(Input &in,unsigned pad,unsigned code,bool down) {
        auto &device=in.devices_.at(pad);
        in.sourceButton(device,(int)code,in.deviceButton(device,code),down?InputEvent::Pressed:InputEvent::Released);
    }
    static void axis(Input &in,unsigned pad,unsigned code,int value,bool trigger=false) {
        auto &device=in.devices_.at(pad);
        if(!device.axes.count(code))device.axes[code]={trigger?0:-32768,32767,0,0};
        in.queueAxis(device,code,value);
    }
    static void hat(Input &in,unsigned pad,unsigned code,int value) {
        auto &device=in.devices_.at(pad);device.axes[code]={-1,1,0,0};in.queueAxis(device,code,value);
    }
    static void remove(Input &in,unsigned pad) {
        in.releaseButtons(in.devices_.at(pad));in.devices_.erase(in.devices_.begin()+pad);in.refreshState();
    }
    static void remap(Input &in,unsigned pad,unsigned code,InputButton action) {
        in.devices_.at(pad).overrides[(int)code]=action;
    }
};
}

static void exhaustive_transport(void) {
    const unsigned to[]={13,14,12,15,0,16,3,1,2,10,11,4,6,7,5};
    const int16_t axes[6]={0};
    for(uint32_t buttons=0;buttons<(1u<<15);buttons++) {
        uint32_t expected=0;for(unsigned i=0;i<15;i++)if(buttons&(1u<<i))expected|=1u<<to[i];
        assert(treefrog_pad(buttons,axes)==expected);
    }
}
static void native_buttons(void) {
    using A=lilsmoke::InputTestAccess;
    const unsigned codes[]={BTN_SOUTH,BTN_EAST,BTN_WEST,BTN_NORTH,BTN_SELECT,BTN_MODE,BTN_START,
        BTN_THUMBL,BTN_THUMBR,BTN_TL,BTN_TR,BTN_DPAD_UP,BTN_DPAD_DOWN,BTN_DPAD_LEFT,BTN_DPAD_RIGHT,BTN_TL2,BTN_TR2};
    const unsigned bits[]={13,14,12,15,0,16,3,1,2,10,11,4,6,7,5,8,9};
    A::reset(*input);unsigned pad=A::pad(*input);
    for(unsigned i=0;i<sizeof codes/sizeof codes[0];i++) {
        A::button(*input,pad,codes[i],true);assert(treefrog_input_read()==(1u<<bits[i]));
        A::button(*input,pad,codes[i],false);assert(treefrog_input_read()==0);
    }
    unsigned second=A::pad(*input);
    A::button(*input,pad,BTN_TL,true);A::button(*input,second,BTN_TL,true);
    A::button(*input,pad,BTN_TL,false);assert(treefrog_input_read()==1u<<10);
    A::remove(*input,second);assert(treefrog_input_read()==0);
    // Explicit per-controller remapping wins over positional X/Y defaults.
    A::remap(*input,pad,BTN_WEST,lilsmoke::InputButton::L1);
    A::button(*input,pad,BTN_WEST,true);assert(treefrog_input_read()==1u<<10);
    A::button(*input,pad,BTN_WEST,false);assert(treefrog_input_read()==0);
}
static void native_axes(void) {
    using A=lilsmoke::InputTestAccess;
    A::reset(*input);unsigned pad=A::pad(*input);
    A::axis(*input,pad,ABS_RX,0);A::axis(*input,pad,ABS_RY,0);
    const unsigned axes[]={ABS_X,ABS_X,ABS_Y,ABS_Y,ABS_Z,ABS_RZ};
    const int values[]={-32768,32767,-32768,32767,32767,32767};
    const unsigned bits[]={7,5,4,6,8,9};
    for(unsigned i=0;i<6;i++) {
        A::axis(*input,pad,axes[i],values[i],i>=4);assert(treefrog_input_read()==1u<<bits[i]);
        A::axis(*input,pad,axes[i],0,i>=4);assert(treefrog_input_read()==0);
    }
    A::hat(*input,pad,ABS_HAT0X,-1);A::hat(*input,pad,ABS_HAT0Y,-1);
    assert(treefrog_input_read()==((1u<<7)|(1u<<4)));
    A::hat(*input,pad,ABS_HAT0X,0);assert(treefrog_input_read()==1u<<4);
    A::hat(*input,pad,ABS_HAT0Y,0);assert(treefrog_input_read()==0);
    // Digital stick camera must preserve all existing buttons, including L/R.
    setenv("LILSMOKE_TREEFROG_INPUT_PROFILE","classicube",1);
    const int camera[4][2]={{-32768,0},{32767,0},{0,-32768},{0,32767}};
    const unsigned look[]={15,13,12,14};
    for(unsigned i=0;i<4;i++) {
        A::axis(*input,pad,ABS_RX,camera[i][0]);A::axis(*input,pad,ABS_RY,camera[i][1]);
        A::button(*input,pad,BTN_TL,true);A::button(*input,pad,BTN_TR,true);
        assert(treefrog_input_read()==((1u<<look[i])|(1u<<10)|(1u<<11)));
        A::button(*input,pad,BTN_TL,false);A::button(*input,pad,BTN_TR,false);
        A::axis(*input,pad,ABS_RX,0);A::axis(*input,pad,ABS_RY,0);assert(treefrog_input_read()==0);
    }
    unsetenv("LILSMOKE_TREEFROG_INPUT_PROFILE");
}
static unsigned camera_audit(void) {
    using A=lilsmoke::InputTestAccess;
    unsigned gaps=0;A::reset(*input);
    unsigned first=A::pad(*input),second=A::pad(*input);
    for(unsigned pad:{first,second}){A::axis(*input,pad,ABS_RX,0);A::axis(*input,pad,ABS_RY,0);}
    A::axis(*input,second,ABS_RX,32767);
    setenv("LILSMOKE_TREEFROG_INPUT_PROFILE","classicube",1);
    if(!(treefrog_input_read()&(1u<<13))) {
        puts("NATIVE_INPUT_AUDIT GAP neutral first pad hides second-pad camera");gaps++;
    }
    A::reset(*input);A::pad(*input,true);
    SDL_Event event{};event.type=SDL_CONTROLLERAXISMOTION;event.caxis.which=123;
    event.caxis.axis=SDL_CONTROLLER_AXIS_RIGHTX;event.caxis.value=32767;
    input->handleSDLEvent(event);
    if(!(treefrog_input_read()&(1u<<13))) {
        puts("NATIVE_INPUT_AUDIT GAP SDL fallback does not retain stick axes");gaps++;
    }
    unsetenv("LILSMOKE_TREEFROG_INPUT_PROFILE");return gaps;
}
static void keymap_contract(const char *path) {
    const char *names[]={"LEFT","RIGHT","UP","DOWN","A","B","X","Y","L1","R1","L2","R2","START","SELECT"};
    const unsigned bits[]={7,5,4,6,13,14,12,15,10,11,8,9,3,0};
    std::ifstream f(path);assert(f);std::map<std::string,unsigned> roles;std::string line;
    while(std::getline(f,line)) {
        auto equal=line.find('=');assert(equal!=std::string::npos);
        roles[line.substr(0,equal)]=(unsigned)std::stoul(line.substr(equal+1));
    }
    for(unsigned i=0;i<14;i++)assert(roles.at(names[i])==bits[i]);
}
int main(int argc,char **argv) {
    assert(argc>=2);setenv("LILSMOKE_GAMEPAD_STATE","/nonexistent/native-input-audit",1);
    assert(SDL_Init(SDL_INIT_EVENTS)==0);input=std::make_unique<lilsmoke::Input>();
    exhaustive_transport();native_buttons();native_axes();keymap_contract(argv[1]);
    unsigned gaps=camera_audit();input.reset();SDL_Quit();
    printf("NATIVE_INPUT_AUDIT 32768 canonical masks, 17 buttons, axes/hats/triggers/remap/release/keymap passed; camera_gaps=%u\n",gaps);
    return argc>2 && !strcmp(argv[2],"--require-camera") && gaps?2:0;
}
