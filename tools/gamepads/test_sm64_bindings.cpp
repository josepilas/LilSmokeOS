#include <cassert>
#include <cstdio>
#include "../../package/lilsmoke-sm64-original/src/input_bridge.cpp"
namespace lilsmoke {
struct InputTestAccess {
    static void setup(Input &p, bool jump, bool attack, bool crouch, bool camera, bool start,
                      int lx, int ly, int rx, int ry) {
        p.devices_.clear();
        Input::Device d;d.fd=-2;d.gamepad=true;
        if(jump)d.held[BTN_SOUTH]=InputButton::A;
        if(attack)d.held[BTN_WEST]=InputButton::Y;
        if(crouch)d.held[KEY_MAX+10]=InputButton::L2;
        if(camera)d.held[BTN_TR]=InputButton::R1;
        if(start)d.held[BTN_START]=InputButton::Start;
        d.axes[ABS_X]={-32768,32767,0,lx};d.axes[ABS_Y]={-32768,32767,0,ly};
        d.axes[ABS_RX]={-32768,32767,0,rx};d.axes[ABS_RY]={-32768,32767,0,ry};
        p.devices_.push_back(d);
    }
};
}
int main() {
    input=std::make_unique<lilsmoke::Input>();
    uint16_t b;int8_t x,y;
    auto sample=[&](bool a,bool attack,bool lt,bool rb,bool start,int lx,int ly,int rx,int ry) {
        lilsmoke::InputTestAccess::setup(*input,a,attack,lt,rb,start,lx,ly,rx,ry);
        b=0;x=y=0;sm64_native_input_read(&b,&x,&y);
    };
    sample(true,true,true,true,true,32767,-32768,0,0);
    assert(b==0xf010 && x==80 && y==80);
    sample(false,false,false,false,false,16384,0,0,0);assert(x==40 && y==0);
    sample(false,false,false,false,false,0,0,0,-32768);assert(b==0x8);
    sample(false,false,false,false,false,0,0,0,32767);assert(b==0x4);
    sample(false,false,false,false,false,0,0,-32768,0);assert(b==0x2);
    sample(false,false,false,false,false,0,0,32767,0);assert(b==0x1);
    input.reset();
    std::puts("PASS SM64 requested A/X/LT/RB/Start, proportional stick speed and all four C-camera directions");
}
