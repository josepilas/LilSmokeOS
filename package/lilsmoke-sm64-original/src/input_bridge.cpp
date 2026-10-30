#include "input_bridge.h"
#include "Input.h"
#include <memory>
#include <exception>
#include <cstdio>
#include <cstdlib>

static std::unique_ptr<lilsmoke::Input> input;
static bool quit;
static bool menu_requested, menu_chord, suppress_buttons;

extern "C" void sm64_native_input_init(void) {
    try {
        input = std::make_unique<lilsmoke::Input>();
        const int devices = input->init(320, 240);
        input->setKeyboardNavigation(false);
        input->setPointerOptions(false, "normal");
        std::fprintf(stderr, "SM64_CONTROLLER native_devices=%d pointer=0\n", devices);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "SM64 controller: %s\n", error.what());
        input.reset();
    }
    quit = false;
    menu_requested = menu_chord = suppress_buttons = false;
}
extern "C" int sm64_native_input_poll(void) {
    if (!input) return 0;
    lilsmoke::InputButton button;
    lilsmoke::InputEvent event;
    lilsmoke::PointerEvent pointer;
    try {
        for (unsigned budget = 0; budget < 256 && input->poll(button, event, pointer); ++budget) {
            if (event == lilsmoke::InputEvent::Pressed && button == lilsmoke::InputButton::Power) quit = true;
            if (event == lilsmoke::InputEvent::Pressed && button == lilsmoke::InputButton::Home) menu_requested = true;
        }
    } catch (const std::exception &error) {
        std::fprintf(stderr, "SM64 controller polling: %s\n", error.what());
        input.reset(); return 1;
    }
    const auto s = input->gameplayState();
    if (!(s.a||s.b||s.x||s.y||s.start||s.select||s.home)) suppress_buttons=false;
    const bool chord = s.start && s.x;
    if (chord && !menu_chord) menu_requested = true;
    menu_chord = chord;
    return quit || (s.start && s.select);
}
extern "C" int sm64_native_input_take_menu(void) { bool requested=menu_requested;menu_requested=false;return requested; }
extern "C" void sm64_native_input_suppress_buttons(void) { suppress_buttons=true; }
extern "C" unsigned sm64_native_input_menu_buttons(void) {
    if (!input) return 0;
    const auto s=input->gameplayState();const auto a=input->analogState();
    return (s.up || a.leftY<-16000 ? 1:0) | (s.down || a.leftY>16000 ? 2:0) |
        (s.left || a.leftX<-16000 ? 4:0) | (s.right || a.leftX>16000 ? 8:0) |
        (s.a?16:0) | (s.b?32:0) | (s.start?64:0);
}
extern "C" void sm64_native_input_read(uint16_t *buttons, int8_t *x, int8_t *y) {
    if (!input || !buttons || !x || !y) return;
    const auto s = input->gameplayState();
    if (menu_requested || menu_chord || suppress_buttons || s.home) return;
    const auto analog = input->analogState();
    // Canonical device mappings feed the original N64 button bits directly.
    uint16_t mapped = (s.a ? 0x8000 : 0) | ((s.x || s.b) ? 0x4000 : 0) | (s.l2 ? 0x2000 : 0) |
                (s.start ? 0x1000 : 0) | (s.l1 ? 0x0020 : 0) | (s.r1 ? 0x0010 : 0) |
                (s.y ? 0x0008 : 0);
    if (analog.hasRight) {
        mapped |= (analog.rightY < -16000 ? 0x0008 : 0) | (analog.rightY > 16000 ? 0x0004 : 0) |
                    (analog.rightX < -16000 ? 0x0002 : 0) | (analog.rightX > 16000 ? 0x0001 : 0);
    }
    if (s.r2) {
        mapped |= (s.up ? 0x0008 : 0) | (s.down ? 0x0004 : 0) |
                    (s.left ? 0x0002 : 0) | (s.right ? 0x0001 : 0);
        *x = *y = 0;
    } else if (analog.hasLeft && (analog.leftX || analog.leftY)) {
        *x = static_cast<int8_t>(int(analog.leftX) * 80 / 32767);
        *y = static_cast<int8_t>(-int(analog.leftY) * 80 / 32767);
    } else {
        if (s.left || s.right) *x = 80 * (int(s.right) - int(s.left));
        if (s.up || s.down) *y = 80 * (int(s.up) - int(s.down));
    }
    *buttons |= mapped;
    if (std::getenv("SM64_ORIGINAL_INPUT_TRACE") && (mapped || *x || *y))
        std::fprintf(stderr, "SM64_CONTROLLER Buttons=%04x Stick=%d,%d\n", mapped, *x, *y);
}
extern "C" void sm64_native_input_close(void) { input.reset(); quit = menu_requested = menu_chord = suppress_buttons = false; }
