// SPDX-License-Identifier: MPL-2.0
#include "ApplicationIcon.h"
#include "Renderer.h"
#include <webp/encode.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

int main(int argc, char **argv) {
    assert(SDL_Init(0) == 0);
    char directory[] = "/tmp/lilsmoke-icon-XXXXXX";
    assert(mkdtemp(directory));
    const std::string path = std::string(directory) + "/icon.webp";
    auto fixture = [&](int w, int h) {
        std::vector<uint8_t> pixels(size_t(w) * h * 4, 255);
        for (size_t at = 0; at < pixels.size(); at += 4) {
            pixels[at] = 241; pixels[at + 1] = 31;
            pixels[at + 2] = 86; pixels[at + 3] = 77;
        }
        uint8_t *encoded = nullptr;
        const size_t size = WebPEncodeLosslessRGBA(pixels.data(), w, h, w * 4, &encoded);
        assert(size && encoded);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char *>(encoded), size);
        WebPFree(encoded);
    };
    fixture(800, 800);
    auto *image = lilsmoke::loadApplicationIcon(path);
    assert(image && image->w == 256 && image->h == 256);
    assert(size_t(image->pitch) * image->h == 256 * 256 * 4);
    uint8_t r, g, b, a;
    SDL_GetRGBA(*static_cast<uint32_t *>(image->pixels), image->format, &r, &g, &b, &a);
    // Libwebp's scaled alpha path premultiplies/unpremultiplies with integer rounding.
    assert(std::abs(int(r) - 241) <= 2 && std::abs(int(g) - 31) <= 2 &&
           std::abs(int(b) - 86) <= 2 && a == 77);
    SDL_FreeSurface(image);
    {
        lilsmoke::Renderer renderer;
        assert(renderer.initOffscreen(320, 240));
        auto *full = renderer.loadImage(path);
        auto *small = renderer.loadImage(path, true);
        int w = 0, h = 0;
        assert(full && small && full != small);
        assert(renderer.textureSize(full, w, h) && w == 800 && h == 800);
        assert(renderer.textureSize(small, w, h) && w == 256 && h == 256);
        assert(renderer.loadImage(path, true) == small);
        fixture(400, 200);
        renderer.invalidateImage(path);
        small = renderer.loadImage(path, true);
        assert(renderer.textureSize(small, w, h) && w == 256 && h == 128);
    }
    fixture(2048, 1);
    image = lilsmoke::loadApplicationIcon(path);
    assert(image && image->w == 256 && image->h == 1);
    SDL_FreeSurface(image);
    fixture(2049, 1);
    assert(!lilsmoke::loadApplicationIcon(path));
    { std::ofstream file(path, std::ios::trunc); file << "RIFFgarbWEBPbroken-payload"; }
    assert(!lilsmoke::loadApplicationIcon(path));
    assert(!lilsmoke::loadApplicationIcon(path + ".missing"));
    if (argc > 1) {
        image = lilsmoke::loadApplicationIcon(argv[1]);
        assert(image && image->w == 256 && image->h == 256);
        std::cout << "Owner WebP decoded to " << image->w << 'x' << image->h << '\n';
        SDL_FreeSurface(image);
    }
    std::filesystem::remove_all(directory);
    SDL_Quit();
    std::cout << "PASS: bounded WebP, aspect ratio, invalid input, cache separation/invalidation\n";
}
