// SPDX-License-Identifier: MPL-2.0
#include "loading_ui.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <webp/decode.h>
extern "C" {
#include <TGL/gl.h>
}

namespace portsmoke::sm64 {
namespace {
#include "font8x8_basic.h"
    void check(bool ok, const char* message)
    {
        if (!ok)
            throw std::runtime_error(message);
    }
    class Screen {
    public:
        explicit Screen(const std::filesystem::path& path)
        {
            check(!std::filesystem::is_symlink(path) && std::filesystem::is_regular_file(path), "Missing regular WebP loading icon.");
            auto size = std::filesystem::file_size(path);
            check(size > 12 && size <= 512 * 1024, "Loading icon exceeds compressed budget.");
            std::vector<std::uint8_t> data(size);
            std::ifstream input(path, std::ios::binary);
            check(bool(input.read(reinterpret_cast<char*>(data.data()), data.size())), "Cannot read WebP loading icon.");
            int w = 0, h = 0;
            check(WebPGetInfo(data.data(), data.size(), &w, &h) && w == 128 && h == 128, "Loading icon must be 128x128 WebP.");
            pixels_.resize(128 * 128 * 4);
            check(WebPDecodeRGBAInto(data.data(), data.size(), pixels_.data(), pixels_.size(), 128 * 4), "Native WebP decode failed.");
            image_ = SDL_CreateRGBSurfaceWithFormatFrom(pixels_.data(), 128, 128, 32, 128 * 4, SDL_PIXELFORMAT_RGBA32);
            canvas_ = SDL_CreateRGBSurfaceWithFormat(0, 320, 240, 32, SDL_PIXELFORMAT_ARGB8888);
            if (!image_ || !canvas_) {
                if (image_)
                    SDL_FreeSurface(image_);
                if (canvas_)
                    SDL_FreeSurface(canvas_);
                throw std::runtime_error(SDL_GetError());
            }
            std::cout << "NativeLoadingIcon=WebP Size=128x128 NativeDecoder=libwebp\n";
        }
        ~Screen()
        {
            SDL_FreeSurface(image_);
            SDL_FreeSurface(canvas_);
        }
        void text(const std::string& value, int x, int y, std::uint32_t color)
        {
            for (unsigned char c : value) {
                if (c >= 128)
                    c = '?';
                for (int yy = 0; yy < 8; ++yy)
                    for (int xx = 0; xx < 8; ++xx)
                        if (font8x8_basic[c][yy] & (1 << xx)) {
                            SDL_Rect pixel { x + xx, y + yy, 1, 1 };
                            SDL_FillRect(canvas_, &pixel, color);
                        }
                x += 8;
            }
        }
        void draw(SDL_Surface* surface, SDL_Window* window, const ExtractionStatus& status, bool failed = false)
        {
            auto color = [&](unsigned r, unsigned g, unsigned b) { return SDL_MapRGB(canvas_->format, r, g, b); };
            SDL_FillRect(canvas_, nullptr, color(239, 243, 247));
            SDL_Rect picture { 128, 20, 64, 64 };
            check(SDL_BlitScaled(image_, nullptr, canvas_, &picture) == 0, SDL_GetError());
            text("Super Mario 64", 104, 99, color(22, 32, 42));
            auto phase = status.phase.load();
            const char* title = failed ? "Extraction failed" : phase == ExtractionPhase::Checking ? "Checking assets"
                : phase == ExtractionPhase::Extracting                                            ? "Extracting assets"
                : phase == ExtractionPhase::Verifying                                             ? "Verifying assets"
                : phase == ExtractionPhase::RemovingOriginal                                      ? "Removing original ROM"
                                                                                                  : "Assets ready";
            text(title, (320 - int(std::strlen(title)) * 8) / 2, 119, color(62, 76, 89));
            auto total = status.total.load(), done = std::min(status.completed.load(), total);
            unsigned percent = total ? unsigned(done * 100 / total) : 0;
            SDL_Rect track { 24, 146, 272, 12 };
            SDL_FillRect(canvas_, &track, color(202, 211, 219));
            SDL_Rect bar { 24, 146, int(272 * percent / 100), 12 };
            SDL_FillRect(canvas_, &bar, color(24, 142, 220));
            char line[80];
            std::snprintf(line, sizeof(line), "%u%%", percent);
            text(line, (320 - int(std::strlen(line)) * 8) / 2, 174, color(22, 32, 42));
            std::snprintf(line, sizeof(line), "%.2f MB extracted", double(status.extracted.load()) / 1000000);
            text(line, (320 - int(std::strlen(line)) * 8) / 2, 191, color(62, 76, 89));
            if (failed) {
                for (std::size_t at = 0, row = 0; at < status.error.size() && row < 3; at += 38, ++row)
                    text(status.error.substr(at, 38), 8, 211 + int(row) * 9, color(148, 38, 38));
            } else if (!status.done.load())
                text("Cancel", 136, 222, color(62, 76, 89));
            check(SDL_BlitSurface(canvas_, nullptr, surface, nullptr) == 0, SDL_GetError());
            if (window)
                check(SDL_UpdateWindowSurface(window) == 0, SDL_GetError());
        }

    private:
        std::vector<std::uint8_t> pixels_;
        SDL_Surface *image_ = nullptr, *canvas_ = nullptr;
    };
    struct Worker {
        ExtractionStatus& status;
        std::thread thread;
        ~Worker()
        {
            if (thread.joinable()) {
                status.cancel = true;
                thread.join();
            }
        }
    };
}
bool showExtraction(SDL_Surface* surface, SDL_Window* window, const std::filesystem::path& original,
    const std::filesystem::path& cache, const std::filesystem::path& icon, bool headless)
{
    Screen screen(icon);
    ExtractionStatus status;
    Worker worker { status, std::thread([&] { extractGameData(original, cache, status); }) };
    std::cout << "NativeExtractionUIReady=1" << std::endl;
    bool quit = false;
    while (!status.done.load(std::memory_order_acquire)) {
        SDL_Event event {};
        while (SDL_PollEvent(&event)) {
            bool cancel = event.type == SDL_QUIT || (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) || (event.type == SDL_CONTROLLERBUTTONDOWN && event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK);
            if (cancel) {
                status.cancel = true;
                quit = true;
            }
        }
        screen.draw(surface, window, status);
        SDL_Delay(headless ? 1 : 33);
    }
    worker.thread.join();
    if (quit) {
        std::cout << "NativeExtraction=CANCELLED OriginalRemoved=" << status.originalRemoved << '\n';
        return false;
    }
    screen.draw(surface, window, status, !status.error.empty());
    if (!status.error.empty()) {
        if (!headless && !quit) {
            bool acknowledged = false;
            while (!acknowledged) {
                SDL_Event event {};
                while (SDL_PollEvent(&event))
                    if (event.type == SDL_QUIT || event.type == SDL_KEYDOWN || event.type == SDL_CONTROLLERBUTTONDOWN)
                        acknowledged = true;
                SDL_Delay(16);
            }
        }
        throw std::runtime_error(status.error);
    }
    std::cout << "NativeExtraction=VERIFIED ExtractedBytes=" << status.extracted.load() << " TotalBytes=" << status.total.load()
              << " OriginalRemoved=" << status.originalRemoved << " UsedCache=" << status.usedCache << '\n';
    if (!headless && !status.usedCache)
        SDL_Delay(300);
    return true;
}
}
