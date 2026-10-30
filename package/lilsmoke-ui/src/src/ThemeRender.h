#pragma once

#include "Theme.h"

#include <cstdint>
#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace lilsmoke {

class Font;
class Renderer;

namespace themeui {

// Scoped software transforms: no extra framebuffer or GPU render target.
class MotionScope {
public:
    MotionScope(uint32_t elapsedMs, int offsetX = 0, int offsetY = 0,
                uint8_t opacity = 255, const char *event = "activate");
    ~MotionScope();
    MotionScope(const MotionScope &) = delete;
    MotionScope &operator=(const MotionScope &) = delete;
private:
    uint32_t elapsed_;
    int x_, y_;
    uint8_t opacity_;
    const char *event_;
    bool enabled_;
};

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

// Per-draw geometry, without cloning or serializing a theme's property map.
struct ElementLayout {
    std::optional<std::array<float, 2>> pos, origin, size, maxSize, rotationOrigin;
    std::optional<float> rotation, fontSize;
    bool ignoreSize = false;
    bool ignoreColor = false;
};

// Image quad (or text layout area) and the clip used by its actual draw.
struct PaintedRegion {
    Rect rect;
    Rect clip;
    double pivotX = 0, pivotY = 0;
    double cosine = 1, sine = 0;
    bool contains(int x, int y) const;
};

bool visible(const ThemeElement &element);
void propertyPair(const ThemeElement &element, const char *property,
                  float &x, float &y, float defaultX = 0, float defaultY = 0);
void parsePairValue(const std::string &value, float &x, float &y, float defaultX, float defaultY);
float zIndex(const ThemeElement &element, float fallback);
Rect elementRect(const ThemeElement &element,
                 Renderer &renderer,
                 float defaultX,
                 float defaultY,
                 float defaultW,
                 float defaultH);

void drawImage(Renderer &renderer,
               const ThemeElement &element,
               const std::string &overridePath = "",
               float defaultX = 0.0f,
               float defaultY = 0.0f,
               float defaultW = 1.0f,
               float defaultH = 1.0f,
               uint8_t opacity = 255,
               PaintedRegion *painted = nullptr,
               const ElementLayout *layout = nullptr);

void drawText(Renderer &renderer,
              Font &font,
              const ThemeElement &element,
              const std::string &overrideText = "",
              uint32_t defaultColor = 0xFFFFFFFF,
              float defaultX = 0.0f,
              float defaultY = 0.0f,
              float defaultW = 0.0f,
              float defaultH = 0.0f,
              PaintedRegion *painted = nullptr,
              const ElementLayout *layout = nullptr);

void drawExtras(Renderer &renderer,
                Font &font,
                const ThemeView *view,
                float lowerZ,
                float upperZ,
                float defaultZ,
                const std::unordered_map<std::string, std::string> &dynamicText = {});

struct ExtraLayer {
    const ThemeView *view = nullptr;
    Rect clip;
    int offsetX = 0, offsetY = 0;
    uint32_t elapsed = 0;
    uint8_t opacity = 255;
    const char *event = "activate";
    bool fixedBackgrounds = false;
    const std::vector<const ThemeElement *> *stationary = nullptr;
};

// Composite fixed scenery and clipped system pages in their original z order.
void drawSystemExtras(Renderer &renderer, Font &font,
                      const ExtraLayer *layers, size_t layerCount,
                      float lowerZ, float upperZ, float defaultZ);

void drawHelp(Renderer &renderer,
              Font &font,
              const ThemeView *view,
              const std::string &prompts);

void drawScreenOverlay(Renderer &renderer,
                       Font &font,
                       const Theme &theme);

std::string upper(std::string value);

}  // namespace themeui
}  // namespace lilsmoke
