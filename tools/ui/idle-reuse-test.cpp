// SPDX-License-Identifier: MPL-2.0
#define SDL_MAIN_HANDLED
#include "SystemView.h"
#include "GridView.h"
#include "SettingsView.h"
#include "ThemeActivity.h"
#include "FramePacer.h"
#include "ProcessLaunch.h"
#include "Cursor.h"
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <time.h>

using namespace lilsmoke;
static uint32_t tick = 1000;
static unsigned checks = 0;
static std::vector<uint32_t> *front = nullptr;
static bool reject = false;
extern "C" Uint32 __wrap_SDL_GetTicks() { return tick; }
static void require(bool ok, const char *message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
static void copyFront(const void *pixels, unsigned w, unsigned pitch,
                      int x, int y, int width, int height) {
    for (int row = y; row < y + height; ++row) for (int col = x; col < x + width; ++col) {
        uint32_t pixel;
        std::memcpy(&pixel, static_cast<const uint8_t *>(pixels) + row * pitch + col * 4, 4);
        (*front)[size_t(row) * w + col] = pixel | 0xFF000000U;
    }
}
extern "C" int lilsmoke_present_damage(const void *pixels, unsigned w, unsigned, unsigned pitch,
                                      const int32_t *rects, unsigned count) {
    if (reject) return 0;
    for (unsigned i = 0; i < count; ++i) {
        const auto *r = rects + i * 4;
        copyFront(pixels, w, pitch, r[0], r[1], r[2], r[3]);
    }
    return 1;
}
namespace lilsmoke {
struct RendererTestAccess {
    static void init(Renderer &r, SDL_Surface *s) {
        r.width_ = r.outputWidth_ = s->w; r.height_ = r.outputHeight_ = s->h;
        r.renderer_ = SDL_CreateSoftwareRenderer(s);
        r.softwareWindowRenderer_ = true; r.softwareSurface_ = s;
        r.nativePresentDamage_ = lilsmoke_present_damage;
        require(r.renderer_ != nullptr, "software renderer");
    }
    static bool retained(const Renderer &r, SDL_Surface *s) { return r.canReuseSurface(s); }
    static void present(Renderer &r, SDL_Surface *s) {
        if (r.damageInvalidated_.exchange(false)) {
            r.resetDamageShadow(); r.nativeDamageFailed_ = false;
            r.nativeRetryAtMs_ = 0; r.nativeRetryDelayMs_ = 0;
        }
        r.presentStats_ = {};
        require(SDL_RenderFlush(r.renderer_) == 0, "flush");
        if (!r.presentNativeSurface(s)) {
            r.presentStats_.fallback = true;
            // Model SDL's visible fallback without pretending it preserves the backbuffer.
            copyFront(s->pixels, s->w, s->pitch, 0, 0, s->w, s->h);
        }
    }
    static void expose(Renderer &r) {
        SDL_Event e{}; e.type = SDL_RENDER_DEVICE_RESET;
        Renderer::damageEventWatch(&r, &e);
    }
    static size_t bytes(const Renderer &r) { return r.textureCacheBytes_; }
};
}
static uint64_t cpuNs() {
    timespec t{}; require(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) == 0, "CPU clock");
    return uint64_t(t.tv_sec) * 1000000000ULL + t.tv_nsec;
}
struct Rig {
    std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> surface;
    Renderer r;
    Font font;
    CursorAtlas cursor;
    FrameReuseGuard guard;
    std::vector<uint32_t> visible;
    unsigned composed = 0, retained = 0;
    uint64_t composeNs = 0;
    Rig(int w, int h, const char *fontPath)
        : surface(SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGB888), SDL_FreeSurface),
          visible(size_t(w) * h, 0xFF000000U) {
        require(surface != nullptr, "surface");
        RendererTestAccess::init(r, surface.get()); r.setTextureCacheLimitMB(4);
        require(font.load(fontPath, 16), "font");
    }
    bool frame(View &v, const Theme &theme, bool optimized, bool pointer = false) {
        const auto activity = v.frameActivity(tick, theme);
        const bool draw = !optimized || guard.needsComposition(&v, activity, tick, theme.revision(),
            RendererTestAccess::retained(r, surface.get()), pointer || v.acceptsText());
        if (draw) {
            const auto began = cpuNs();
            v.render(r, font, theme);
            if (pointer) cursor.draw(r, CursorKind::Hand, 37 + tick % 10, 41, 24);
            guard.composed(&v, activity, tick, theme.revision(), pointer || v.acceptsText());
            composeNs += cpuNs() - began; ++composed;
        } else ++retained;
        front = &visible;
        RendererTestAccess::present(r, surface.get());
        require(RendererTestAccess::bytes(r) <= 4U * 1024U * 1024U, "image budget grew");
        return draw;
    }
};

static void contract() {
    static_assert(sizeof(FrameReuseGuard) <= 40, "reuse guard grew");
    static_assert(sizeof(ViewActivity) <= 32, "activity metadata grew");
    FrameReuseGuard g;
    ViewActivity settled{ViewActivity::State::Settled, 7, 100, true};
    const void *a = &g;
    require(g.needsComposition(a, settled, 99, 1, true, false), "first frame must draw");
    g.composed(a, settled, 99, 1, false);
    require(!g.needsComposition(a, settled, 99, 1, true, false), "settled reuse");
    require(g.needsComposition(a, settled, 100, 1, true, false), "deadline equality");
    require(g.needsComposition(a, {}, 99, 1, true, false), "unknown always redraw");
    require(g.needsComposition(a, settled, 99, 1, false, false), "ownership required");
    require(g.needsComposition(a, settled, 99, 1, true, true), "overlay always redraw");
    require(g.needsComposition(a, settled, 99, 2, true, false), "theme reload invalidates");
    auto revision = settled; ++revision.revision;
    require(g.needsComposition(a, revision, 99, 1, true, false), "revision invalidates");
    require(g.needsComposition(&revision, settled, 99, 1, true, false), "view identity invalidates");
    settled.redrawAt = 16;
    require(settled.reusable(0xFFFFFFF0U) && !settled.reusable(16), "deadline wrap");
    for (unsigned i = 0; i < 2; ++i) {
        FramePacer oldPacer(0xFFFFFF00U), newPacer(0xFFFFFF00U);
        uint32_t now = 0xFFFFFF00U;
        for (int frame = 0; frame < 360; ++frame) {
            now += frame == 139 ? 200 : 4;
            const auto before = oldPacer.delayAfterFrame(now), after = newPacer.delayAfterFrame(now);
            require(before == after, "active pacer changed"); now += before;
        }
    }
}

static void scenario(const std::string &root, const char *fontPath, const char *style, int width, int height,
                     int expectStatic = 1) {
    tick = 1000;
    Rig before(width, height, fontPath), after(width, height, fontPath);
    Theme fallback;
    const std::vector<SystemEntry> entries{{"snes", "Super Nintendo", 10}, {"nes", "Nintendo", 4},
                                          {"gba", "Game Boy Advance", 7}, {"md", "Mega Drive", 2}};
    unsigned launched = 0;
    SystemView a(entries, {}, &root), b(entries, [&](const SystemEntry &) -> View * {
        ++launched; requestProcessLaunch({"/fixture/game", {"game"}}); return nullptr;
    }, &root);
    a.onEnter(); b.onEnter();
    if (!root.empty()) {
        Theme inspected; inspected.loadSet(root, "snes", "Super Nintendo");
        if (const auto *view = inspected.findView("system")) {
            for (const auto &element : view->elements)
                std::printf("IDLE_ELEMENT root=%s type=%s name=%s tracks=%zu\n", root.c_str(),
                            element.type.c_str(), element.name.c_str(), element.animations.size());
        }
    }
    setenv("LILSMOKE_THEME_TRANSITION", style, 1);
    for (unsigned frame = 0; frame < 150; ++frame) {
        tick = expectStatic == 0 && frame > 130 ? 5000 + (frame - 130) * 83 : 1000 + frame * 17;
        if (frame == 60 || frame == 61 || frame == 63 || frame == 65) {
            a.handleInput(InputButton::Right, InputEvent::Pressed);
            b.handleInput(InputButton::Right, InputEvent::Pressed);
        }
        if (frame == 118) RendererTestAccess::expose(after.r);
        if (frame == 123) after.r.invalidateImage("/nonexistent/invalidation");
        const bool pointer = frame >= 111 && frame <= 115;
        const bool originalReject = reject;
        reject = frame == 120;
        before.frame(a, fallback, false, pointer);
        const bool drew = after.frame(b, fallback, true, pointer);
        reject = originalReject;
        require(before.visible == after.visible, "retained frame differs byte-for-byte from always-rendered frame");
        if (frame < 30 || (frame >= 60 && frame < 95) || pointer || frame == 116 ||
            frame == 118 || frame == 121 || frame == 123)
            require(drew, "active/overlay/reset/recovery composition skipped");
        if (frame == 122) {
            const int oldPitch = after.surface->pitch;
            after.surface->pitch += 4;
            require(!RendererTestAccess::retained(after.r, after.surface.get()), "changed pitch reused");
            after.surface->pitch = oldPitch;
        }
    }
    if (expectStatic == 1) require(after.retained > 20, "proved static composition was not reduced");
    else if (expectStatic == 0) require(after.retained == 0, "authored/unknown animation was cached");
    require(!processLaunchRequested(), "unexpected launch before input");
    b.handleInput(InputButton::A, InputEvent::Pressed);
    require(launched == 1 && processLaunchRequested(), "launch dispatch lost on settled view");
    takeProcessLaunch();
    require(a.selectedIndex() == b.selectedIndex(), "retarget selection changed");
    std::printf("IDLE_SCENARIO root=%s style=%s size=%dx%d baseline_composed=%u composed=%u retained=%u baseline_cpu_ns=%llu optimized_cpu_ns=%llu\n",
        root.c_str(), style, width, height, before.composed, after.composed, after.retained,
        static_cast<unsigned long long>(before.composeNs), static_cast<unsigned long long>(after.composeNs));
}

static void fallbackAndPower(const char *fontPath) {
    Rig rig(320, 240, fontPath);
    Theme theme;
    GridView grid({}, {}, "SNES", theme);
    require(grid.frameActivity(tick, theme).state == ViewActivity::State::Unknown, "grid accidentally opted in");
    for (int n = 0; n < 3; ++n) require(rig.frame(grid, theme, true), "unknown grid skipped");
    Config config;
    SettingsView settings(config);
    unsigned powers = 0;
    settings.setOnPowerOff([&]() { ++powers; requestProcessLaunch({"/fixture/power", {"poweroff"}}); });
    require(rig.frame(settings, theme, true), "settings skipped");
    settings.handleInput(InputButton::A, InputEvent::Pressed);
    require(!settings.allowAutoBackPop(), "power confirmation did not open");
    require(rig.frame(settings, theme, true), "power modal skipped");
    settings.handleInput(InputButton::B, InputEvent::Pressed);
    require(powers == 0 && !processLaunchRequested(), "cancel dispatched power");
    settings.handleInput(InputButton::A, InputEvent::Pressed);
    settings.handleInput(InputButton::Left, InputEvent::Pressed);
    settings.handleInput(InputButton::A, InputEvent::Pressed);
    require(powers == 1 && processLaunchRequested(), "power confirmation dispatch lost");
    takeProcessLaunch(); // Never execute any helper in this test.
}

int main(int argc, char **argv) {
    try {
        require(argc == 4, "usage: idle-reuse-test THEMES FONT FIXTURES");
        require(SDL_Init(0) == 0, "SDL init");
        setenv("LILSMOKE_DRAW_CLOCK", "0", 1); setenv("LILSMOKE_SHOW_BATTERY", "off", 1);
        contract();
        for (const char *theme : {"es-theme-art-book-next-arkos-main", "es-theme-nes-box-master",
                                 "es-theme-switch-master", "es-theme-WOODY-CODY-RG351V-main"})
            scenario(std::string(argv[1]) + "/" + theme, argv[2], "slide", 320, 240, -1);
        scenario("/root/lilsmoke-os/build/buildroot/output/target/usr/share/lilsmoke/themes/default",
                 argv[2], "slide", 320, 240);
        scenario("", argv[2], "fade", 640, 480);
        scenario("", argv[2], "instant", 856, 480);
        std::filesystem::create_directories(argv[3]);
        const std::string xml = std::string(argv[3]) + "/theme.xml";
        for (const char *extra : {
            "<image name=\"background\" extra=\"static\"><color>186438</color><size>1 1</size><storyboard event=\"activate\"><animation property=\"x\" from=\"0\" to=\"0.1\" begin=\"3000\" duration=\"1000\"/></storyboard></image>",
            "<video name=\"pendingVideo\"/>", "<text name=\"ticker\"><scrollspeed>0.1</scrollspeed><text>Ticker</text></text>"}) {
            { std::ofstream out(xml); out << "<theme><formatVersion>7</formatVersion><view name=\"system\">" << extra << "</view></theme>"; }
            Theme loaded; require(loaded.load(xml), "dynamic theme load");
            require(themeui::systemThemeMayChange(loaded), "dynamic theme classifier failed");
            scenario(argv[3], argv[2], "slide", 320, 240, 0);
        }
        fallbackAndPower(argv[2]);
        std::printf("IDLE_REUSE_OK checks=%u paired_frames=1500\n", checks);
        return 0;
    } catch (const std::exception &e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
}
