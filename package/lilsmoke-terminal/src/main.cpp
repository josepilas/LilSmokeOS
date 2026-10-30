// SPDX-License-Identifier: MPL-2.0
#include "Keyboard.h"
#include "Pty.h"
#include <SDL_ttf.h>
#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
volatile sig_atomic_t stopping = 0;
void stopSignal(int) { stopping = 1; }

int dimension(const char *name, int fallback, int minimum, int maximum) {
    const char *value = std::getenv(name);
    if (!value) return fallback;
    char *end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    return *end || parsed < minimum || parsed > maximum ? fallback : static_cast<int>(parsed);
}

void utf8(std::string &text, uint32_t cp) {
    if (cp <= 0x7f) text += static_cast<char>(cp);
    else if (cp <= 0x7ff) {
        text += static_cast<char>(0xc0 | (cp >> 6));
        text += static_cast<char>(0x80 | (cp & 63));
    } else if (cp <= 0xffff) {
        text += static_cast<char>(0xe0 | (cp >> 12));
        text += static_cast<char>(0x80 | ((cp >> 6) & 63));
        text += static_cast<char>(0x80 | (cp & 63));
    } else if (cp <= 0x10ffff) {
        text += static_cast<char>(0xf0 | (cp >> 18));
        text += static_cast<char>(0x80 | ((cp >> 12) & 63));
        text += static_cast<char>(0x80 | ((cp >> 6) & 63));
        text += static_cast<char>(0x80 | (cp & 63));
    }
}

class Display {
public:
    ~Display() {
        for (auto &item : glyphs_) SDL_FreeSurface(item.second.surface);
        if (font_) TTF_CloseFont(font_);
        if (window_) SDL_DestroyWindow(window_);
        if (TTF_WasInit()) TTF_Quit();
        SDL_Quit();
    }
    void init() {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) throw std::runtime_error(SDL_GetError());
        if (TTF_Init() < 0) throw std::runtime_error(TTF_GetError());
        const char *overrideFont = std::getenv("LILSMOKE_TERMINAL_FONT");
        const char *paths[] = {overrideFont,
            "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
            "/usr/share/fonts/DejaVuSansMono.ttf"};
        const int size = dimension("LILSMOKE_TERMINAL_FONT_SIZE", 16, 10, 24);
        for (const char *path : paths) {
            if (path && *path) font_ = TTF_OpenFont(path, size);
            if (font_) break;
        }
        if (!font_ || !TTF_FontFaceIsFixedWidth(font_))
            throw std::runtime_error("A monospace DejaVuSansMono.ttf font is required");
        int minimum, maximum, y0, y1;
        TTF_GlyphMetrics(font_, 'M', &minimum, &maximum, &y0, &y1, &cellW_);
        cellH_ = TTF_FontLineSkip(font_);
        if (cellW_ < 1 || cellH_ < 1) throw std::runtime_error("Invalid font metrics");
        const bool windowed = std::getenv("LILSMOKE_TERMINAL_WINDOWED") != nullptr;
        int width = dimension("LILSMOKE_SCREEN_WIDTH", 640, 320, 1920);
        int height = dimension("LILSMOKE_SCREEN_HEIGHT", 480, 240, 1080);
        const Uint32 flags = windowed ? SDL_WINDOW_RESIZABLE : SDL_WINDOW_FULLSCREEN_DESKTOP;
        if (!windowed) {
            SDL_DisplayMode mode{};
            if (SDL_GetCurrentDisplayMode(0, &mode) == 0 || SDL_GetDesktopDisplayMode(0, &mode) == 0) {
                if (mode.w < 320 || mode.h < 200 || mode.w > 1920 || mode.h > 1080)
                    throw std::runtime_error("Display mode exceeds terminal bounds (320x200 to 1920x1080)");
                width = mode.w;
                height = mode.h;
            }
        }
        window_ = SDL_CreateWindow("LilSmoke Shell", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
            width, height, flags);
        if (!window_) throw std::runtime_error(SDL_GetError());
        SDL_ShowCursor(SDL_DISABLE);
        refresh();
        SDL_StartTextInput();
    }
    void refresh() {
        int width = 0, height = 0;
        SDL_GetWindowSize(window_, &width, &height);
        if (width < 1 || height < 1 || width > 1920 || height > 1080)
            throw std::runtime_error("Terminal window exceeds bounded surface size");
        surface_ = SDL_GetWindowSurface(window_);
        if (!surface_) throw std::runtime_error(SDL_GetError());
        rows_ = std::clamp((surface_->h - header()) / cellH_, 1, lilsmoke::Terminal::MaxRows);
        cols_ = std::clamp(surface_->w / cellW_, 1, lilsmoke::Terminal::MaxCols);
        previous_.clear();
        SDL_FillRect(surface_, nullptr, SDL_MapRGB(surface_->format, 18, 21, 20));
    }
    int rows() const { return rows_; }
    int cols() const { return cols_; }
    int header() const { return cellH_ + 6; }
    void draw(lilsmoke::Terminal &terminal, const std::string &status) {
        const bool full = previous_.size() != static_cast<size_t>(rows_ * cols_);
        if (full) previous_.resize(rows_ * cols_);
        std::vector<SDL_Rect> damage;
        SDL_Rect bar{0, 0, surface_->w, header()};
        SDL_FillRect(surface_, &bar, SDL_MapRGB(surface_->format, 32, 51, 42));
        label("LilSmoke Shell" + (status.empty() ? "" : " | " + status), 6, 3, surface_->w - 12);
        damage.push_back(bar);
        const VTermPos cursor = terminal.cursor();
        const bool blink = (SDL_GetTicks() / 600) % 2 == 0;
        for (int row = 0; row < rows_; ++row) {
            bool changed = full;
            for (int col = 0; col < cols_; ++col) {
                VTermScreenCell cell{};
                if (!terminal.cell(row, col, cell)) continue;
                const size_t index = row * cols_ + col;
                const bool atCursor = terminal.cursorVisible() && cursor.row == row && cursor.col == col;
                const bool wasCursor = lastCursorVisible_ && lastCursor_.row == row && lastCursor_.col == col;
                if (!full && !atCursor && !wasCursor && !cell.attrs.blink &&
                    std::memcmp(&cell, &previous_[index], sizeof(cell)) == 0) continue;
                previous_[index] = cell;
                changed = true;
                if (cell.chars[0] == UINT32_MAX) continue;
                auto fg = terminal.rgb(cell.fg), bg = terminal.rgb(cell.bg);
                if (cell.attrs.reverse) std::swap(fg, bg);
                const int width = std::max(1, static_cast<int>(cell.width));
                SDL_Rect rect{col * cellW_, header() + row * cellH_, width * cellW_, cellH_};
                SDL_FillRect(surface_, &rect, SDL_MapRGB(surface_->format, bg.rgb.red, bg.rgb.green, bg.rgb.blue));
                if (cell.chars[0] && cell.chars[0] != UINT32_MAX && !cell.attrs.conceal &&
                    (!cell.attrs.blink || blink)) {
                    std::string text;
                    for (uint32_t cp : cell.chars) { if (!cp) break; utf8(text, cp); }
                    const int style = (cell.attrs.bold ? TTF_STYLE_BOLD : 0) |
                        (cell.attrs.italic ? TTF_STYLE_ITALIC : 0);
                    if (SDL_Surface *glyph = raster(text, style)) {
                        SDL_SetSurfaceColorMod(glyph, fg.rgb.red, fg.rgb.green, fg.rgb.blue);
                        SDL_SetClipRect(surface_, &rect);
                        SDL_Rect dest{rect.x, rect.y, glyph->w, glyph->h};
                        SDL_BlitSurface(glyph, nullptr, surface_, &dest);
                        SDL_SetClipRect(surface_, nullptr);
                    }
                }
                if (cell.attrs.underline || cell.attrs.strike) {
                    SDL_Rect line{rect.x, rect.y + (cell.attrs.strike ? cellH_ / 2 : cellH_ - 2), rect.w, 1};
                    SDL_FillRect(surface_, &line, SDL_MapRGB(surface_->format, fg.rgb.red, fg.rgb.green, fg.rgb.blue));
                }
                if (atCursor && blink) {
                    SDL_Rect line{rect.x, rect.y + cellH_ - 2, cellW_, 2};
                    SDL_FillRect(surface_, &line, SDL_MapRGB(surface_->format, 119, 221, 153));
                }
            }
            if (changed) damage.push_back({0, header() + row * cellH_, surface_->w, cellH_});
        }
        lastCursor_ = cursor;
        lastCursorVisible_ = terminal.cursorVisible();
        if (full) {
            if (SDL_UpdateWindowSurface(window_) < 0) throw std::runtime_error(SDL_GetError());
        } else if (SDL_UpdateWindowSurfaceRects(window_, damage.data(), static_cast<int>(damage.size())) < 0)
            throw std::runtime_error(SDL_GetError());
        terminal.painted();
    }
    void error(const std::string &error) {
        SDL_FillRect(surface_, nullptr, SDL_MapRGB(surface_->format, 18, 21, 20));
        label("LilSmoke Shell", 8, 8, surface_->w - 16);
        // Wrap diagnostics into the actual surface, including narrow devices.
        const size_t width = std::max(1, cols_ - 2);
        int y = header() + 8;
        for (size_t pos = 0; pos < error.size() && y + cellH_ <= surface_->h; pos += width, y += cellH_)
            label(error.substr(pos, width), 8, y, surface_->w - 16);
        SDL_UpdateWindowSurface(window_);
    }
private:
    struct Glyph { SDL_Surface *surface; uint64_t use; size_t bytes; };
    SDL_Surface *raster(const std::string &text, int style) {
        std::string key(1, static_cast<char>(style));
        key += text;
        auto found = glyphs_.find(key);
        if (found != glyphs_.end()) { found->second.use = ++clock_; return found->second.surface; }
        TTF_SetFontStyle(font_, style);
        SDL_Surface *glyph = TTF_RenderUTF8_Blended(font_, text.c_str(), {255, 255, 255, 255});
        if (!glyph) return nullptr;
        const size_t bytes = static_cast<size_t>(glyph->pitch) * glyph->h;
        constexpr size_t limit = 2 * 1024 * 1024;
        if (bytes > limit) { SDL_FreeSurface(glyph); return nullptr; }
        while (!glyphs_.empty() && (glyphs_.size() >= 512 || cacheBytes_ + bytes > limit)) {
            auto oldest = glyphs_.begin();
            for (auto it = glyphs_.begin(); it != glyphs_.end(); ++it)
                if (it->second.use < oldest->second.use) oldest = it;
            cacheBytes_ -= oldest->second.bytes;
            SDL_FreeSurface(oldest->second.surface);
            glyphs_.erase(oldest);
        }
        cacheBytes_ += bytes;
        glyphs_.emplace(std::move(key), Glyph{glyph, ++clock_, bytes});
        return glyph;
    }
    void label(const std::string &text, int x, int y, int width) {
        TTF_SetFontStyle(font_, TTF_STYLE_NORMAL);
        SDL_Surface *label = TTF_RenderUTF8_Blended(font_, text.c_str(), {216, 237, 222, 255});
        if (!label) return;
        SDL_Rect clip{x, y, width, cellH_};
        SDL_SetClipRect(surface_, &clip);
        SDL_Rect destination{x, y, label->w, label->h};
        SDL_BlitSurface(label, nullptr, surface_, &destination);
        SDL_SetClipRect(surface_, nullptr);
        SDL_FreeSurface(label);
    }
    SDL_Window *window_ = nullptr;
    SDL_Surface *surface_ = nullptr;
    TTF_Font *font_ = nullptr;
    int cellW_ = 1, cellH_ = 1, rows_ = 1, cols_ = 1;
    std::map<std::string, Glyph> glyphs_;
    uint64_t clock_ = 0;
    size_t cacheBytes_ = 0;
    std::vector<VTermScreenCell> previous_;
    VTermPos lastCursor_{};
    bool lastCursorVisible_ = false;
};

int application() {
    Display display;
    display.init();
    lilsmoke::Terminal terminal(display.rows(), display.cols());
    lilsmoke::Keyboard keyboard;
    lilsmoke::Pty pty;
    std::string error;
    bool failed = !pty.start(terminal.rows(), terminal.cols(), error);
    if (!failed) {
        const char banner[] = "\033[1;32mLilSmoke\033[0m\r\n";
        terminal.feed(banner, sizeof(banner) - 1);
    }
    Uint32 lastPaint = 0, childExitAt = 0, overflowUntil = 0;
    int lastBlink = -1;
    bool childExited = false;
    while (!stopping) {
        SDL_Event event;
        // Also bound event processing: repeated keyboard input cannot starve PTY I/O.
        for (int count = 0; count < 128 && SDL_PollEvent(&event); ++count) {
            if (event.type == SDL_QUIT) return 0;
            if (failed) {
                if (event.type == SDL_KEYDOWN && (event.key.keysym.sym == SDLK_RETURN ||
                    event.key.keysym.sym == SDLK_ESCAPE)) return 1;
                continue;
            }
            if (event.type == SDL_WINDOWEVENT) {
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    display.refresh();
                    terminal.resize(display.rows(), display.cols());
                    if (!pty.resize(terminal.rows(), terminal.cols(), error)) failed = true;
                }
                if (event.window.event == SDL_WINDOWEVENT_EXPOSED) {
                    display.refresh();
                    terminal.invalidate();
                }
            }
            if (event.type == SDL_MOUSEWHEEL) {
                const int direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;
                terminal.scroll(std::clamp(event.wheel.y, -128, 128) * 3 * direction);
            }
            keyboard.event(event, terminal);
        }
        if (failed) {
            pty.stop();
            display.error(error);
            SDL_Delay(30);
            continue;
        }
        if (!pty.pump(terminal, error)) { failed = true; continue; }
        const Uint32 now = SDL_GetTicks();
        if (terminal.takeOverflow()) {
            overflowUntil = now + 2000;
            std::fprintf(stderr, "LilSmoke Shell: input queue full, key/reply rejected\n");
        }
        if (pty.exited() && !childExited) { childExited = true; childExitAt = now; }
        if (childExited && (pty.eof() || now - childExitAt >= 200)) return 0;
        if (pty.eof() && !childExited) return 0;
        const int blink = now / 600;
        if (now - lastPaint >= 33 && (terminal.dirty() || blink != lastBlink)) {
            std::string status;
            if (static_cast<Sint32>(overflowUntil - now) > 0) status = "Input buffer full";
            else if (terminal.scrollOffset()) status = "scrollback " + std::to_string(terminal.scrollOffset());
            display.draw(terminal, status);
            lastPaint = now; lastBlink = blink;
        }
        pollfd descriptor{pty.fd(), static_cast<short>(POLLIN | (terminal.output().size() ? POLLOUT : 0)), 0};
        // SDL's DirectFB event source is not a pollable fd. Check it at <=10ms.
        poll(&descriptor, 1, 10);
    }
    return 0;
}
}

int main(int argc, char **argv) {
    if (argc == 2 && std::strcmp(argv[1], "--check-pty") == 0) {
        lilsmoke::Pty pty;
        std::string error;
        if (!pty.start(24, 80, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
        lilsmoke::Terminal terminal(24, 80);
        terminal.output().append("exit\n", 5);
        for (int i = 0; i < 100; ++i) {
            if (!pty.pump(terminal, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
            if (pty.exited()) { std::puts("PTY, controlling tty and /bin/sh -i: OK"); return 0; }
            SDL_Delay(10);
        }
        std::fprintf(stderr, "PTY shell did not exit\n"); return 1;
    }
    struct sigaction action{};
    action.sa_handler = stopSignal;
    sigemptyset(&action.sa_mask);
    for (int sig : {SIGTERM, SIGHUP, SIGINT}) sigaction(sig, &action, nullptr);
    try { return application(); }
    catch (const std::exception &error) { std::fprintf(stderr, "LilSmoke Shell: %s\n", error.what()); return 1; }
}
