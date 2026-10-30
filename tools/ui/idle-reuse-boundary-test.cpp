// SPDX-License-Identifier: MPL-2.0
#define main idleReuseScenarioEntry
#include "idle-reuse-test.cpp"
#undef main

class UnknownProgress final : public View {
public:
    unsigned renders = 0;
    View *handleInput(InputButton, InputEvent) override { return nullptr; }
    void render(Renderer &r, Font &, const Theme &) override {
        ++renders; r.clear(0x102030FF); r.fillRect(10, 10, renders, 8, 0x40A070FF);
    }
};

static void dynamicBoundaries(const char *fontPath, const std::string &directory) {
    Rig rig(640, 480, fontPath);
    Theme fallback;
    UnknownProgress progress;
    for (int frame = 0; frame < 90; ++frame) {
        ++tick;
        require(rig.frame(progress, fallback, true), "unknown async progress was retained");
    }
    require(progress.renders == 90, "async rendering side effects lost");
    const std::string empty;
    SystemView system({{"snes", "Super Nintendo", 1}}, {}, &empty);
    system.onEnter();
    rig.frame(system, fallback, true);
    tick += 600;
    require(rig.frame(system, fallback, true), "settled terminal frame missing");
    require(!rig.frame(system, fallback, true), "settled frame not reused");
    require(!rig.r.canReuseFrame(), "offscreen fixture must not claim real window ownership");
    {
        Rig control(640, 480, fontPath);
        SystemView reference({{"snes", "Super Nintendo", 1}}, {}, &empty);
        reference.onEnter();
        const auto oldComposed = rig.composed, oldRetained = rig.retained;
        for (unsigned frame = 0; frame < 120; ++frame) {
            tick += 17;
            require(control.frame(reference, fallback, false), "always-render control skipped");
            require(!rig.frame(system, fallback, true), "settled idle frame recomposed");
            require(control.visible == rig.visible, "steady idle differs byte-for-byte");
        }
        require(rig.composed == oldComposed && rig.retained == oldRetained + 120,
                "steady idle composition count");
        std::printf("IDLE_STEADY loops=120 baseline_composed=%u optimized_composed=0 retained=120 bytes=%zu\n",
                    control.composed, rig.visible.size() * sizeof(uint32_t));
    }
    for (const char *kind : {"clock", "battery"}) {
        const auto file = directory + "/overlay.xml";
        { std::ofstream xml(file);
          xml << "<theme><view name=\"screen\">";
          if (!std::strcmp(kind, "clock")) xml << "<text name=\"clock\"><fontsize>0.03</fontsize></text>";
          else xml << "<batteryindicator name=\"batteryIndicator\"/>";
          xml << "</view></theme>"; }
        const auto prior = fallback.revision();
        require(fallback.load(file) && fallback.revision() > prior, "theme revision did not advance");
        require(rig.frame(system, fallback, true), "theme reload reused stale frame");
        setenv(!std::strcmp(kind, "clock") ? "LILSMOKE_DRAW_CLOCK" : "LILSMOKE_SHOW_BATTERY", "on", 1);
        for (unsigned frame = 0; frame < 5; ++frame) {
            tick += 17;
            require(system.frameActivity(tick, fallback).state == ViewActivity::State::Active,
                    "live overlay was declared settled");
            require(rig.frame(system, fallback, true), "live clock/battery was retained");
        }
        setenv("LILSMOKE_DRAW_CLOCK", "0", 1); setenv("LILSMOKE_SHOW_BATTERY", "off", 1);
    }
    rig.guard.invalidate();
    require(rig.frame(system, fallback, true), "external view invalidation ignored");
    require(!rig.frame(system, fallback, true), "stable view did not recover");
    rig.r.setSafeAreaPercent(5);
    require(!RendererTestAccess::retained(rig.r, rig.surface.get()), "safe-area change retained stale canvas");
    require(rig.frame(system, fallback, true), "safe-area change skipped composition");
}

#ifndef __mips__
static void actualWindow() {
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("LILSMOKE_UI_WINDOWED", "1", 1); setenv("LILSMOKE_UI_HIDDEN", "1", 1);
    Renderer renderer;
    std::vector<uint32_t> visible(64 * 48);
    front = &visible;
    require(renderer.init(64, 48), "real SDL window init");
    require(!renderer.canReuseFrame(), "unpresented window retained");
    renderer.clear(0x193761FF); renderer.present();
    require(renderer.presentStats().native && renderer.canReuseFrame(), "real SDL software ownership not recognized");
    SDL_Event reset{}; reset.type = SDL_RENDER_DEVICE_RESET;
    require(SDL_PushEvent(&reset) == 1, "SDL reset event");
    require(!renderer.canReuseFrame(), "actual event watch failed to invalidate");
    renderer.clear(0x193761FF); renderer.present();
    require(renderer.presentStats().fullFrame && renderer.canReuseFrame(), "window repaint after reset failed");
    renderer.invalidateImage("/absent/artwork");
    require(!renderer.canReuseFrame(), "asset invalidation failed");
    renderer.clear(0x193761FF); renderer.present();
    require(renderer.canReuseFrame(), "window ownership recovery");
    reject = true; renderer.present(); reject = false;
    require(renderer.presentStats().fallback && !renderer.canReuseFrame(), "SDL fallback granted retention");
}
#endif

int main(int argc, char **argv) {
    try {
        require(argc == 3, "usage: idle-reuse-boundary-test FONT OUTPUT");
        std::filesystem::create_directories(argv[2]);
        require(SDL_Init(0) == 0, "SDL init");
        setenv("LILSMOKE_DRAW_CLOCK", "0", 1); setenv("LILSMOKE_SHOW_BATTERY", "off", 1);
        dynamicBoundaries(argv[1], argv[2]);
        // Actual delayed and looping storyboards remain active even after their first final frame.
        const std::string xml = std::string(argv[2]) + "/theme.xml";
        { std::ofstream out(xml);
          out << "<theme><view name=\"system\"><text name=\"timed\" extra=\"true\"><text>Delayed</text>"
                 "<storyboard event=\"activate\" repeat=\"32\"><animation property=\"opacity\" from=\"0\" to=\"1\" "
                 "begin=\"3000\" duration=\"1000\" autoreverse=\"true\"/></storyboard></text></view></theme>"; }
        scenario(argv[2], argv[1], "slide", 320, 240, 0);
        for (const char *root : {"es-theme-art-book-next-arkos-main", "es-theme-WOODY-CODY-RG351V-main"}) {
            Theme inspected;
            inspected.loadSet(std::string("/root/lilsmoke-vm-validation/storage/themes/") + root, "snes", "SNES");
            for (const char *name : {"system", "screen"}) if (const auto *view = inspected.findView(name))
                for (const auto &element : view->elements) {
                    std::printf("IDLE_AUDIT root=%s view=%s type=%s name=%s tracks=%zu\n", root, name,
                                element.type.c_str(), element.name.c_str(), element.animations.size());
                    for (const auto &property : element.props)
                        if (property.first.find("scroll") != std::string::npos ||
                            property.first.find("video") != std::string::npos ||
                            property.first.find("marquee") != std::string::npos)
                            std::printf("IDLE_AUDIT_PROPERTY key=%s value=%s\n", property.first.c_str(), property.second.c_str());
                }
        }
#ifndef __mips__
        actualWindow();
#endif
        std::printf("IDLE_BOUNDARIES_OK checks=%u paired_frames=150 actual_window=%d\n", checks,
#ifdef __mips__
                    0
#else
                    1
#endif
        );
        return 0;
    } catch (const std::exception &e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
}
