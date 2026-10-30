#include "VirtualKeyboard.h"
#include "Renderer.h"
#include "Font.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>

namespace { volatile sig_atomic_t stopped = 0; void stop(int) { stopped = 1; } }
int main(int argc, char **argv) {
    std::string title = "Text", value;
    size_t limit = 255;
    bool numeric = false, secret = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--numeric")) numeric = true;
        else if (!std::strcmp(argv[i], "--secret")) secret = true;
        else if (!std::strcmp(argv[i], "--title") && i + 1 < argc) title = argv[++i];
        else if (!std::strcmp(argv[i], "--initial") && i + 1 < argc) value = argv[++i];
        else if (!std::strcmp(argv[i], "--max-length") && i + 1 < argc) {
            char *end = nullptr; const long n = std::strtol(argv[++i], &end, 10);
            if (!end || *end || n < 1 || n > 4096) return 2;
            limit = size_t(n);
        } else { std::fprintf(stderr, "usage: lilsmoke-keyboard [--title TEXT] [--initial TEXT] [--max-length 1..4096] [--numeric] [--secret]\n"); return 2; }
    }
    if (value.size() > limit || title.size() > 160 ||
        std::any_of(value.begin(), value.end(), [numeric](unsigned char c) { return c < 32 || c > 126 || (numeric && (c < '0' || c > '9')); })) return 2;
    setenv("SDL_VIDEODRIVER", "directfb", 0);
    setenv("SDL_RENDER_DRIVER", "software", 1);
    std::signal(SIGTERM, stop); std::signal(SIGINT, stop);
    lilsmoke::Renderer renderer;
    if (!renderer.init(640, 480)) return 3;
    lilsmoke::Font font;
    if (!font.load("/usr/share/fonts/dejavu/DejaVuSans.ttf", 16)) return 3;
    lilsmoke::Input input;
    input.init(640, 480); input.setTextMode(true);
    lilsmoke::VirtualKeyboard keyboard; keyboard.reset(numeric);
    SDL_ShowCursor(SDL_DISABLE);
    while (!stopped) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) if (event.type == SDL_QUIT) stopped = 1;
        lilsmoke::InputButton button; lilsmoke::InputEvent action; lilsmoke::PointerEvent pointer;
        while (input.poll(button, action, pointer)) {
            auto result = input.text().empty() ? (pointer.type == lilsmoke::PointerEventType::None ? keyboard.input(button, action) : keyboard.pointer(pointer)) :
                lilsmoke::KeyboardResult{lilsmoke::KeyboardAction::Text, input.text()};
            if (button == lilsmoke::InputButton::Power) stopped = 1;
            if (result.action == lilsmoke::KeyboardAction::Cancel) return 1;
            if (result.action == lilsmoke::KeyboardAction::Accept) {
                return std::printf("%s\n", value.c_str()) < 0 || std::fflush(stdout) != 0 ? 4 : 0;
            }
            if (result.action == lilsmoke::KeyboardAction::Text) {
                for (unsigned char c : result.text) {
                    if (c == '\b' || c == 127) { if (!value.empty()) value.pop_back(); }
                    else if (c >= 32 && c <= 126 && value.size() < limit && (!numeric || (c >= '0' && c <= '9'))) value += char(c);
                }
            }
        }
        keyboard.render(renderer, font, title, value, secret); renderer.present(); SDL_Delay(16);
    }
    return 1;
}
