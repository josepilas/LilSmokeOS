#pragma once
#include "extraction.hpp"
#include <SDL.h>

namespace portsmoke::sm64 {
bool showExtraction(SDL_Surface* surface, SDL_Window* window,
    const std::filesystem::path& original, const std::filesystem::path& cache,
    const std::filesystem::path& icon, bool headless);
}
