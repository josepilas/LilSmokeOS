#define SDL_MAIN_HANDLED
#include "GridView.h"
#include "VirtualKeyboard.h"
#include "hotplug_file.h"
#include "rk707.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace lilsmoke;
using GameListView = GridView;
namespace lilsmoke {
struct InputTestAccess {
    static void setup(Input &input, const char *directory) {
        input.inputDir_ = directory;
        input.gamepadDir_ = directory;
        input.autoconfigDir_ = directory;
    }
    static void ipc(Input &input) {
        input.usbReader_.checked = 0; // Explicit fixture publication bypasses reconnect backoff.
        input.pollUsbGamepads();
    }
    static void sdl(Input &input, int id, uint16_t vendor = 0, uint16_t product = 0) {
        Input::Device device;
        device.fd = -3; device.gamepad = true;
        device.path = "sdl-controller:" + std::to_string(id);
        device.usbVendor = vendor; device.usbProduct = product;
        input.devices_.push_back(std::move(device));
    }
    static int keyboard(Input &input) {
        int pipefd[2]; assert(!pipe2(pipefd, O_NONBLOCK | O_CLOEXEC));
        Input::Device device;
        device.fd = pipefd[0]; device.keyboard = true;
        input.devices_.push_back(std::move(device));
        return pipefd[1];
    }
};
struct RendererTestAccess {
    static size_t textEntries(const Renderer &r) { return r.textCache_.size(); }
    static size_t textBytes(const Renderer &r) { return r.textCacheBytes_; }
    static size_t imageBytes(const Renderer &r) { return r.textureCacheBytes_; }
};
}

static void press(View &view, InputButton button) { view.handleInput(button, InputEvent::Pressed); }
static void search(GameListView &view, const std::string &query) {
    press(view, InputButton::Y);
    assert(view.acceptsText() && !view.allowAutoBackPop());
    while (!view.textValue().empty()) view.handleText("\b");
    view.handleText(query);
    press(view, InputButton::Start);
    assert(!view.acceptsText());
}
static void trigger(Input &input, int instance, int value) {
    SDL_Event event{};
    event.type = SDL_CONTROLLERAXISMOTION;
    event.caxis.which = instance;
    event.caxis.axis = SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
    event.caxis.value = value;
    input.handleSDLEvent(event);
}
static void drain(Input &input, GameListView &view) {
    InputButton button; InputEvent event; PointerEvent pointer;
    int count = 0;
    while (input.poll(button, event, pointer)) {
        assert(++count < 256);
        if (!input.text().empty()) view.handleText(input.text());
        else view.handleInput(button, event);
    }
}
static void emit(int fd, unsigned code, int value = 1) {
    input_event event{}; event.type = EV_KEY; event.code = code; event.value = value;
    assert(write(fd, &event, sizeof(event)) == sizeof(event));
}

int main(int argc, char **argv) {
    assert(argc == 3);
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    setenv("LILSMOKE_UI_WINDOWED", "1", 1);
    setenv("LILSMOKE_UI_HIDDEN", "1", 1);
    const bool headless = std::getenv("QA_HEADLESS") != nullptr;
    const int stride = headless ? 4 : 1;
    gp_fixture fixture; gp_setup(&fixture);
    Renderer renderer;
    if (!headless)
        assert(renderer.init(std::getenv("QA_SMALL_SCREEN") ? 320 : 640,
                             std::getenv("QA_SMALL_SCREEN") ? 240 : 480));
    Font font;
    if (!headless) assert(font.load(argv[2], 16));
    Theme theme;
    assert(theme.load(argv[1]));
    std::vector<ROM> roms(20000);
    std::vector<const ROM *> pointers;
    for (size_t i = 0; i < roms.size(); ++i) {
        roms[i].name = "Game " + std::to_string(i);
        pointers.push_back(&roms[i]);
    }
    roms[5].name = "Kirby";
    roms[42].name = "Super MARIO World";
    roms[43].name = "Mario Kart";
    const ROM *launched = nullptr;
    int launches = 0;
    GameListView games(pointers, [&](const ROM *rom) -> View * {
        launched = rom; ++launches; return nullptr;
    }, "SNES", theme);
    auto render = [&]() { if (!headless) games.render(renderer, font, theme); };
    games.onEnter();
    render(); // Establish the theme's one-column layout when a display is available.
    for (int i = 0; i < 5; ++i) press(games, InputButton::Right);
    press(games, InputButton::A); assert(launched == &roms[5]);
    press(games, InputButton::Y); games.handleText("missing");
    press(games, InputButton::B);
    press(games, InputButton::A); assert(launched == &roms[5]);
    search(games, "mArIo");
    assert(!games.allowAutoBackPop());
    render();
    press(games, InputButton::A); assert(launched == &roms[42]);
    press(games, InputButton::Down);
    press(games, InputButton::A); assert(launched == &roms[43]);
    press(games, InputButton::Y); games.handleText("NOT");
    press(games, InputButton::B);
    press(games, InputButton::A); assert(launched == &roms[43]);
    press(games, InputButton::B);
    assert(games.allowAutoBackPop());
    press(games, InputButton::A); assert(launched == &roms[5]);
    search(games, "nothing matches");
    render();
    const int previousLaunches = launches;
    press(games, InputButton::A); assert(launches == previousLaunches);
    games.updateFastScroll(100, false); games.updateFastScroll(200, true);
    press(games, InputButton::A); assert(launches == previousLaunches);
    search(games, "");
    press(games, InputButton::A); assert(launched == &roms[5]);

    // Shared controller keyboard: A types, X erases, Start accepts, B cancels.
    VirtualKeyboard keyboard; keyboard.reset();
    press(games, InputButton::Y);
    if (!headless) {
        keyboard.render(renderer, font, games.textTitle(), games.textValue());
        std::vector<uint32_t> pixels(renderer.width() * renderer.height());
        assert(SDL_RenderReadPixels(static_cast<SDL_Renderer *>(renderer.sdlRenderer()), nullptr,
            SDL_PIXELFORMAT_ARGB8888, pixels.data(), renderer.width() * 4) == 0);
        assert(std::any_of(pixels.begin(), pixels.end(), [&](uint32_t pixel) { return pixel != pixels[0]; }));
    }
    auto key = keyboard.input(InputButton::A, InputEvent::Pressed);
    assert(key.action == KeyboardAction::Text && key.text == "1");
    games.handleText(key.text);
    key = keyboard.input(InputButton::X, InputEvent::Pressed); games.handleText(key.text);
    assert(games.textValue().empty());
    games.handleText("\xc3\xa9"); games.handleText("\b"); assert(games.textValue().empty());
    games.handleText(std::string(96, 'x')); games.handleText("overflow");
    assert(games.textValue().size() == 96);
    key = keyboard.input(InputButton::B, InputEvent::Pressed);
    assert(key.action == KeyboardAction::Cancel); press(games, InputButton::B);

    Input input;
    InputTestAccess::setup(input, fixture.directory);
    int writer = InputTestAccess::keyboard(input);
    input.setTextMode(true);
    press(games, InputButton::Y);
    for (unsigned code : {KEY_M, KEY_A, KEY_R, KEY_I, KEY_O}) { emit(writer, code); emit(writer, code, 0); }
    emit(writer, KEY_ENTER); emit(writer, KEY_ENTER, 0);
    drain(input, games);
    assert(!games.acceptsText());
    press(games, InputButton::A); assert(launched == &roms[42]);
    press(games, InputButton::B);
    close(writer);
    input.setTextMode(false);

    // RK707 uses the same ABI-1 IPC trigger axes as other shared HIDAPI pads.
    gp_pad(&fixture, 0, 0x0079, 0x181c, 1);
    unsigned char report[27] = {0x80, 0, 15, 128, 128, 128, 128};
    assert(rk707_decode(report, sizeof(report), &fixture.packet.pads[0]));
    assert(fixture.packet.pads[0].axes[5] == 32767 && fixture.packet.pads[0].buttons == 0);
    gp_write(&fixture, ls_gp_now(), 1); InputTestAccess::ipc(input);
    drain(input, games); assert(input.state().r2);
    InputTestAccess::sdl(input, 9, 0x0079, 0x181c);
    trigger(input, 9, 32767); // Duplicate SDL hardware must not latch a second source.
    games.updateFastScroll(1000, false);
    games.updateFastScroll(1000, input.state().r2);
    press(games, InputButton::A); assert(launched == &roms[5 + 3 * stride]);
    games.updateFastScroll(1049, true);
    press(games, InputButton::A); assert(launched == &roms[5 + 3 * stride]);
    games.updateFastScroll(1050, true);
    press(games, InputButton::A); assert(launched == &roms[5 + 6 * stride]);
    report[0] = 0;
    assert(rk707_decode(report, sizeof(report), &fixture.packet.pads[0]));
    gp_write(&fixture, ls_gp_now(), 0); InputTestAccess::ipc(input);
    drain(input, games); assert(!input.state().r2);
    games.updateFastScroll(2000, input.state().r2);
    press(games, InputButton::A); assert(launched == &roms[5 + 6 * stride]);
    fixture.packet.pads[0].axes[5] = 32767;
    gp_write(&fixture, ls_gp_now(), 0); InputTestAccess::ipc(input);
    fixture.packet.pads[0].connected = 0;
    gp_write(&fixture, ls_gp_now(), 0); InputTestAccess::ipc(input);
    drain(input, games); assert(!input.state().r2);

    // SDL controller axis events, including hysteresis, two pads and unplug.
    InputTestAccess::sdl(input, 7); InputTestAccess::sdl(input, 8);
    trigger(input, 7, 32767); drain(input, games); assert(input.state().r2);
    games.updateFastScroll(2050, true);
    press(games, InputButton::A); assert(launched == &roms[5 + 9 * stride]);
    trigger(input, 7, 7000); assert(input.state().r2);
    trigger(input, 8, 32767);
    trigger(input, 7, 0); assert(input.state().r2);
    SDL_Event removed{}; removed.type = SDL_CONTROLLERDEVICEREMOVED; removed.cdevice.which = 8;
    input.handleSDLEvent(removed); drain(input, games); assert(!input.state().r2);
    games.updateFastScroll(2200, false);
    games.onLeave(); games.onEnter();
    games.updateFastScroll(3000, true);
    press(games, InputButton::A); assert(launched == &roms[5 + 9 * stride]);
    games.updateFastScroll(3001, false); games.updateFastScroll(3002, true);
    press(games, InputButton::A); assert(launched == &roms[5 + 12 * stride]);
    press(games, InputButton::Y);
    games.updateFastScroll(4000, true); press(games, InputButton::B);
    games.updateFastScroll(5000, true);
    press(games, InputButton::A); assert(launched == &roms[5 + 12 * stride]);
    games.updateFastScroll(6000, false);
    const uint32_t nearWrap = std::numeric_limits<uint32_t>::max() - 20;
    games.updateFastScroll(nearWrap, true); games.updateFastScroll(29, true);
    press(games, InputButton::A); assert(launched == &roms[5 + 18 * stride]);
    games.updateFastScroll(1000000, true);
    press(games, InputButton::A); assert(launched == &roms[5 + 21 * stride]);

    // Sustained browsing uses the renderer's bounded text cache, not an
    // ever-growing search-name/artwork cache. One tick advances three rows.
    for (uint32_t i = 0; i < 600; ++i) {
        games.updateFastScroll(1000050 + i * 50, true);
        render();
    }
    assert(RendererTestAccess::textBytes(renderer) <= 1024 * 1024);
    assert(RendererTestAccess::textEntries(renderer) <= 128);
    assert(RendererTestAccess::imageBytes(renderer) == 0);
    search(games, "Game"); // Worst-case broad search across the 20k library.
    render();
    press(games, InputButton::A); assert(launched == &roms[0]);
    games.updateFastScroll(0, false);
    for (uint32_t i = 1; i <= 7000; ++i) games.updateFastScroll(i * 50, true);
    press(games, InputButton::A); assert(launched == &roms.back());
    GameListView empty({}, {}, "Empty", theme);
    search(empty, "anything");
    press(empty, InputButton::A);
    empty.updateFastScroll(0, false); empty.updateFastScroll(50, true);
    press(empty, InputButton::B); assert(empty.allowAutoBackPop());
    gp_cleanup(&fixture);
    std::puts("PASS game navigation: search/cancel/empty/20k ROMs, controller keyboard, physical text,");
    std::puts("RK707 IPC and SDL R2 hold/release/unplug, timing/wrap/stall/end bounds");
    if (!headless) std::puts("PASS keyboard pixels and bounded render cache");
}
