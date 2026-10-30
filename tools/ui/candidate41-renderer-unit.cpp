#define C41_RENDERER_UNITS
#define main candidate41ScenarioMain
#include "candidate41-perf.cpp"
#undef main

static std::vector<uint32_t> readPixels(Renderer &r) {
    std::vector<uint32_t> p(320 * 240);
    auto *sdl = static_cast<SDL_Renderer *>(r.sdlRenderer());
    SDL_Rect viewport; SDL_RenderGetViewport(sdl, &viewport);
    SDL_RenderSetViewport(sdl, nullptr);
    check(SDL_RenderReadPixels(sdl, nullptr,
        SDL_PIXELFORMAT_ARGB8888, p.data(), 320 * 4) == 0, "read pixels");
    SDL_RenderSetViewport(sdl, &viewport);
    return p;
}
int main(int argc, char **argv) {
    try {
        check(argc == 2, "fixture directory required");
        setenv("LILSMOKE_UI_FAST_BLITS", "1", 1);
        std::filesystem::create_directories(argv[1]);
#ifndef __mips__
        setenv("SDL_VIDEODRIVER", "dummy", 1);
        setenv("LILSMOKE_UI_WINDOWED", "1", 1); setenv("LILSMOKE_UI_HIDDEN", "1", 1);
        for (const auto *profile : {"0", "1"}) {
            setenv("LILSMOKE_UI_PROFILE", profile, 1);
            Renderer probe; check(probe.init(32, 24), "profile fixture renderer");
            check(RendererTestAccess::profiling(probe) == (profile[0] != '0'), "PROFILE=0 must disable renderer clocks");
        }
#endif
        SDL_SetMainReady(); check(SDL_Init(0) == 0, "SDL init");
        unsigned checks = 0;
        for (const auto targetFormat : {SDL_PIXELFORMAT_RGB888, SDL_PIXELFORMAT_ARGB8888}) {
        std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> surface(
            SDL_CreateRGBSurfaceWithFormat(0, 320, 240, 32, targetFormat), SDL_FreeSurface);
        check(surface != nullptr, "surface");
        Renderer r; check(RendererTestAccess::init(r, surface.get()), "renderer");
        auto *sdl = static_cast<SDL_Renderer *>(r.sdlRenderer());
        for (const auto value : {"", "0.1 0.2", "0.1", "-0 +0", "bad", "1 bad", "1e-20 2e10", "nan 1", "1 2 ignored"})
            for (float a : {0.f, .5f, -17.f}) for (float b : {0.f, .25f, 19.f}) {
                ThemeElement element; element.props["pos"] = value;
                float expectedX, expectedY, actualX, actualY;
                element.getPos(expectedX, expectedY, a, b);
                for (int i = 0; i < 3; ++i) {
                    themeui::propertyPair(element, "pos", actualX, actualY, a, b);
                    check(std::memcmp(&expectedX, &actualX, 4) == 0 && std::memcmp(&expectedY, &actualY, 4) == 0,
                        "cached property parser changed float/default bits");
                    ++checks;
                }
            }
        for (const auto format : {SDL_PIXELFORMAT_ARGB8888, SDL_PIXELFORMAT_ABGR8888, SDL_PIXELFORMAT_RGB888}) {
            auto *image = SDL_CreateRGBSurfaceWithFormat(0, 13, 17, 32, format);
            check(image != nullptr, "source image");
            for (int y = 0; y < 17; ++y) for (int x = 0; x < 13; ++x) {
                const auto value = SDL_MapRGBA(image->format, (x*31+y)%256, (x+y*19)%256, (x*7+y*11)%256, (x*17+y*37)%256);
                std::memcpy(static_cast<uint8_t *>(image->pixels) + y * image->pitch + x * 4, &value, 4);
            }
            const auto path = std::filesystem::path(argv[1]) / (std::to_string(format) + ".png");
            check(IMG_SavePNG(image, path.c_str()) == 0, "save source fixture");
            auto *stream = SDL_CreateTexture(sdl, format, SDL_TEXTUREACCESS_STREAMING, 13, 17);
            check(stream && SDL_UpdateTexture(stream, nullptr, image->pixels, image->pitch) == 0, "source streaming texture");
            auto *owned = r.loadImage(path.string()); check(owned != nullptr, "owned source texture");
            SDL_RenderSetViewport(sdl, nullptr); SDL_RenderSetClipRect(sdl, nullptr);
            SDL_SetTextureBlendMode(stream, SDL_BLENDMODE_BLEND);
            const SDL_Rect direct{7, 11, 81, 67};
            check(RendererTestAccess::fast(r, stream, direct), "direct path must be exercised");
            SDL_SetTextureScaleMode(stream, SDL_ScaleModeLinear);
            check(!RendererTestAccess::fast(r, stream, direct), "linear scale must retain SDL fallback");
            SDL_SetTextureScaleMode(stream, SDL_ScaleModeNearest);
            SDL_SetTextureBlendMode(stream, SDL_BLENDMODE_ADD);
            check(!RendererTestAccess::fast(r, stream, direct), "additive blend must retain SDL fallback");
            for (SDL_Texture *texture : {stream, owned}) {
                SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
                for (int a : {0, 1, 64, 127, 128, 254, 255}) for (int color : {255, 155, 0})
                    for (bool clipped : {false, true}) for (bool inset : {false, true})
                        for (SDL_Rect rect : {SDL_Rect{7, 11, 81, 67}, SDL_Rect{-11, 14, 81, 67},
                            SDL_Rect{23, 190, 81, 67}, SDL_Rect{290, 40, 81, 67}, SDL_Rect{20, 20, 161, 151}}) {
                            SDL_Rect viewport = inset ? SDL_Rect{3, 7, 314, 226} : SDL_Rect{0, 0, 320, 240};
                            SDL_RenderSetViewport(sdl, &viewport);
                            SDL_Rect clip{10, 20, 200, 160};
                            SDL_RenderSetClipRect(sdl, clipped ? &clip : nullptr);
                            SDL_SetTextureAlphaMod(texture, a);
                            SDL_SetTextureColorMod(texture, color, color == 255 ? 255 : 87, color == 255 ? 255 : 103);
                            const uint32_t background = targetFormat == SDL_PIXELFORMAT_ARGB8888 ? 0x2371B367 : 0x2371B3FF;
                            r.clear(background);
                            r.drawTexture(texture, rect.x, rect.y, rect.w, rect.h);
                            const auto actual = readPixels(r);
                            r.clear(background);
                            check(SDL_RenderCopy(sdl, texture, nullptr, &rect) == 0, "reference SDL copy");
                            check(actual == readPixels(r), "scaled blit differs from original SDL pixels");
                            check(RendererTestAccess::images(r) <= 8U * 1024U * 1024U, "image and derived caches bounded");
                            RendererTestAccess::accounting(r);
                            ++checks;
                        }
            }
            SDL_RenderSetViewport(sdl, nullptr); SDL_RenderSetClipRect(sdl, nullptr);
            const size_t cachedBefore = RendererTestAccess::derived(r);
            const SDL_Rect stable{17, 23, 93, 71};
            SDL_SetTextureBlendMode(owned, SDL_BLENDMODE_BLEND);
            for (int frame = 0; frame < 24; ++frame) {
                SDL_SetTextureAlphaMod(owned, frame < 12 ? (frame * 53) % 256 : 255);
                SDL_SetTextureColorMod(owned, frame < 12 ? 233 : 255,
                                      frame < 12 ? 87 : 255, frame < 12 ? 141 : 255);
                r.clear(0x2371B367);
                r.drawTexture(owned, stable.x, stable.y, stable.w, stable.h);
                const auto actual = readPixels(r);
                // Clear the reference without advancing the production frame clock.
                SDL_SetRenderDrawColor(sdl, 0x23, 0x71, 0xB3, 0x67);
                check(SDL_RenderClear(sdl) == 0, "stable reference clear");
                check(SDL_RenderCopy(sdl, owned, nullptr, &stable) == 0, "stable reference copy");
                check(actual == readPixels(r), "stable cached scale changed SDL pixels");
                RendererTestAccess::accounting(r); ++checks;
            }
            Uint32 ownedFormat = 0;
            check(SDL_QueryTexture(owned, &ownedFormat, nullptr, nullptr, nullptr) == 0, "owned format");
            if (ownedFormat == SDL_PIXELFORMAT_RGB888)
                check(RendererTestAccess::derived(r) > cachedBefore, "opaque stable raster was not reused");
            else
                check(RendererTestAccess::derived(r) == cachedBefore, "alpha raster must retain direct blend");
            SDL_DestroyTexture(stream); SDL_FreeSurface(image);
            r.invalidateImage(path.string());
            RendererTestAccess::accounting(r);
        }
        r.setTextureCacheLimitMB(4);
        for (int i = 0; i < 9; ++i) {
            const auto path = std::filesystem::path(argv[1]) / ("pressure-" + std::to_string(i) + ".png");
            auto *image = SDL_CreateRGBSurfaceWithFormat(0, 512, 512, 32, SDL_PIXELFORMAT_ARGB8888);
            check(image != nullptr, "pressure image");
            SDL_FillRect(image, nullptr, 0x98765432U + i);
            check(IMG_SavePNG(image, path.c_str()) == 0, "save pressure fixture");
            SDL_FreeSurface(image);
            auto *texture = r.loadImage(path.string()); check(texture != nullptr, "pressure texture");
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
            for (int frame = 0; frame < 4; ++frame) {
                SDL_RenderSetViewport(sdl, nullptr); SDL_RenderSetClipRect(sdl, nullptr);
                r.clear(0x2371B3FF); r.drawTexture(texture, -10, 11, 281, 167);
                check(RendererTestAccess::images(r) <= 4U * 1024U * 1024U, "derivatives must not expand image budget");
                RendererTestAccess::accounting(r); ++checks;
            }
            if (i % 2) r.invalidateImage(path.string());
            RendererTestAccess::accounting(r);
        }
        for (int i = 0; i < 9; ++i)
            r.invalidateImage((std::filesystem::path(argv[1]) / ("pressure-" + std::to_string(i) + ".png")).string());
        const auto startDecodes = decodedImages;
        for (int frame = 0; frame < 6; ++frame) {
            r.clear(0x2371B3FF);
            for (int i = 0; i < 5; ++i) {
                auto *texture = r.loadImage((std::filesystem::path(argv[1]) / ("pressure-" + std::to_string(i) + ".png")).string());
                check(texture != nullptr, "scan image");
                r.drawTexture(texture, i * 50, 10, 50, 50);
                RendererTestAccess::accounting(r);
                check(RendererTestAccess::images(r) <= 4U * 1024U * 1024U, "scan cache bound");
                ++checks;
            }
        }
        check(decodedImages - startDecodes == 15, "five-image scan must retain three reused images, not thrash all five");
        for (int offset : {0, 1, 2, 3}) {
            constexpr int width = 137, height = 73, pitch = width * 4 + 12;
            std::vector<uint8_t> memory(pitch * height + 8, 0x91);
            auto *s = SDL_CreateRGBSurfaceFrom(memory.data()+offset, width, height, 32, pitch,
                0xFF0000, 0xFF00, 0xFF, 0xFF000000);
            check(s != nullptr, "unaligned surface");
            std::vector<uint32_t> displayed(width * height);
            uint32_t random = 12345;
            for (int f = 0; f < 80; ++f) {
                for (int n = 0; n < 13; ++n) {
                    random = random * 1664525U + 1013904223U;
                    int x = random % width;
                    random = random * 1664525U + 1013904223U;
                    int y = random % height;
                    std::memcpy(memory.data() + offset + y*pitch + x*4, &random, 4);
                }
                check(RendererTestAccess::present(r, s), "unaligned presentation");
                for (size_t i = 0; i < damage.size(); i += 4)
                    for (int y = damage[i+1]; y < damage[i+1]+damage[i+3]; ++y)
                        std::memcpy(displayed.data()+y*width+damage[i], memory.data()+offset+y*pitch+damage[i]*4, damage[i+2]*4);
                for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
                    uint32_t p; std::memcpy(&p, memory.data()+offset+y*pitch+x*4, 4);
                    check(((p ^ displayed[y*width+x]) & 0xFFFFFF) == 0, "unaligned damage dropped pixels");
                }
                ++checks;
            }
            SDL_FreeSurface(s);
        }
        }
        std::printf("C41_RENDERER_UNIT_OK cases=%u rgba_clip_modulation_alignment=1\n", checks);
        return 0;
    } catch (const std::exception &e) { std::fprintf(stderr, "FAIL %s\n", e.what()); return 1; }
}
