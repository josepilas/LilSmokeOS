// Production evdev parser; isolated pipes do not send input to the desktop.
#include "Input.h"
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace lilsmoke;
namespace lilsmoke {
struct InputTestAccess {
    static int attach(Input &input, const char *id, int low, int high) {
        int fds[2];
        if (pipe2(fds, O_NONBLOCK | O_CLOEXEC)) throw std::runtime_error("pipe");
        input.inputDir_ = "/nonexistent-gamepad-test";
        input.gamepadDir_ = "/tmp/lilsmoke-gamepad-qa";
        input.autoconfigDir_ = "/tmp/lilsmoke-gamepad-qa/autoconfig";
        Input::Device pad;
        pad.fd = fds[0]; pad.path = id; pad.id = id; pad.name = id; pad.gamepad = true;
        int center = low + (high-low)/2;
        pad.axes[ABS_X] = {low,high,0,center};
        pad.axes[ABS_Y] = {low,high,0,center};
        pad.axes[ABS_RX] = {low,high,0,center};
        pad.axes[ABS_RY] = {low,high,0,center};
        pad.axes[ABS_Z] = {0,255,0,0};
        pad.axes[ABS_RZ] = {0,255,0,0};
        pad.axes[ABS_HAT0X] = {-1,1,0,0};
        pad.axes[ABS_HAT0Y] = {-1,1,0,0};
        input.loadGamepadMap(pad);
        input.devices_.push_back(std::move(pad));
        return fds[1];
    }
    static void detach(Input &input, size_t index) {
        auto &pad = input.devices_.at(index);
        pad.held.clear(); close(pad.fd);
        input.devices_.erase(input.devices_.begin()+index);
        input.refreshState();
    }
    static std::string profile(const Input &input, size_t index) {
        return input.gamepadProfile(input.devices_.at(index),"USB test pad",
            {ABS_X,ABS_Y,ABS_Z,ABS_RX,ABS_RY,ABS_RZ,ABS_HAT0X,ABS_HAT0Y},
            {BTN_SOUTH,BTN_EAST,BTN_NORTH,BTN_WEST,BTN_TL,BTN_TR,
             BTN_SELECT,BTN_START,BTN_THUMBL,BTN_THUMBR,BTN_TRIGGER_HAPPY1});
    }
    static std::string legacyProfile(const Input &input, size_t index) {
        return input.gamepadProfile(input.devices_.at(index),"Legacy USB pad",
            {ABS_X,ABS_Y}, {BTN_0,BTN_SOUTH,BTN_NORTH,BTN_TRIGGER_HAPPY1});
    }
};
}
int checks = 0;
void check(bool ok, const char *label) {
    if (!ok) throw std::runtime_error(label);
    ++checks; std::cout << "PASS " << label << '\n';
}
void emit(int fd, unsigned type, unsigned code, int value) {
    input_event event{}; event.type=type; event.code=code; event.value=value;
    if (write(fd,&event,sizeof event)!=sizeof event) throw std::runtime_error("write");
}
int drain(Input &input) {
    InputButton button; InputEvent event; PointerEvent pointer; int count=0;
    while(input.poll(button,event,pointer)) ++count;
    return count;
}
int main() {
    try {
        std::filesystem::create_directories("/tmp/lilsmoke-gamepad-qa");
        std::filesystem::remove("/tmp/lilsmoke-gamepad-qa/pad-one.map");
        Input input;
        int first=InputTestAccess::attach(input,"pad-one",-32768,32767);
        int second=InputTestAccess::attach(input,"pad-two",0,255);
        emit(first,EV_KEY,BTN_SOUTH,1); drain(input);
        check(input.state().a,"standard south button confirms in UI");
        emit(second,EV_KEY,BTN_SOUTH,1); drain(input);
        emit(first,EV_KEY,BTN_SOUTH,0); drain(input);
        check(input.state().a,"release from one pad preserves second pad held A");
        emit(second,EV_KEY,BTN_SOUTH,0); drain(input);
        check(!input.state().a,"last pad release clears held A");
        emit(first,EV_KEY,BTN_THUMBL,1); drain(input);
        check(input.state().l3,"left stick click is a real mappable button");
        emit(first,EV_KEY,BTN_THUMBL,0); drain(input);
        std::string profile=InputTestAccess::profile(input,0);
        check(profile.find("input_b_btn = \"0\"")!=std::string::npos &&
              profile.find("input_a_btn = \"1\"")!=std::string::npos,
              "RetroPad button layout uses kernel joystick indices");
        check(profile.find("input_l_x_minus_axis = \"-0\"")!=std::string::npos &&
              profile.find("input_r_y_plus_axis = \"+4\"")!=std::string::npos,
              "both analog sticks retain full analog axes in RetroArch");
        check(profile.find("input_up_axis = \"-7\"")!=std::string::npos &&
              profile.find("input_l2_axis = \"+2\"")!=std::string::npos,
              "RetroArch hat and trigger indices follow descriptor layout");
        emit(first,EV_ABS,ABS_X,-32768); drain(input);
        check(input.state().left,"signed analog minimum navigates left");
        emit(first,EV_ABS,ABS_X,32767); drain(input);
        check(!input.state().left && input.state().right,"analog reversal releases old direction");
        emit(first,EV_ABS,ABS_X,100); drain(input);
        check(!input.state().right,"analog dead zone returns to neutral");
        emit(second,EV_ABS,ABS_Y,0); drain(input);
        check(input.state().up,"unsigned analog range minimum navigates up");
        emit(second,EV_ABS,ABS_Y,128); drain(input);
        check(!input.state().up,"unsigned centered stick is neutral");
        emit(first,EV_ABS,ABS_Z,255); drain(input);
        check(input.state().l2,"analog trigger pressure maps L2");
        emit(first,EV_ABS,ABS_Z,0); drain(input);
        check(!input.state().l2,"trigger rest does not latch negative direction");
        emit(first,EV_ABS,ABS_HAT0Y,1); drain(input);
        emit(second,EV_KEY,BTN_DPAD_DOWN,1); drain(input);
        emit(first,EV_ABS,ABS_HAT0Y,0); drain(input);
        check(input.state().down,"hat release preserves digital D-pad held on other pad");
        emit(second,EV_KEY,BTN_DPAD_DOWN,0); drain(input);
        emit(first,EV_KEY,BTN_SOUTH,1); emit(second,EV_KEY,BTN_EAST,1); drain(input);
        emit(first,EV_SYN,SYN_DROPPED,0); drain(input);
        check(!input.state().a && input.state().b,"dropped packet clears only affected pad");
        emit(first,EV_SYN,SYN_REPORT,0); drain(input);
        input.setBindingCapture(true);
        emit(first,EV_KEY,BTN_TRIGGER_HAPPY1,1);
        InputButton button; InputEvent event; PointerEvent pointer;
        check(input.poll(button,event,pointer) && input.rawBinding().ready &&
              input.rawBinding().code==BTN_TRIGGER_HAPPY1 && button==InputButton::None,
              "unmapped raw button can be captured without guessed reverse lookup");
        check(input.bindCaptured(InputButton::X),"per-device remap saves unknown button");
        profile=InputTestAccess::profile(input,0);
        check(profile.find("input_x_btn = \"10\"")!=std::string::npos,
              "unknown-button remap is exported to RetroArch profile");
        input.setBindingCapture(false);
        emit(first,EV_KEY,BTN_TRIGGER_HAPPY1,0); drain(input);
        emit(first,EV_KEY,BTN_TRIGGER_HAPPY1,1); drain(input);
        check(input.state().x,"captured unknown button now activates mapped action");
        emit(second,EV_KEY,BTN_TRIGGER_HAPPY1,1); drain(input);
        emit(first,EV_KEY,BTN_TRIGGER_HAPPY1,0); drain(input);
        check(!input.state().x,"per-device mapping does not leak to another controller");
        input.setBindingCapture(true);
        emit(first,EV_ABS,ABS_RX,32767);
        check(input.poll(button,event,pointer) && input.rawBinding().type==EV_ABS &&
              input.rawBinding().direction==1,"unused axis direction is capturable");
        check(input.bindCaptured(InputButton::R1),"axis direction remap persists");
        input.setBindingCapture(false);
        emit(first,EV_ABS,ABS_RX,0); drain(input);
        emit(first,EV_ABS,ABS_RX,32767); drain(input);
        check(input.state().r1,"remapped axis direction drives a shoulder button");
        Input reloaded;
        int third=InputTestAccess::attach(reloaded,"pad-one",-32768,32767);
        emit(third,EV_KEY,BTN_TRIGGER_HAPPY1,1); drain(reloaded);
        check(reloaded.state().x,"per-device map reloads on reconnect");
        close(third);
        input.setBindingCapture(true);
        emit(first,EV_KEY,BTN_0,1);
        check(input.poll(button,event,pointer) && input.rawBinding().code==BTN_0 &&
              input.bindCaptured(InputButton::X),"legacy low-index button can replace previous remapping");
        input.setBindingCapture(false);
        profile=InputTestAccess::legacyProfile(input,0);
        check(profile.find("input_x_btn = \"0\"")!=std::string::npos,
              "explicit remapping wins even before default buttons in joydev order");
        emit(first,EV_KEY,BTN_0,0);
        emit(first,EV_KEY,BTN_TRIGGER_HAPPY1,1); drain(input);
        check(!input.state().x,"replaced unknown-button override does not remain active");
        emit(first,EV_KEY,BTN_0,1); drain(input);
        check(input.state().x,"replacement legacy button activates mapped action");
        InputTestAccess::detach(input,0);
        check(input.state().b && !input.state().r1,"disconnect preserves other pad and releases removed pad");
        close(first); close(second);
        std::cout << "GAMEPAD_SMOKE_OK checks=" << checks << " input_event_bytes=" << sizeof(input_event) << '\n';
    } catch(const std::exception &error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
