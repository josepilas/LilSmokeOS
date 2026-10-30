// Exercises the production evdev parser through isolated pipes, never desktop input.
#include "Input.h"
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <tuple>
#include <vector>

using namespace lilsmoke;
namespace lilsmoke {
struct InputTestAccess {
    static int attach(Input &input, int kind) {
        int fds[2];
        if (pipe2(fds, O_NONBLOCK | O_CLOEXEC)) throw std::runtime_error("pipe2");
        input.inputDir_ = "/nonexistent-lilsmoke-test";
        input.pointerState_.x = 320;
        input.pointerState_.y = 240;
        Input::Device d;
        d.fd = fds[0];
        d.path = "isolated-test";
        d.keyboard = kind == 1;
        d.relativePointer = kind == 2;
        d.absolutePointer = d.usesMultitouch = d.hasTouchSignal = kind == 3;
        if (kind == 4) {
            d.absolutePointer = d.directPointer = d.hasTouchSignal = true;
        }
        d.absMaxX = 1919;
        d.absMaxY = 941;
        input.devices_.push_back(d);
        return fds[1];
    }
};
}
struct Event {
    InputButton button;
    InputEvent kind;
    PointerEvent pointer;
    std::string text;
};
struct Harness {
    Input input;
    int writer;
    explicit Harness(int kind) : writer(InputTestAccess::attach(input, kind)) {}
    ~Harness() { close(writer); }
    void emit(unsigned type, unsigned code, int value) {
        input_event e{};
        e.type = type; e.code = code; e.value = value;
        if (write(writer, &e, sizeof(e)) != sizeof(e)) throw std::runtime_error("write");
    }
    void sync() { emit(EV_SYN, SYN_REPORT, 0); }
    std::vector<Event> drain() {
        std::vector<Event> out;
        InputButton b; InputEvent e; PointerEvent p;
        while (input.poll(b, e, p)) out.push_back({b,e,p,input.text()});
        return out;
    }
};
int checks = 0;
void check(bool ok, const char *what) {
    if (!ok) throw std::runtime_error(what);
    ++checks;
    std::cout << "PASS " << what << "\n";
}
int main() {
    try {
        {
            Harness h(1);
            h.input.setTextMode(true);
            h.emit(EV_KEY, KEY_A, 1);
            h.emit(EV_KEY, KEY_A, 0);
            h.emit(EV_KEY, KEY_LEFTSHIFT, 1);
            h.emit(EV_KEY, KEY_A, 1);
            h.emit(EV_KEY, KEY_1, 1);
            h.emit(EV_KEY, KEY_LEFTSHIFT, 0);
            h.emit(EV_KEY, KEY_BACKSPACE, 1);
            h.emit(EV_KEY, KEY_ENTER, 1);
            h.emit(EV_KEY, KEY_ESC, 1);
            h.sync();
            auto v=h.drain();
            check(v.size()==6, "keyboard does not emit text on key release");
            check(v[0].text=="a" && v[1].text=="A" && v[2].text=="!", "US keyboard shift text");
            check(v[3].text=="\b", "keyboard backspace");
            check(v[4].button==InputButton::Start && v[5].button==InputButton::B, "keyboard Enter submits text / Escape cancels");
        }
        {
            Harness h(0);
            h.input.keymap().set(KEY_ENTER, InputButton::Start);
            h.emit(EV_KEY, KEY_ENTER, 1); h.sync();
            check(h.drain().at(0).button==InputButton::Start, "GPIO keymap remains unchanged");
        }
        {
            Harness h(1);
            h.input.keymap().set(KEY_ENTER, InputButton::Start);
            h.input.keymap().set(KEY_ESC, InputButton::Select);
            h.input.setKeyboardNavigation(false);
            h.emit(EV_KEY, KEY_ENTER, 1); h.sync();
            check(h.drain().at(0).button==InputButton::Start && h.input.state().start,
                  "emulator keyboard Enter remains Start");
            h.emit(EV_KEY, KEY_ENTER, 0); h.sync(); h.drain();
            check(!h.input.state().start, "emulator Start release clears state");
            h.emit(EV_KEY, KEY_ESC, 1); h.sync();
            check(h.drain().at(0).button==InputButton::Select,
                  "emulator Escape remains Select instead of UI Back");
        }
        {
            Harness h(2);
            h.emit(EV_REL, REL_X, 10); h.emit(EV_REL, REL_Y, -20);
            h.emit(EV_KEY, BTN_LEFT, 1); h.emit(EV_REL, REL_WHEEL, -1); h.sync();
            auto v=h.drain();
            check(v.size()==3, "motion wheel and click all delivered in one packet");
            check(v[0].pointer.type==PointerEventType::Moved && v[0].pointer.x==330 &&
                  v[0].pointer.y==220, "relative movement coordinates");
            check(v[1].pointer.type==PointerEventType::Scroll && v[1].pointer.wheelY==-1, "wheel packet");
            check(v[2].pointer.type==PointerEventType::Pressed && v[2].pointer.x==330 &&
                  v[2].pointer.y==220, "click uses same-packet final position");
            h.emit(EV_REL, REL_X, 50000); h.emit(EV_REL, REL_Y, -50000); h.sync(); h.drain();
            check(h.input.pointerState().x==639 && h.input.pointerState().y==0, "cursor clamps to framebuffer");
            h.input.setPointerOptions(false,"normal");
            h.emit(EV_REL, REL_X,-10); h.emit(EV_KEY,BTN_LEFT,0); h.sync();
            check(h.drain().empty() && !h.input.pointerState().visible, "pointer disabled discards packets");
        }
        {
            Harness h(2); h.input.setPointerOptions(true,"slow");
            h.emit(EV_REL,REL_X,1); h.sync(); h.drain();
            h.emit(EV_REL,REL_X,1); h.sync(); h.drain();
            check(h.input.pointerState().x==321, "slow speed preserves subpixel remainder");
        }
        {
            Harness h(3);
            h.emit(EV_ABS,ABS_MT_SLOT,0); h.emit(EV_ABS,ABS_MT_TRACKING_ID,7);
            h.emit(EV_ABS,ABS_MT_POSITION_X,100); h.emit(EV_ABS,ABS_MT_POSITION_Y,100); h.sync();
            check(h.drain().empty(), "touchpad first contact anchors without jump");
            h.emit(EV_ABS,ABS_MT_POSITION_X,200); h.emit(EV_ABS,ABS_MT_POSITION_Y,200); h.sync();
            auto v=h.drain();
            check(v.size()==1 && v[0].pointer.touchpad && v[0].pointer.x>320, "touchpad relative movement");
            int x=h.input.pointerState().x;
            h.emit(EV_ABS,ABS_MT_SLOT,1); h.emit(EV_ABS,ABS_MT_POSITION_X,1800); h.sync();
            check(h.drain().empty() && h.input.pointerState().x==x, "second touch contact cannot jump cursor");
            h.emit(EV_ABS,ABS_MT_SLOT,0); h.emit(EV_ABS,ABS_MT_TRACKING_ID,-1); h.sync(); h.drain();
            h.emit(EV_ABS,ABS_MT_TRACKING_ID,8); h.emit(EV_ABS,ABS_MT_POSITION_X,1500); h.sync();
            check(h.drain().empty() && h.input.pointerState().x==x, "finger recontact reanchors");
            h.emit(EV_KEY,BTN_LEFT,1); h.sync(); v=h.drain();
            check(v.size()==1 && v[0].pointer.button==PointerButton::Primary, "PS4 touchpad click");
        }
        {
            Harness h(0);
            h.emit(EV_ABS,ABS_HAT0X,-1); h.sync();
            auto v=h.drain();
            check(v.size()==1 && v[0].button==InputButton::Left, "gamepad hat press");
            h.emit(EV_ABS,ABS_HAT0X,1); h.sync(); v=h.drain();
            check(v.size()==2 && v[0].kind==InputEvent::Released &&
                  v[1].button==InputButton::Right, "gamepad hat transition releases old direction");
            check(!h.input.state().left && h.input.state().right,
                  "legacy hat reversal does not latch previous direction");
            h.emit(EV_ABS,ABS_HAT0X,0); h.sync(); h.drain();
            check(!h.input.state().left && !h.input.state().right,
                  "legacy hat neutral clears all held directions");
        }
        {
            Harness h(2);
            h.emit(EV_SYN,SYN_DROPPED,0);
            h.emit(EV_REL,REL_X,200); h.emit(EV_KEY,BTN_LEFT,1); h.sync();
            check(h.drain().empty() && h.input.pointerState().x==320, "dropped packet cannot cause stale movement or click");
            h.emit(EV_REL,REL_X,5); h.sync();
            check(h.drain().at(0).pointer.x==325, "input resumes after dropped packet");
        }
        {
            Harness h(4);
            h.input.setPointerViewport(640, 480, 32, 24, 576, 432);
            h.emit(EV_ABS, ABS_X, 960); h.emit(EV_ABS, ABS_Y, 471);
            h.emit(EV_KEY, BTN_TOUCH, 1); h.sync();
            auto v = h.drain();
            check(!v.empty() && v[0].pointer.x == 287 && v[0].pointer.y == 215,
                  "absolute touch maps physical output into TV safe viewport");
            h.emit(EV_ABS, ABS_X, 1919); h.emit(EV_ABS, ABS_Y, 941); h.sync(); h.drain();
            check(h.input.pointerState().x == 575 && h.input.pointerState().y == 431,
                  "touch in right/bottom overscan clamps to content");
            h.emit(EV_ABS, ABS_X, 0); h.emit(EV_ABS, ABS_Y, 0); h.sync(); h.drain();
            check(h.input.pointerState().x == 0 && h.input.pointerState().y == 0,
                  "touch in left/top overscan clamps to content");
        }
        {
            Harness h(2);
            h.input.setPointerViewport(640, 480, 32, 24, 576, 432);
            h.emit(EV_REL, REL_X, 10000); h.emit(EV_REL, REL_Y, 10000); h.sync(); h.drain();
            check(h.input.pointerState().x == 575 && h.input.pointerState().y == 431,
                  "relative mouse clamps to TV content, not overscan");
            h.input.setPointerViewport(640, 480, 0, 0, 640, 480);
            h.emit(EV_REL, REL_X, 10000); h.emit(EV_REL, REL_Y, 10000); h.sync(); h.drain();
            check(h.input.pointerState().x == 639 && h.input.pointerState().y == 479,
                  "leaving TV mode restores full pointer bounds");
        }
        std::cout << "INPUT_SMOKE_OK checks=" << checks << " input_event_bytes=" << sizeof(input_event) << "\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << "\n";
        return 1;
    }
}
