#define SDL_MAIN_HANDLED
#include "Cursor.h"
#include "Font.h"
#include "Renderer.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#ifdef LILSMOKE_COUNT_UPLOADS
static size_t textUploads = 0;
static bool drawingText = false;
extern "C" SDL_Texture *__real_SDL_CreateTextureFromSurface(SDL_Renderer *, SDL_Surface *);
extern "C" SDL_Texture *__wrap_SDL_CreateTextureFromSurface(SDL_Renderer *renderer, SDL_Surface *surface) {
    if (drawingText) ++textUploads;
    return __real_SDL_CreateTextureFromSurface(renderer, surface);
}
#endif

static double milliseconds(Uint64 start) {
    return (SDL_GetPerformanceCounter() - start) * 1000.0 / SDL_GetPerformanceFrequency();
}

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    SDL_SetMainReady();
    lilsmoke::Renderer renderer;
    if (!renderer.init(1280, 720)) return 3;
    lilsmoke::Font font;
    if (!font.load(argv[1], 24)) return 4;
    lilsmoke::CursorAtlas cursor;
    cursor.load(renderer, argv[2]);
    // Explicit control reproduces the pre-cache font draw/upload/destroy path.
    const bool uncached = std::getenv("LILSMOKE_PERF_UNCACHED") != nullptr;
    TTF_Font *referenceFont = uncached ? TTF_OpenFont(argv[1], 24) : nullptr;
    if (uncached && !referenceFont) return 5;
    auto reference = [&](const std::string &text, int x, int y, uint32_t color, bool styled) {
        SDL_Color c{static_cast<Uint8>(color >> 24), static_cast<Uint8>(color >> 16),
                    static_cast<Uint8>(color >> 8), static_cast<Uint8>(color)};
        SDL_Surface *surface = TTF_RenderUTF8_Blended(referenceFont, text.c_str(), c);
        if (!surface) std::abort();
        SDL_Texture *texture = SDL_CreateTextureFromSurface(
            static_cast<SDL_Renderer *>(renderer.sdlRenderer()), surface);
        SDL_Rect dst{x, y, surface->w, surface->h};
        SDL_FreeSurface(surface);
        if (!texture) std::abort();
        if (styled) {
            const int lineHeight = TTF_FontLineSkip(referenceFont);
            if (34 > lineHeight) dst.y += (34 - lineHeight) / 2;
            SDL_Point center{dst.w / 2, dst.h / 2};
            SDL_RenderCopyEx(static_cast<SDL_Renderer *>(renderer.sdlRenderer()), texture,
                             nullptr, &dst, 0.0, &center, SDL_FLIP_NONE);
        } else {
            SDL_QueryTexture(texture, nullptr, nullptr, &dst.w, &dst.h);
            SDL_RenderCopy(static_cast<SDL_Renderer *>(renderer.sdlRenderer()), texture, nullptr, &dst);
        }
        SDL_DestroyTexture(texture);
    };
    const int iterations = argc > 3 ? std::max(1, std::atoi(argv[3])) : 120;
    std::vector<double> textTimes, frameTimes, cursorTimes, presentTimes;
    size_t measuredUploads = 0;
    for (int frame = 0; frame < iterations + 5; ++frame) {
        const Uint64 frameStart = SDL_GetPerformanceCounter();
        renderer.clear(0x202020FF);
        const Uint64 textStart = SDL_GetPerformanceCounter();
#ifdef LILSMOKE_COUNT_UPLOADS
        const size_t priorUploads = textUploads;
        drawingText = true;
#endif
        for (int row = 0; row < 16; ++row) {
            const std::string label = "Game " + std::to_string(row) + " - Super Famicom library";
            if (uncached) {
                reference(label, 20, 20 + row * 36, 0xFFFFFFFF, false);
                reference("Settings / LilSmoke Explorer", 670, 20 + row * 36, 0xD9C9A3FF, true);
            } else {
                font.render(renderer, label, 20, 20 + row * 36, 0xFFFFFFFF);
                font.renderStyled(renderer, "Settings / LilSmoke Explorer", 670, 20 + row * 36,
                                  570, 34, 0xD9C9A3FF, "", 24, "left", "center");
            }
        }
#ifdef LILSMOKE_COUNT_UPLOADS
        drawingText = false;
        if (frame >= 5) measuredUploads += textUploads - priorUploads;
#endif
        const double textMs = milliseconds(textStart);
        const Uint64 cursorStart = SDL_GetPerformanceCounter();
        cursor.draw(renderer, lilsmoke::CursorKind::Normal, 32 + frame * 7 % 1200,
                    64 + frame * 3 % 600, 32);
        const double cursorMs = milliseconds(cursorStart);
        const Uint64 presentStart = SDL_GetPerformanceCounter();
        renderer.present();
        const double presentMs = milliseconds(presentStart);
        const double frameMs = milliseconds(frameStart);
        if (frame >= 5) {
            textTimes.push_back(textMs); frameTimes.push_back(frameMs);
            cursorTimes.push_back(cursorMs); presentTimes.push_back(presentMs);
        }
    }
    auto report = [](const char *name, std::vector<double> times) {
        std::sort(times.begin(), times.end());
        std::printf("%s median_ms=%.4f p95_ms=%.4f\n", name,
                    times[times.size() / 2], times[(times.size() - 1) * 95 / 100]);
    };
    report("text_32_draws", textTimes); report("cursor", cursorTimes);
    report("present", presentTimes); report("frame", frameTimes);
    int outputWidth = 0, outputHeight = 0;
    SDL_GetRendererOutputSize(static_cast<SDL_Renderer *>(renderer.sdlRenderer()), &outputWidth, &outputHeight);
    std::printf("UI_CURSOR_PERF_OK driver=%s logical=1280x720 output=%dx%d frames=%d text_path=%s\n",
                SDL_GetCurrentVideoDriver(), outputWidth, outputHeight, iterations,
                uncached ? "uncached-control" : "production-cache");
    std::printf("text_uploads_measured=%zu accounting=%s\n", measuredUploads,
#ifdef LILSMOKE_COUNT_UPLOADS
                "linker-wrap"
#else
                "disabled"
#endif
    );
    if (referenceFont) TTF_CloseFont(referenceFont);
}
