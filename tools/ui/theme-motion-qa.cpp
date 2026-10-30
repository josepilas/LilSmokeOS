#define SDL_MAIN_HANDLED
#include "ThemeMotion.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

static uint32_t qaNow = 1000;
static void require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
static bool near(float a, float b) { return std::fabs(a - b) < 0.0001f; }

static void numericTests() {
    using namespace lilsmoke;
    CarouselMotion motion;
    require(near(motion.position(0), 0), "uninitialized position");
    motion.reset(0);
    motion.select(4, 5, 1000);
    require(near(motion.position(1000), 0), "wrap start continuity");
    require(near(motion.position(1250), -0.875f), "wrap must take shortest path");
    require(near(motion.position(1500), -1), "wrap endpoint");
    require(!motion.moving(1500), "motion must settle");
    motion.select(0, 5, 1600);
    require(near(motion.position(2100), 0), "reverse wrap endpoint");
    for (int i = 0; i < 2000; ++i) {
        uint32_t now = 2200 + i * 11;
        const float before = motion.position(now);
        motion.select(i % 5, 5, now);
        require(near(motion.position(now), before), "rapid input jumps at retarget");
        require(std::isfinite(motion.position(now + 8)), "rapid input nonfinite");
    }
    motion.reset(2);
    require(near(motion.position(999999), 2), "reset must cancel motion");
    motion.reset(0);
    motion.select(1, 2, UINT32_MAX - 100);
    require(near(std::fabs(motion.position(149)), 0.875f), "tick wrap interpolation");
    require(near(std::fabs(motion.position(399)), 1), "tick wrap endpoint");
    motion.select(0, 1, 100);
    require(near(motion.position(101), 0), "one-entry system");
    motion.select(0, 0, 100);
    require(near(motion.position(101), 0), "zero-entry system");
    require(wrapThemeIndex(-1, 5) == 4 && wrapThemeIndex(5, 5) == 0 &&
            wrapThemeIndex(-7, 5) == 3 && wrapThemeIndex(4, 0) == 0,
            "index wrapping");
    require(near(themeFade(0), 0) && near(themeFade(75), 0.5f) &&
            near(themeFade(500), 1) && near(themeFade(0, 0), 1), "fade endpoints");
    std::puts("THEME_MOTION_NUMERIC_OK rapid=2000 tick_wrap=1");
}

#ifndef QA_NUMERIC_ONLY
#include "Font.h"
#include "GridView.h"
#include "Renderer.h"
#include "SystemView.h"
#include "ThemeAnimation.h"
#include "ThemeRender.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <vector>

namespace fs = std::filesystem;
using namespace lilsmoke;

// Wrap only references in this executable, leaving SDL's own timer thread alone.
extern "C" Uint32 __wrap_SDL_GetTicks() { return qaNow; }

namespace lilsmoke {
struct RendererTestAccess {
#ifdef __mips__
    static bool init(Renderer &r, SDL_Surface *surface) {
        r.width_ = r.outputWidth_ = surface->w;
        r.height_ = r.outputHeight_ = surface->h;
        r.softwareSurface_ = surface;
        r.softwareWindowRenderer_ = true;
        r.renderer_ = SDL_CreateSoftwareRenderer(surface);
        return r.renderer_ != nullptr;
    }
#endif
    static size_t entries(const Renderer &r) { return r.textCache_.size(); }
    static size_t bytes(const Renderer &r) { return r.textCacheBytes_; }
    static SDL_Texture *glyph(const Renderer &r, const std::string &text) {
        for (const auto &entry : r.textCache_)
            if (entry.first.text == text) return entry.second.texture;
        return nullptr;
    }
};
}

static std::ofstream frames;
static std::vector<uint32_t> pixels(Renderer &r) {
    std::vector<uint32_t> result(r.width() * r.height());
    require(SDL_RenderReadPixels(static_cast<SDL_Renderer *>(r.sdlRenderer()), nullptr,
        SDL_PIXELFORMAT_ARGB8888, result.data(), r.width() * 4) == 0, "read pixels");
    return result;
}
static void savePixels(Renderer &r, const fs::path &path) {
    auto data = pixels(r);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(data.data(), r.width(),
        r.height(), 32, r.width() * 4, SDL_PIXELFORMAT_ARGB8888);
    require(surface != nullptr, "screenshot surface");
    int result = IMG_SavePNG(surface, path.c_str());
    SDL_FreeSurface(surface);
    require(result == 0, "save screenshot");
}
static void capture(View &view, Renderer &r, Font &font, const Theme &theme,
                    const fs::path &out, const std::string &name, uint32_t time) {
    qaNow = time;
    r.clear(0x070707FF);
    view.render(r, font, theme);
    savePixels(r, out / (name + ".png"));
    frames << name << '\t' << time << '\n';
    r.present();
}
static void press(View &view, InputButton key) {
    view.handleInput(key, InputEvent::Pressed);
}
static PointerEvent click(int x, int y) {
    PointerEvent e{};
    e.type = PointerEventType::Pressed;
    e.button = PointerButton::Primary;
    e.x = x; e.y = y;
    return e;
}
static void pointerTest(View &view, Renderer &r, int &launches, bool allowAbsent = false) {
    int hands = 0;
    for (int y = 0; y < r.height(); ++y) {
        for (int x = 0; x < r.width(); ++x) {
            if (view.cursorAt(x, y) != CursorKind::Hand) continue;
            ++hands;
            int before = launches;
            // First press selects an unselected hit, second must activate it.
            view.handlePointer(click(x, y));
            if (launches == before) view.handlePointer(click(x, y));
            require(launches == before + 1, "settled hand hit must activate exactly once");
            break;
        }
        if (hands) break;
    }
    require(allowAbsent || hands > 0, "settled view has no pointer target");
    int before = launches;
    view.handlePointer(click(-10, -10));
    require(launches == before, "outside pointer activated a target");
}
static void makeImage(const fs::path &path, uint32_t color, bool patterned = false) {
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 160, 90, 32, SDL_PIXELFORMAT_ARGB8888);
    require(s != nullptr, "fixture image allocation");
    SDL_FillRect(s, nullptr, color);
    if (patterned) {
        SDL_Rect box{20, 20, 35, 50};
        SDL_FillRect(s, &box, 0xFFFFFFFF);
    }
    const int result = IMG_SavePNG(s, path.c_str());
    SDL_FreeSurface(s);
    require(result == 0, "fixture image write");
}
static void alphaCacheTest(Renderer &r, Font &font) {
    const std::string text = "QA alpha-only reusable glyph";
    r.clear(0x000000FF);
    font.render(r, text, 5, 5, 0xD16427FF);
    SDL_Texture *texture = RendererTestAccess::glyph(r, text);
    require(texture != nullptr, "missing alpha test glyph");
    const size_t entries = RendererTestAccess::entries(r);
    const size_t bytes = RendererTestAccess::bytes(r);
    auto opaque = pixels(r);
    for (unsigned a = 1; a < 256; ++a) {
        r.clear(0x000000FF);
        font.render(r, text, 5, 5, 0xD1642700 | a);
        require(RendererTestAccess::glyph(r, text) == texture, "alpha recreated glyph texture");
        require(RendererTestAccess::entries(r) == entries && RendererTestAccess::bytes(r) == bytes,
                "alpha changed glyph cache size");
    }
    r.clear(0x000000FF);
    font.render(r, text, 5, 5, 0xD1642740);
    require(pixels(r) != opaque, "alpha cache reuse lost visible alpha modulation");
    r.clear(0x000000FF);
    font.render(r, text, 5, 5, 0xD16427FF);
    require(pixels(r) == opaque, "alpha modulation leaked into subsequent opaque draw");
    std::printf("TEXT_ALPHA_CACHE_OK entries=%zu bytes=%zu alpha_variants=255 same_texture=1\n", entries, bytes);
}
static void scopeTest(Renderer &r, Font &font, const fs::path &out) {
    ThemeElement e;
    e.type = "image"; e.props["path"] = (out / "media-a.png").string();
    e.props["pos"] = "0.1 0.1"; e.props["size"] = "0.25 0.25";
    r.clear(0x000000FF); themeui::drawImage(r, e);
    auto baseline = pixels(r);
    {
        themeui::MotionScope scope(100, 25, 10, 80, "activate");
        r.clear(0x000000FF); themeui::drawImage(r, e);
        require(pixels(r) != baseline, "MotionScope offset/alpha did not render");
    }
    r.clear(0x000000FF); themeui::drawImage(r, e);
    require(pixels(r) == baseline, "MotionScope state leaked");
    ThemeAnimation track;
    track.property = ThemeAnimation::Property::X;
    track.from = 0.1f; track.to = 0.5f; track.hasFrom = track.hasTo = true;
    track.durationMs = 500;
    e.animations.push_back(track);
    r.clear(0x000000FF); themeui::drawImage(r, e);
    require(pixels(r) == baseline, "storyboard sampled outside explicit MotionScope");
    {
        themeui::MotionScope outer(250, 12, 8, 200, "activate");
        r.clear(0x000000FF); themeui::drawImage(r, e);
        auto outerPixels = pixels(r);
        {
            themeui::MotionScope inner(50, 60, 4, 40, "deactivate");
            r.clear(0x000000FF); themeui::drawImage(r, e);
            require(pixels(r) != outerPixels, "nested scope did not override outer scope");
        }
        r.clear(0x000000FF); themeui::drawImage(r, e);
        require(pixels(r) == outerPixels, "nested scope failed to restore outer scope");
    }
    r.clear(0x000000FF); themeui::drawImage(r, e);
    require(pixels(r) == baseline, "nested scopes failed to restore base image");
    ThemeElement text;
    text.type = "text"; text.props["pos"] = "0.1 0.1";
    text.props["size"] = "0.6 0.15";
    text.props["fontsize"] = "0.07";
    r.clear(0x000000FF); themeui::drawText(r, font, text, "Static application overlay");
    const auto staticText = pixels(r);
    ThemeAnimation opacity;
    opacity.property = ThemeAnimation::Property::Opacity;
    opacity.hasFrom = opacity.hasTo = true;
    opacity.from = 0; opacity.to = 1; opacity.durationMs = 500;
    text.animations.push_back(opacity);
    r.clear(0x000000FF); themeui::drawText(r, font, text, "Static application overlay");
    require(pixels(r) == staticText, "outside-scope storyboard hid application overlay");
    {
        themeui::MotionScope scope(0);
        r.clear(0x000000FF); themeui::drawText(r, font, text, "Static application overlay");
        require(pixels(r) != staticText, "explicit text storyboard not sampled");
    }
    r.clear(0x000000FF); themeui::drawText(r, font, text, "Static application overlay");
    require(pixels(r) == staticText, "view scope hid next application's overlay");
    for (uint32_t t : {0u, 100u, 250u, 500u}) {
        r.clear(0x000000FF);
        themeui::MotionScope scope(t);
        themeui::drawImage(r, e);
        savePixels(r, out / ("scope-" + std::to_string(t) + ".png"));
    }
    std::puts("MOTION_SCOPE_OK restored=1 nested=1 base_image=1 base_text=1 cross_view=1");
}
static std::vector<SystemEntry> systems() {
    return {{"snes", "Super Nintendo", 77}, {"nes", "Nintendo", 65},
            {"gba", "Game Boy Advance", 110}, {"md", "Mega Drive", 84}};
}
#include "theme-background-qa.h"
static void sequence(SystemView &view, Renderer &r, Font &font, const Theme &theme,
                     const fs::path &out, const std::string &prefix, uint32_t base) {
    qaNow = base;
    view.onEnter();
    capture(view, r, font, theme, out, prefix + "-before", base + 1000);
    qaNow = base + 1500;
    press(view, InputButton::Right);
    std::set<unsigned> times{0, 100, 250, 500, 750};
    for (unsigned t = 0; t <= 1000; t += 40) times.insert(t);
    int frame = 0;
    for (unsigned t : times) {
        char name[160];
        std::snprintf(name, sizeof(name), "%s-%03d-t%04u", prefix.c_str(), frame++, t);
        capture(view, r, font, theme, out, name, base + 1500 + t);
    }
}
static void fixtureTests(Renderer &r, Font &font, const fs::path &out) {
    backgroundTests(r, font, out);
    const fs::path root = out / "fixtures";
    fs::create_directories(root);
    bool paintedHits = true;
    for (const std::string type : {"horizontal", "vertical", "horizontal_wheel", "vertical_wheel", "horizontal-single"}) {
        const bool single = type == "horizontal-single";
        const std::string carouselType = single ? "horizontal" : type;
        fs::path dir = root / type;
        for (const char *id : {"snes", "nes"}) {
            fs::create_directories(dir / id);
            makeImage(dir / id / "logo.png", std::string(id) == "snes" ? 0xFFFF0000 : 0xFF00FF00);
            std::ofstream xml(dir / id / "theme.xml");
            xml << "<theme><formatVersion>4</formatVersion><view name=\"system\">"
                "<carousel name=\"systemcarousel\"><type>" << carouselType << "</type>"
                "<pos>0 0.1</pos><size>1 0.8</size><color>00000000</color>"
                "<logoSize>0.18 0.16</logoSize><logoScale>1.1</logoScale>"
                "<maxLogoCount>" << (single ? 1 : 11) << "</maxLogoCount><logoRotation>7.5</logoRotation>"
                "<logoRotationOrigin>-5 0.5</logoRotationOrigin></carousel>"
                "<image name=\"logo\"><path>./logo.png</path></image></view></theme>";
        }
        std::string themeDir = dir.string();
        int launches = 0;
        std::string launched;
        SystemView view({{"snes", "SNES", 2}, {"nes", "NES", 3}},
            [&](const SystemEntry &entry) -> View * { ++launches; launched = entry.id; return nullptr; }, &themeDir);
        Theme empty;
        setenv("LILSMOKE_THEME_TRANSITION", "slide", 1);
        sequence(view, r, font, empty, out, "fixture-" + type, 10000);
        const auto settled = pixels(r);
        for (int channel : {0, 1}) {
            std::vector<int> candidates;
            double centerX = 0, centerY = 0;
            for (size_t i = 0; i < settled.size(); ++i) {
                const uint32_t rgb = settled[i] & 0x00FFFFFF;
                const bool color = channel == 0 ? (rgb & 0x00FFFF) == 0 && (rgb >> 16) >= 120
                                               : (rgb & 0xFF00FF) == 0 && ((rgb >> 8) & 255) >= 120;
                if (!color) continue;
                candidates.push_back(i); centerX += i % r.width(); centerY += i / r.width();
            }
            if (candidates.empty()) continue;
            centerX /= candidates.size(); centerY /= candidates.size();
            const int p = *std::min_element(candidates.begin(), candidates.end(), [&](int a, int b) {
                return std::hypot(a % r.width() - centerX, a / r.width() - centerY) <
                       std::hypot(b % r.width() - centerX, b / r.width() - centerY);
            });
            const int x = p % r.width(), y = p / r.width(), before = launches;
            const bool hand = view.cursorAt(x, y) == CursorKind::Hand;
            view.handlePointer(click(x, y));
            if (launches == before) view.handlePointer(click(x, y));
            const bool ok = hand && launches == before + 1 && launched == (channel == 0 ? "snes" : "nes");
            paintedHits = paintedHits && ok;
            std::printf("PAINTED_POINTER_%s fixture=%s channel=%d x=%d y=%d hand=%d launched=%s\n",
                ok ? "OK" : "FAIL", type.c_str(), channel, x, y, hand, launched.c_str());
        }
        pointerTest(view, r, launches);
        std::printf("POINTER_OK fixture=%s\n", type.c_str());
        setenv("LILSMOKE_THEME_TRANSITION", "instant", 1);
        qaNow += 1000; press(view, InputButton::Right);
        capture(view, r, font, empty, out, "instant-fixture-" + type + "-0", qaNow);
        const auto instant = pixels(r);
        capture(view, r, font, empty, out, "instant-fixture-" + type + "-100", qaNow + 100);
        require(pixels(r) == instant, "instant carousel did not settle immediately");
    }
    Theme empty;
    std::string noAssets;
    int launches = 0;
    SystemView fallback(systems(), [&](const SystemEntry &) -> View * { ++launches; return nullptr; }, &noAssets);
    qaNow = 20000; fallback.onEnter();
    capture(fallback, r, font, empty, out, "no-assets", 21000);
    pointerTest(fallback, r, launches);
    press(fallback, InputButton::Left);
    require(fallback.selectedIndex() == 3, "system wrap selection");
    for (int i = 0; i < 101; ++i) { qaNow += 7; press(fallback, InputButton::Right); }
    require(fallback.selectedIndex() == 0, "rapid system selection");
    PointerEvent wheel{}; wheel.type = PointerEventType::Scroll; wheel.wheelY = -1;
    fallback.handlePointer(wheel);
    require(fallback.selectedIndex() == 1, "vertical scroll input");
    wheel.wheelY = 0; wheel.wheelX = -1;
    fallback.handlePointer(wheel);
    require(fallback.selectedIndex() == 0, "horizontal scroll input");
    SystemView zero({}, {}, &noAssets);
    capture(zero, r, font, empty, out, "zero-systems", 23000);
    press(zero, InputButton::Right);
    require(zero.cursorAt(10, 10) == CursorKind::NotAllowed, "zero-entry pointer");
    std::puts("FALLBACK_RAPID_WRAP_WHEEL_OK");
    require(paintedHits, "painted logo pointer target mismatch (see PAINTED_POINTER_FAIL)");
}
static void realTheme(Renderer &r, Font &font, const std::string &root, const fs::path &out) {
    int launches = 0;
    SystemView view(systems(), [&](const SystemEntry &) -> View * { ++launches; return nullptr; }, &root);
    Theme theme;
    require(theme.loadSet(root, "snes", "Super Nintendo"), "real theme did not load");
    setenv("LILSMOKE_THEME_TRANSITION", "slide", 1);
    sequence(view, r, font, theme, out, "system", 30000);
    std::string pointerError;
    try { pointerTest(view, r, launches); }
    catch (const std::exception &error) {
        pointerError = error.what();
        std::printf("REAL_THEME_SYSTEM_POINTER_FAIL: %s\n", error.what());
    }
    for (const char *style : {"fade", "instant", "auto"}) {
        setenv("LILSMOKE_THEME_TRANSITION", style, 1);
        const uint32_t base = qaNow + 2000;
        qaNow = base; press(view, InputButton::Right);
        for (unsigned t : {0u, 100u, 250u, 500u})
            capture(view, r, font, theme, out, std::string(style) + "-" + std::to_string(t), base + t);
    }
    std::vector<ROM> roms(12);
    std::vector<const ROM *> refs;
    for (int i = 0; i < 12; ++i) {
        roms[i].system = "snes";
        roms[i].name = "Motion QA Game " + std::to_string(i);
        roms[i].path = "/not-a-real-rom/" + std::to_string(i);
        roms[i].coverPath = (out / (i % 2 ? "media-b.png" : "media-a.png")).string();
        roms[i].description = "Synthetic QA artwork; production theme layout and renderer.";
        refs.push_back(&roms[i]);
    }
    GridView grid(refs, [&](const ROM *) -> View * { ++launches; return nullptr; }, "SNES", theme);
    qaNow += 2000; grid.onEnter();
    capture(grid, r, font, theme, out, "grid-before", qaNow + 1000);
    const uint32_t base = qaNow + 1000;
    qaNow = base; grid.setSelectedIndex(1);
    for (unsigned t : {0u, 40u, 100u, 150u, 250u, 500u})
        capture(grid, r, font, theme, out, "grid-t" + std::to_string(t), base + t);
    try { pointerTest(grid, r, launches); }
    catch (const std::exception &error) {
        pointerError = error.what();
        std::printf("REAL_THEME_GRID_POINTER_FAIL: %s\n", error.what());
    }
    for (ROM &rom : roms) rom.coverPath.clear();
    grid.setSelectedIndex(2);
    capture(grid, r, font, theme, out, "grid-no-assets", qaNow + 1000);
    std::puts("REAL_THEME_POINTER_MEDIA_OK");
    require(pointerError.empty(), pointerError.c_str());
}
#endif

int main(int argc, char **argv) {
    try {
        numericTests();
#ifndef QA_NUMERIC_ONLY
        if (argc == 2 && std::string(argv[1]) == "--numeric") return 0;
        require(argc == 7, "usage: theme-motion-qa THEME OUTPUT FONT WIDTH HEIGHT fixtures|real|background");
        const fs::path out = argv[2]; fs::create_directories(out);
        const int width = std::stoi(argv[4]), height = std::stoi(argv[5]);
        setenv("SDL_VIDEODRIVER", "dummy", 1); setenv("SDL_AUDIODRIVER", "dummy", 1);
        setenv("LILSMOKE_UI_WINDOWED", "1", 1); setenv("LILSMOKE_UI_HIDDEN", "1", 1);
        setenv("LILSMOKE_DRAW_CLOCK", "0", 1); setenv("LILSMOKE_SHOW_BATTERY", "off", 1);
        setenv("LILSMOKE_SHOW_HELP_PROMPTS", "1", 1);
        SDL_SetMainReady(); SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
#ifdef __mips__
        require(SDL_Init(0) == 0, "headless SDL initialization");
        std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> surface(
            SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888), SDL_FreeSurface);
        require(surface != nullptr, "headless surface");
        Renderer r; require(RendererTestAccess::init(r, surface.get()), "headless renderer initialization");
#else
        Renderer r; require(r.init(width, height), "renderer initialization");
#endif
        Font font; require(font.load(argv[3], 16), "load QA font");
        require(SDL_GetTicks() == qaNow, "deterministic clock linker wrap missing");
        frames.open(out / "frames.tsv"); frames << "frame\tticks_ms\n";
        makeImage(out / "media-a.png", 0xFF2488CC, true);
        makeImage(out / "media-b.png", 0xFFD94658, true);
        alphaCacheTest(r, font); scopeTest(r, font, out);
        if (std::string(argv[6]) == "background") backgroundTests(r, font, out);
        else if (std::string(argv[6]) == "fixtures") fixtureTests(r, font, out);
        else realTheme(r, font, argv[1], out);
        std::printf("THEME_MOTION_RENDER_OK width=%d height=%d software=1 deterministic=1\n", width, height);
#else
        (void)argc; (void)argv; (void)qaNow;
#endif
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "THEME_MOTION_QA_FAIL: %s\n", error.what());
        return 1;
    }
}
