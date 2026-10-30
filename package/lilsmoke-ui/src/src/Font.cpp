// LilSmokeUI — Font.cpp

#include "Font.h"
#include "Renderer.h"
#include "Log.h"

#include <SDL2/SDL_ttf.h>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace lilsmoke {

// Font addresses can be reused after reload/eviction; texture keys must not be.
static uint64_t nextFontID = 0;

Font::Font() = default;

Font::~Font() {
    for (auto &entry : cache_) {
        if (entry.second.font) TTF_CloseFont(entry.second.font);
    }
    cache_.clear();
    if (font_) TTF_CloseFont(font_);
}

bool Font::load(const std::string &path, int ptSize) {
    layouts_.clear();
    layoutBytes_ = 0;
    if (TTF_WasInit() == 0) {
        if (TTF_Init() < 0) {
            LOG_ERROR("TTF_Init failed: %s", TTF_GetError());
            return false;
        }
    }
    for (auto &entry : cache_) {
        if (entry.second.font) TTF_CloseFont(entry.second.font);
    }
    cache_.clear();
    if (font_) TTF_CloseFont(font_);
    font_ = nullptr;

    defaultPath_ = path;
    defaultSize_ = ptSize;

    font_ = TTF_OpenFont(path.c_str(), ptSize);
    fontID_ = ++nextFontID;
    if (!font_) {
        LOG_ERROR("TTF_OpenFont(%s, %d) failed: %s", path.c_str(), ptSize, TTF_GetError());
        return false;
    }

    LOG_INFO("Font: loaded %s @ %dpt", path.c_str(), ptSize);
    return true;
}

void Font::render(Renderer &r, const std::string &text, int x, int y, uint32_t color) {
    if (!font_ || text.empty()) return;

    int surfW = 0, surfH = 0;
    bool temporary = false;
    SDL_Texture *tex = r.textTexture(fontID_, font_, text, color, surfW, surfH, temporary);
    if (!tex) return;
    r.drawTexture(tex, x, y, surfW, surfH);
    if (temporary) SDL_DestroyTexture(tex);
}

void Font::textSize(const std::string &text, int &w, int &h) {
    if (!font_) { w = 0; h = 0; return; }
    const auto result = measure({font_, fontID_, 0}, text);
    w = result.width; h = result.height;
}

const Font::Layout *Font::findLayout(const LayoutKey &key) {
    auto it = layouts_.find(key);
    if (it == layouts_.end()) return nullptr;
    it->second.lastUse = ++layoutClock_;
    return &it->second;
}

const Font::Layout *Font::storeLayout(LayoutKey key, Layout layout) {
    layout.bytes = sizeof(LayoutKey) + sizeof(Layout) + key.text.capacity() + 128;
    layout.bytes += layout.lines.capacity() * sizeof(std::string);
    for (const auto &line : layout.lines) layout.bytes += line.capacity() + 1;
    if (layout.bytes > layoutLimitBytes_) return nullptr;
    while (!layouts_.empty() && (layoutBytes_ + layout.bytes > layoutLimitBytes_ || layouts_.size() >= 128)) {
        auto oldest = layouts_.begin();
        for (auto it = layouts_.begin(); it != layouts_.end(); ++it)
            if (it->second.lastUse < oldest->second.lastUse) oldest = it;
        layoutBytes_ -= oldest->second.bytes;
        layouts_.erase(oldest);
    }
    layout.lastUse = ++layoutClock_;
    layoutBytes_ += layout.bytes;
    return &layouts_.emplace(std::move(key), std::move(layout)).first->second;
}

Font::Layout Font::measure(const Face &face, const std::string &text) {
    LayoutKey key{face.id, -1, false, text};
    if (const auto *cached = findLayout(key)) {
        Layout result; result.width = cached->width; result.height = cached->height;
        return result;
    }
    Layout result;
    if (TTF_SizeUTF8(face.font, text.c_str(), &result.width, &result.height) != 0)
        result.width = result.height = 0;
    storeLayout(std::move(key), result);
    return result;
}

Font::Face Font::fontFor(const std::string &path, int pixelSize) {
    if (pixelSize < 6) pixelSize = 6;
    if (pixelSize > 160) pixelSize = 160;
    const std::string resolved = path.empty() ? defaultPath_ : path;
    if (font_ && resolved == defaultPath_ && pixelSize == defaultSize_)
        return {font_, fontID_, 0};

    const std::string key = resolved + "#" + std::to_string(pixelSize);
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        it->second.lastUse = ++cacheClock_;
        return it->second;
    }

    if (cache_.size() >= 16) {
        auto oldest = cache_.begin();
        for (auto entry = cache_.begin(); entry != cache_.end(); ++entry)
            if (entry->second.lastUse < oldest->second.lastUse) oldest = entry;
        if (oldest->second.font) TTF_CloseFont(oldest->second.font);
        cache_.erase(oldest);
    }

    TTF_Font *loaded = TTF_OpenFont(resolved.c_str(), pixelSize);
    if (!loaded && resolved != defaultPath_)
        loaded = TTF_OpenFont(defaultPath_.c_str(), pixelSize);
    if (!loaded) {
        LOG_WARN("TTF_OpenFont(%s, %d) failed: %s",
                 resolved.c_str(), pixelSize, TTF_GetError());
    }
    Face face{loaded, ++nextFontID, ++cacheClock_};
    cache_[key] = face;
    return face;
}

void Font::textSizeStyled(const std::string &text,
                          const std::string &fontPath,
                          int pixelSize,
                          int &w,
                          int &h) {
    const auto face = fontFor(fontPath, pixelSize);
    if (!face.font || text.empty()) {
        w = 0;
        h = 0;
        return;
    }
    const auto result = measure(face, text);
    w = result.width; h = result.height;
}

static std::vector<std::string> wrapLines(TTF_Font *font,
                                          const std::string &text,
                                          int maxWidth,
                                          bool wrap) {
    std::vector<std::string> lines;
    std::istringstream paragraphs(text);
    std::string paragraph;
    while (std::getline(paragraphs, paragraph)) {
        if (!wrap || maxWidth <= 0) {
            lines.push_back(paragraph);
            continue;
        }

        std::istringstream words(paragraph);
        std::string line;
        std::string word;
        while (words >> word) {
            std::string candidate = line.empty() ? word : line + " " + word;
            int width = 0, height = 0;
            TTF_SizeUTF8(font, candidate.c_str(), &width, &height);
            if (!line.empty() && width > maxWidth) {
                lines.push_back(line);
                line = word;
            } else {
                line = candidate;
            }
        }
        lines.push_back(line);
    }
    if (lines.empty()) lines.push_back("");
    return lines;
}

void Font::renderStyled(Renderer &r,
                        const std::string &text,
                        int x,
                        int y,
                        int w,
                        int h,
                        uint32_t color,
                        const std::string &fontPath,
                        int pixelSize,
                        const std::string &alignment,
                        const std::string &verticalAlignment,
                        bool wrap,
                        float lineSpacing,
                        double rotation) {
    if (text.empty()) return;
    const Face face = fontFor(fontPath, pixelSize);
    TTF_Font *font = face.font;
    if (!font) return;
    if (lineSpacing < 0.5f) lineSpacing = 0.5f;
    if (lineSpacing > 3.0f) lineSpacing = 3.0f;

    LayoutKey key{face.id, wrap ? w : 0, wrap, text};
    const Layout *layout = findLayout(key);
    Layout uncached;
    if (!layout) {
        uncached.lines = wrapLines(font, text, w, wrap);
        layout = storeLayout(std::move(key), uncached);
        if (!layout) layout = &uncached;
    }
    const auto &lines = layout->lines;
    int lineHeight = std::max(1, TTF_FontLineSkip(font));
    int advance = std::max(1, static_cast<int>(lineHeight * lineSpacing));
    int totalHeight = lineHeight + std::max(0, static_cast<int>(lines.size()) - 1) * advance;
    int drawY = y;
    if (h > totalHeight && verticalAlignment == "center")
        drawY += (h - totalHeight) / 2;
    else if (h > totalHeight && (verticalAlignment == "bottom" || verticalAlignment == "right"))
        drawY += h - totalHeight;

    for (const std::string &line : lines) {
        if (line.empty()) {
            drawY += advance;
            continue;
        }
        int textW = 0, textH = 0;
        bool temporary = false;
        SDL_Texture *texture = r.textTexture(face.id, font, line, color, textW, textH, temporary);
        if (!texture) {
            drawY += advance;
            continue;
        }

        int drawX = x;
        if (w > textW && alignment == "center") drawX += (w - textW) / 2;
        else if (w > textW && alignment == "right") drawX += w - textW;
        r.drawTextureEx(texture, drawX, drawY, textW, textH, rotation,
                        textW / 2, textH / 2);
        if (temporary) SDL_DestroyTexture(texture);
        drawY += advance;
        if (h > 0 && drawY >= y + h) break;
    }
}

}  // namespace lilsmoke
