#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <cstdio>
#include <vector>

int main() {
    auto *surface = SDL_CreateRGBSurfaceWithFormat(0, 320, 240, 32, SDL_PIXELFORMAT_RGB888);
    auto *renderer = SDL_CreateSoftwareRenderer(surface);
    auto *image = SDL_CreateRGBSurfaceWithFormat(0, 13, 17, 32, SDL_PIXELFORMAT_ARGB8888);
    for (int y = 0; y < 17; ++y) for (int x = 0; x < 13; ++x)
        static_cast<Uint32 *>(image->pixels)[y * image->pitch / 4 + x] =
            SDL_MapRGBA(image->format, x * 19, y * 13, (x * 17 + y * 11) % 256, (x * 7 + y * 23) % 256);
    auto *texture = SDL_CreateTextureFromSurface(renderer, image);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(texture, 128);
    std::vector<Uint32> full(320 * 240), split(full.size());
    SDL_SetRenderDrawColor(renderer, 27, 61, 83, 255);
    SDL_RenderClear(renderer); SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, full.data(), 320 * 4);
    SDL_RenderClear(renderer);
    for (const SDL_Rect clip : {SDL_Rect{0, 0, 107, 240}, SDL_Rect{107, 0, 213, 240}}) {
        SDL_RenderSetClipRect(renderer, &clip); SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    }
    SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, split.data(), 320 * 4);
    unsigned differences = 0;
    for (size_t i = 0; i < full.size(); ++i) differences += full[i] != split[i];
    std::printf("SDL_UNMODIFIED_CLIP_DIFFERENCES pixels=%u total=%zu\n", differences, full.size());
    SDL_DestroyTexture(texture); SDL_FreeSurface(image);
    SDL_DestroyRenderer(renderer); SDL_FreeSurface(surface); SDL_Quit();
    return differences ? 0 : 1;
}
