// LilSmokeUI — Font.h
//
// Uses SDL2_ttf for text rendering. Loads a default font at startup
// (/usr/share/fonts/dejavu/DejaVuSans.ttf in the Buildroot image).

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct _TTF_Font;
typedef struct _TTF_Font TTF_Font;

namespace lilsmoke {

class Renderer;

class Font {
public:
    Font();
    ~Font();

    // Load a font from path at given point size.
    bool load(const std::string &path, int ptSize);

    // Whether load() succeeded.
    bool loaded() const { return font_ != nullptr; }

    // Render text via the given renderer. Color is 0xRRGGBBAA.
    void render(Renderer &r, const std::string &text, int x, int y, uint32_t color);

    // Get text dimensions (in pixels) for the current font.
    void textSize(const std::string &text, int &w, int &h);

    // Theme-aware text rendering. Theme font sizes are pixel heights derived
    // from a normalized fraction of the display height.
    void renderStyled(Renderer &r,
                      const std::string &text,
                      int x,
                      int y,
                      int w,
                      int h,
                      uint32_t color,
                      const std::string &fontPath,
                      int pixelSize,
                      const std::string &alignment = "left",
                      const std::string &verticalAlignment = "top",
                      bool wrap = false,
                      float lineSpacing = 1.0f,
                      double rotation = 0.0);
    void textSizeStyled(const std::string &text,
                        const std::string &fontPath,
                        int pixelSize,
                        int &w,
                        int &h);

private:
    friend struct FontTestAccess;
    struct Face {
        TTF_Font *font = nullptr;
        uint64_t id = 0;
        uint64_t lastUse = 0;
    };
    TTF_Font *font_ = nullptr;
    uint64_t fontID_ = 0;
    std::string defaultPath_;
    int defaultSize_ = 16;
    std::unordered_map<std::string, Face> cache_;
    uint64_t cacheClock_ = 0;

    struct LayoutKey {
        uint64_t font;
        int width;
        bool wrap;
        std::string text;
        bool operator==(const LayoutKey &o) const {
            return font == o.font && width == o.width && wrap == o.wrap && text == o.text;
        }
    };
    struct LayoutHash {
        size_t operator()(const LayoutKey &k) const {
            return std::hash<std::string>{}(k.text) ^ std::hash<uint64_t>{}(k.font) ^
                std::hash<int>{}(k.width) ^ static_cast<size_t>(k.wrap);
        }
    };
    struct Layout {
        std::vector<std::string> lines;
        int width = 0, height = 0;
        size_t bytes = 0;
        uint64_t lastUse = 0;
    };
    std::unordered_map<LayoutKey, Layout, LayoutHash> layouts_;
    size_t layoutBytes_ = 0;
    uint64_t layoutClock_ = 0;
    static constexpr size_t layoutLimitBytes_ = 64U * 1024U;
    Layout measure(const Face &face, const std::string &text);
    const Layout *findLayout(const LayoutKey &key);
    const Layout *storeLayout(LayoutKey key, Layout layout);

    Face fontFor(const std::string &path, int pixelSize);
};

}  // namespace lilsmoke
