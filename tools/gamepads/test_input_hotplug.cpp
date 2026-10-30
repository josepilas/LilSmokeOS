#include "Input.h"
#include "hotplug_file.h"
#include <cstdio>
#include <fstream>
static uint64_t test_now = 1000;
extern "C" int __wrap_clock_gettime(clockid_t id, struct timespec *ts) {
    (void)id;
    ts->tv_sec = test_now / 1000; ts->tv_nsec = test_now % 1000 * 1000000; return 0;
}
extern "C" int __wrap___clock_gettime64(clockid_t id, struct timespec *ts) { return __wrap_clock_gettime(id, ts); }
namespace lilsmoke {
struct InputTestAccess {
    static void setup(Input &input, const char *dir) { input.gamepadDir_ = dir; }
    static void poll(Input &input) { input.pollUsbGamepads(); }
    static size_t count(const Input &input) { return input.devices_.size(); }
    static unsigned take(Input &input, InputButton button, InputEvent event) {
        unsigned count = 0;
        InputButton b; InputEvent e; PointerEvent p;
        while (input.popQueued(b, e, p)) {
            assert(p.type == PointerEventType::None);
            if (b == button && e == event) ++count;
        }
        return count;
    }
};
}
int main() {
    using namespace lilsmoke;
    struct gp_fixture f; gp_setup(&f);
    Input input; InputTestAccess::setup(input, f.directory);
    gp_pad(&f, 0, 0x045e, 0x028e, 1);
    f.packet.pads[0].buttons = 1U << LS_GP_X;
    f.packet.pads[0].axes[0] = 32767; f.packet.pads[0].axes[3] = -32768;
    f.packet.pads[0].axes[4] = 32767;
    gp_write(&f, test_now, 1); InputTestAccess::poll(input);
    assert(input.gameplayState().x && input.gameplayState().l2);
    assert(input.analogState().leftX == 32767 && input.analogState().rightY == -32767);
    InputTestAccess::take(input, InputButton::None, InputEvent::None);
    gp_pad(&f, 1, 0x0079, 0x181c, 2);
    f.packet.pads[1].buttons = 1U << LS_GP_X;
    gp_write(&f, test_now, 0); InputTestAccess::poll(input);
    InputTestAccess::take(input, InputButton::None, InputEvent::None);
    f.packet.pads[0].connected = 0;
    gp_write(&f, test_now, 0); InputTestAccess::poll(input);
    assert(input.gameplayState().x && !input.gameplayState().l2);
    assert(!InputTestAccess::take(input, InputButton::Y, InputEvent::Released));
    f.packet.pads[1].connected = 0;
    gp_write(&f, test_now, 0); InputTestAccess::poll(input);
    assert(!input.gameplayState().x && !input.analogState().hasLeft);
    assert(InputTestAccess::take(input, InputButton::Y, InputEvent::Released) == 1);
    /* Same slot and instance, different mode/profile, without a neutral poll. */
    gp_pad(&f, 0, 0x045e, 0x028e, 1); f.packet.pads[0].buttons = 1U << LS_GP_A;
    gp_write(&f, test_now, 0); InputTestAccess::poll(input);
    InputTestAccess::take(input, InputButton::None, InputEvent::None);
    const std::string map = std::string(f.directory) + "/0003-0079-181c-0000-hidapi.map";
    { std::ofstream file(map); file << "1 304 0 start\n"; }
    gp_pad(&f, 0, 0x0079, 0x181c, 1); f.packet.pads[0].buttons = 1U << LS_GP_A;
    gp_write(&f, test_now, 0); InputTestAccess::poll(input);
    assert(input.gameplayState().start && !input.gameplayState().a);
    assert(InputTestAccess::take(input, InputButton::Start, InputEvent::Pressed) == 1);
    /* Same PID/instance on a replaced state file must reset input too. */
    test_now += 100; gp_write(&f, test_now, 1); InputTestAccess::poll(input);
    assert(InputTestAccess::take(input, InputButton::Start, InputEvent::Released) == 1);
    for (unsigned i = 0; i < 150; ++i) {
        test_now += 8; gp_write(&f, test_now, 0); InputTestAccess::poll(input);
        assert(input.gameplayState().start);
    }
    test_now += 501; InputTestAccess::poll(input);
    assert(!input.gameplayState().start && InputTestAccess::count(input) == 0);
    assert(InputTestAccess::take(input, InputButton::Start, InputEvent::Released) == 1);
    test_now += 100; gp_write(&f, test_now, 1);
    { Input restarted; InputTestAccess::setup(restarted, f.directory);
      InputTestAccess::poll(restarted); assert(restarted.gameplayState().start); }
    assert(!input.pointerState().visible);
    unlink(map.c_str()); gp_cleanup(&f);
    puts("PASS Common Input/UI/Apps: modes/profiles, replacement, restart, two pads, releases, analogs/triggers, cursor isolation");
}
