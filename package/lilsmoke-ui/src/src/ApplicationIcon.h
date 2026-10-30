// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <webp/decode.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace lilsmoke {
// Decode large WebP icons directly to a small surface; never retain a full-size
// artwork texture on the 128-MiB target. Other formats retain existing limits.
inline SDL_Surface *loadApplicationIcon(const std::string &path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto bytes = file.tellg();
    if (bytes < 12 || bytes > 2 * 1024 * 1024) return nullptr;
    file.seekg(0);
    char signature[12];
    if (!file.read(signature, sizeof signature)) return nullptr;
    if (std::memcmp(signature, "RIFF", 4) || std::memcmp(signature + 8, "WEBP", 4))
        return IMG_Load(path.c_str());
    file.seekg(0);
    std::vector<unsigned char> encoded(static_cast<size_t>(bytes));
    if (!file.read(reinterpret_cast<char *>(encoded.data()), encoded.size())) return nullptr;
    WebPDecoderConfig config;
    if (!WebPInitDecoderConfig(&config) ||
        WebPGetFeatures(encoded.data(), encoded.size(), &config.input) != VP8_STATUS_OK ||
        config.input.width < 1 || config.input.height < 1 ||
        config.input.width > 2048 || config.input.height > 2048 || config.input.has_animation)
        return nullptr;
    const int edge = std::max(config.input.width, config.input.height);
    const int w = config.input.width * std::min(edge, 256) / edge;
    const int h = config.input.height * std::min(edge, 256) / edge;
    auto *surface = SDL_CreateRGBSurfaceWithFormat(0, std::max(1, w), std::max(1, h),
                                                 32, SDL_PIXELFORMAT_RGBA32);
    if (!surface) return nullptr;
    config.options.use_scaling = edge > 256;
    config.options.scaled_width = surface->w;
    config.options.scaled_height = surface->h;
    config.output.colorspace = MODE_RGBA;
    config.output.is_external_memory = 1;
    config.output.u.RGBA.rgba = static_cast<uint8_t *>(surface->pixels);
    config.output.u.RGBA.stride = surface->pitch;
    config.output.u.RGBA.size = size_t(surface->pitch) * surface->h;
    const auto result = WebPDecode(encoded.data(), encoded.size(), &config);
    WebPFreeDecBuffer(&config.output);
    if (result != VP8_STATUS_OK) { SDL_FreeSurface(surface); return nullptr; }
    SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_BLEND);
    return surface;
}
} // namespace lilsmoke
