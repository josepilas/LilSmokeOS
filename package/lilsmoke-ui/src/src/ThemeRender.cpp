#include "ThemeRender.h"
#include "ThemeAnimation.h"

#include "Font.h"
#include "Renderer.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <vector>

namespace lilsmoke {
namespace themeui {

namespace {

struct MotionContext {
    uint32_t elapsed = 0;
    int x = 0, y = 0;
    uint8_t opacity = 255;
    const char *event = "activate";
    bool enabled = false;
};
static MotionContext motion;

struct PropertyKey {
    std::string value;
    uint32_t a, b;
    uint8_t kind;
    bool operator==(const PropertyKey &other) const {
        return a == other.a && b == other.b && kind == other.kind && value == other.value;
    }
};
struct PropertyHash {
    size_t operator()(const PropertyKey &key) const {
        return std::hash<std::string>{}(key.value) ^ key.a ^ (size_t(key.b) << 1) ^ key.kind;
    }
};
struct PropertyValue { uint32_t a, b; uint64_t used; };
static std::unordered_map<PropertyKey, PropertyValue, PropertyHash> propertyCache;
static uint64_t propertyClock;
static uint32_t floatBits(float value) { uint32_t bits; std::memcpy(&bits, &value, 4); return bits; }
static float bitsFloat(uint32_t bits) { float value; std::memcpy(&value, &bits, 4); return value; }
static PropertyValue *cachedProperty(const PropertyKey &key) {
    const auto found = propertyCache.find(key);
    if (found == propertyCache.end()) return nullptr;
    found->second.used = ++propertyClock;
    return &found->second;
}
static void storeProperty(PropertyKey key, uint32_t a, uint32_t b = 0) {
    if (key.value.size() > 128) return;
    if (propertyCache.size() >= 64) {
        auto oldest = propertyCache.begin();
        for (auto it = propertyCache.begin(); it != propertyCache.end(); ++it)
            if (it->second.used < oldest->second.used) oldest = it;
        propertyCache.erase(oldest);
    }
    propertyCache.emplace(std::move(key), PropertyValue{a, b, ++propertyClock});
}
static float propertyFloat(const ThemeElement &element, const char *property, float fallback) {
    const auto found = element.props.find(property);
    if (found == element.props.end()) return fallback;
    PropertyKey key{found->second, floatBits(fallback), 0, 2};
    if (const auto *value = cachedProperty(key)) return bitsFloat(value->a);
    const float value = element.getFloat(property, fallback);
    storeProperty(std::move(key), floatBits(value));
    return value;
}
static uint32_t propertyColor(const ThemeElement &element, const char *property, uint32_t fallback) {
    const auto found = element.props.find(property);
    if (found == element.props.end()) return fallback;
    PropertyKey key{found->second, fallback, 0, 3};
    if (const auto *value = cachedProperty(key)) return value->a;
    const uint32_t value = element.getColor(property, fallback);
    storeProperty(std::move(key), value);
    return value;
}

static float animatedRotation(const ThemeElement &element, float base) {
    using P = ThemeAnimation::Property;
    using namespace animation_detail;
    float value = boundedValue(P::Rotation, base);
    const size_t count = std::min(element.animations.size(),
                                  static_cast<size_t>(ThemeAnimation::MaxTracks));
    bool named = false, assigned = false;
    uint32_t lastBegin = 0;
    for (size_t i = 0; i < count; ++i)
        if (element.animations[i].event == motion.event) named = true;
    for (size_t i = 0; i < count; ++i) {
        const auto &track = element.animations[i];
        if (track.property != P::Rotation ||
            (named ? track.event != motion.event : !track.event.empty())) continue;
        const uint32_t begin = std::min(track.beginMs, ThemeAnimation::MaxTimeMs);
        const uint32_t duration = std::min(track.durationMs, ThemeAnimation::MaxTimeMs);
        const uint32_t span = trackDuration(track);
        const uint32_t boardSpan = std::max(begin + span,
            std::min(track.storyboardDurationMs, ThemeAnimation::MaxStoryboardMs));
        const uint32_t boardTotal = boardSpan * repeatCount(track.storyboardRepeat);
        uint32_t time = motion.elapsed;
        if (boardSpan) time = motion.elapsed >= boardTotal ? boardSpan : motion.elapsed % boardSpan;
        if (time < begin || (assigned && begin < lastBegin)) continue;
        assigned = true; lastBegin = begin;
        const uint32_t local = time - begin;
        float t = track.autoReverse ? 0.0f : 1.0f;
        if (duration && local < span) {
            const uint32_t cycle = duration * (track.autoReverse ? 2u : 1u);
            const uint32_t phase = local % cycle;
            t = phase / static_cast<float>(duration);
            if (track.autoReverse && phase > duration)
                t = (cycle - phase) / static_cast<float>(duration);
        }
        const float from = track.hasFrom ? boundedValue(P::Rotation, track.from, base) : base;
        const float to = track.hasTo ? boundedValue(P::Rotation, track.to, base) : base;
        value = boundedValue(P::Rotation, from + (to - from) * ease(t, track.mode), base);
    }
    return value;
}

static ThemeAnimationValues animated(const ThemeElement &element, float x, float y,
                                     const ElementLayout *layout = nullptr) {
    if (!motion.enabled || element.animations.empty()) {
        using P = ThemeAnimation::Property;
        using animation_detail::boundedValue;
        return {motion.enabled ? boundedValue(P::X, x) : x,
            motion.enabled ? boundedValue(P::Y, y) : y,
            boundedValue(P::Scale, propertyFloat(element, "scale", 1), 1),
            boundedValue(P::Opacity, propertyFloat(element, "opacity", 1), 1),
            boundedValue(P::Rotation, layout && layout->rotation ? *layout->rotation
                : propertyFloat(element, "rotation", 0)), false};
    }
    auto values = sampleAnimation(element, motion.elapsed, motion.event, x, y);
    if (layout && layout->rotation)
        values.rotation = animatedRotation(element,
            animation_detail::boundedValue(ThemeAnimation::Property::Rotation, *layout->rotation));
    return values;
}

static void animateRect(const ThemeElement &element, Renderer &renderer,
                        Rect &rect, const ThemeAnimationValues &values,
                        float baseX, float baseY, const ElementLayout *layout = nullptr) {
    rect.x += motion.x; rect.y += motion.y;
    if (floatBits(values.x) != floatBits(baseX))
        rect.x += static_cast<int>(std::lround((values.x - baseX) * renderer.width()));
    if (floatBits(values.y) != floatBits(baseY))
        rect.y += static_cast<int>(std::lround((values.y - baseY) * renderer.height()));
    if (floatBits(values.scale) == floatBits(1.0f)) return;
    float anchorX = 0.5f, anchorY = 0.5f;
    if (layout && layout->origin) {
        anchorX = (*layout->origin)[0]; anchorY = (*layout->origin)[1];
    } else propertyPair(element, "origin", anchorX, anchorY, 0.5f, 0.5f);
    const int w = std::max(0, static_cast<int>(std::lround(rect.w * values.scale)));
    const int h = std::max(0, static_cast<int>(std::lround(rect.h * values.scale)));
    rect.x += static_cast<int>(std::lround((rect.w - w) * anchorX));
    rect.y += static_cast<int>(std::lround((rect.h - h) * anchorY));
    rect.w = w;
    rect.h = h;
}

static void parsePair(const std::string &value,
                      float &a,
                      float &b,
                      float defaultA,
                      float defaultB) {
    a = defaultA;
    b = defaultB;
    if (value.empty()) return;
    parsePairValue(value, a, b, defaultA, defaultB);
}

static bool isExtra(const ThemeElement &element) {
    std::string value = element.get("extra");
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value == "true" || value == "1" || value == "yes" ||
           value == "static";
}

static Rect imageRectUncached(const ThemeElement &element,
                      Renderer &renderer,
                      SDL_Texture *texture,
                      float defaultX,
                      float defaultY,
                      float defaultW,
                      float defaultH,
                      const ElementLayout *layout = nullptr) {
    float x = defaultX, y = defaultY, originX = 0.0f, originY = 0.0f;
    if (layout && layout->pos) { x = (*layout->pos)[0]; y = (*layout->pos)[1]; }
    else propertyPair(element, "pos", x, y, defaultX, defaultY);
    if (layout && layout->origin) { originX = (*layout->origin)[0]; originY = (*layout->origin)[1]; }
    else propertyPair(element, "origin", originX, originY, 0.0f, 0.0f);

    int textureW = 1, textureH = 1;
    renderer.textureSize(texture, textureW, textureH);
    float normalizedAspect = 0;
    bool aspectReady = false;
    const auto aspect = [&]() {
        if (!aspectReady) {
            normalizedAspect = (static_cast<float>(textureW) / std::max(1, textureH)) *
                (static_cast<float>(renderer.height()) / renderer.width());
            aspectReady = true;
        }
        return normalizedAspect;
    };

    float width = defaultW;
    float height = defaultH;
    const bool hasSize = layout && layout->size ? true
        : !(layout && layout->ignoreSize) && !element.get("size").empty();
    if (hasSize) {
        if (layout && layout->size) { width = (*layout->size)[0]; height = (*layout->size)[1]; }
        else propertyPair(element, "size", width, height, defaultW, defaultH);
        if (width <= 0.0f && height > 0.0f) width = height * aspect();
        if (height <= 0.0f && width > 0.0f)
            height = width / std::max(0.001f, aspect());
    }

    float maxW = 0.0f, maxH = 0.0f;
    if (layout && layout->maxSize) { maxW = (*layout->maxSize)[0]; maxH = (*layout->maxSize)[1]; }
    else parsePair(element.get("maxsize"), maxW, maxH, 0.0f, 0.0f);
    if (maxW > 0.0f || maxH > 0.0f) {
        if (!hasSize) {
            if (maxW <= 0.0f) {
                height = maxH;
                width = height * aspect();
            } else {
                width = maxW;
                height = width / std::max(0.001f, aspect());
                if (maxH > 0.0f && height > maxH) {
                    height = maxH;
                    width = height * aspect();
                }
            }
        } else if (width > 0.0f && height > 0.0f) {
            float scale = 1.0f;
            if (maxW > 0.0f) scale = std::min(scale, maxW / width);
            if (maxH > 0.0f) scale = std::min(scale, maxH / height);
            if (scale < 1.0f) {
                width *= scale;
                height *= scale;
            }
        }
    }

    float minW = 0.0f, minH = 0.0f;
    parsePair(element.get("minsize"), minW, minH, 0.0f, 0.0f);
    if ((minW > 0.0f || minH > 0.0f) && !hasSize) {
        width = minW > 0.0f ? minW : minH * aspect();
        height = width / std::max(0.001f, aspect());
        if (minH > 0.0f && height < minH) {
            height = minH;
            width = height * aspect();
        }
    }

    int pixelW = std::max(1, static_cast<int>(std::round(width * renderer.width())));
    int pixelH = std::max(1, static_cast<int>(std::round(height * renderer.height())));
    return {
        static_cast<int>(std::round(x * renderer.width() - originX * pixelW)),
        static_cast<int>(std::round(y * renderer.height() - originY * pixelH)),
        pixelW,
        pixelH,
    };
}

struct GeometryKey {
    std::array<std::string, 5> properties;
    std::array<uint32_t, 4> defaults;
    std::array<uint32_t, 8> overrides{};
    std::array<int, 4> dimensions;
    unsigned present = 0, numeric = 0;
    bool operator==(const GeometryKey &o) const {
        return present == o.present && numeric == o.numeric && overrides == o.overrides &&
            defaults == o.defaults && dimensions == o.dimensions && properties == o.properties;
    }
};
struct GeometryHash {
    size_t operator()(const GeometryKey &key) const {
        size_t hash = key.present | (key.numeric << 8);
        for (const auto &property : key.properties) hash = hash * 31 + std::hash<std::string>{}(property);
        for (const auto value : key.defaults) hash = hash * 31 + value;
        for (const auto value : key.overrides) hash = hash * 31 + value;
        for (const auto value : key.dimensions) hash = hash * 31 + static_cast<unsigned>(value);
        return hash;
    }
};
struct GeometryValue { Rect rect; uint64_t used; };
static std::unordered_map<GeometryKey, GeometryValue, GeometryHash> geometryCache;
static uint64_t geometryClock;

static Rect imageRect(const ThemeElement &element, Renderer &renderer, SDL_Texture *texture,
                      float defaultX, float defaultY, float defaultW, float defaultH,
                      const ElementLayout *layout = nullptr) {
    GeometryKey key;
    static const char *names[] = {"pos", "size", "origin", "maxsize", "minsize"};
    const std::optional<std::array<float, 2>> *overrides[] = {
        layout ? &layout->pos : nullptr, layout ? &layout->size : nullptr,
        layout ? &layout->origin : nullptr, layout ? &layout->maxSize : nullptr};
    for (size_t i = 0; i < key.properties.size(); ++i) {
        if (i < 4 && overrides[i] && *overrides[i]) {
            key.numeric |= 1U << i;
            key.present |= 1U << i;
            key.overrides[i * 2] = floatBits((**overrides[i])[0]);
            key.overrides[i * 2 + 1] = floatBits((**overrides[i])[1]);
            continue;
        }
        if (i == 1 && layout && layout->ignoreSize) continue;
        const auto property = element.props.find(names[i]);
        if (property == element.props.end()) continue;
        if (property->second.size() > 64)
            return imageRectUncached(element, renderer, texture, defaultX, defaultY, defaultW, defaultH, layout);
        key.present |= 1U << i;
        key.properties[i] = property->second;
    }
    int textureW, textureH; renderer.textureSize(texture, textureW, textureH);
    key.dimensions = {renderer.width(), renderer.height(), textureW, textureH};
    key.defaults = {floatBits(defaultX), floatBits(defaultY), floatBits(defaultW), floatBits(defaultH)};
    const auto found = geometryCache.find(key);
    if (found != geometryCache.end()) { found->second.used = ++geometryClock; return found->second.rect; }
    const Rect rect = imageRectUncached(element, renderer, texture, defaultX, defaultY, defaultW, defaultH, layout);
    if (geometryCache.size() >= 32) {
        auto oldest = geometryCache.begin();
        for (auto it = geometryCache.begin(); it != geometryCache.end(); ++it)
            if (it->second.used < oldest->second.used) oldest = it;
        geometryCache.erase(oldest);
    }
    geometryCache.emplace(std::move(key), GeometryValue{rect, ++geometryClock});
    return rect;
}

static uint32_t withOpacity(uint32_t color, uint8_t opacity) {
    uint32_t alpha = color & 0xFF;
    alpha = (alpha * opacity) / 255;
    return (color & 0xFFFFFF00U) | alpha;
}

static void captureRegion(Renderer &renderer, Rect rect, double rotation,
                          int centerX, int centerY, PaintedRegion *painted) {
    if (!painted) return;
    SDL_Renderer *sdl = static_cast<SDL_Renderer *>(renderer.sdlRenderer());
    SDL_Rect clip{0, 0, renderer.width(), renderer.height()};
    if (SDL_RenderIsClipEnabled(sdl)) SDL_RenderGetClipRect(sdl, &clip);
    const int left = std::max(0, clip.x), top = std::max(0, clip.y);
    const int right = std::min(renderer.width(), clip.x + clip.w);
    const int bottom = std::min(renderer.height(), clip.y + clip.h);
    double cosine = 1, sine = 0;
    if (rotation != 0) {
        const double radians = rotation * (3.14159265358979323846 / 180.0);
        cosine = std::cos(radians); sine = std::sin(radians);
    }
    *painted = {rect, {left, top, std::max(0, right - left), std::max(0, bottom - top)},
                static_cast<double>(rect.x + centerX), static_cast<double>(rect.y + centerY),
                cosine, sine};
}

struct HelpPromptEntry {
    std::string icon;
    std::string label;
};

static std::string trimAscii(const std::string &value) {
    size_t first = 0;
    while (first < value.size() &&
           std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }
    size_t last = value.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }
    return value.substr(first, last - first);
}

static std::vector<std::string> splitOnSpaceRun(const std::string &value,
                                                 size_t minimumRun) {
    std::vector<std::string> result;
    size_t start = 0;
    size_t cursor = 0;
    while (cursor < value.size()) {
        if (value[cursor] != ' ') {
            ++cursor;
            continue;
        }
        const size_t runStart = cursor;
        while (cursor < value.size() && value[cursor] == ' ') ++cursor;
        if (cursor - runStart < minimumRun) continue;
        std::string part = trimAscii(value.substr(start, runStart - start));
        if (!part.empty()) result.push_back(std::move(part));
        start = cursor;
    }
    std::string tail = trimAscii(value.substr(start));
    if (!tail.empty()) result.push_back(std::move(tail));
    return result;
}

static std::vector<HelpPromptEntry> parseHelpPrompts(
    const std::string &prompts) {
    std::vector<HelpPromptEntry> result;
    for (const std::string &entry : splitOnSpaceRun(prompts, 4)) {
        std::vector<std::string> parts = splitOnSpaceRun(entry, 2);
        if (parts.size() < 2) continue;
        result.push_back({upper(parts.front()), upper(parts.back())});
    }
    return result;
}

static std::string helpIconProperty(const std::string &icon) {
    if (icon == "UP/DOWN") return "iconupdown";
    if (icon == "LEFT/RIGHT") return "iconleftright";
    if (icon == "UP/DOWN/LEFT/RIGHT") return "iconupdownleftright";
    if (icon == "A") return "icona";
    if (icon == "B") return "iconb";
    if (icon == "X") return "iconx";
    if (icon == "Y") return "icony";
    if (icon == "L") return "iconl";
    if (icon == "R") return "iconr";
    if (icon == "START") return "iconstart";
    if (icon == "SELECT") return "iconselect";
    return "";
}

static bool environmentEnabled(const char *name, bool fallback) {
    const char *value = std::getenv(name);
    if (!value || !*value) return fallback;
    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return normalized == "1" || normalized == "true" ||
           normalized == "yes" || normalized == "on";
}

static int currentBatteryPercent() {
    if (const char *overrideValue =
            std::getenv("LILSMOKE_BATTERY_PERCENT")) {
        char *end = nullptr;
        long value = std::strtol(overrideValue, &end, 10);
        if (end != overrideValue && value >= 0 && value <= 100)
            return static_cast<int>(value);
        return -1;
    }

    static std::time_t lastRead = 0;
    static int cachedPercent = -1;
    const std::time_t now = std::time(nullptr);
    if (lastRead != 0 && now - lastRead < 10) return cachedPercent;
    lastRead = now;
    cachedPercent = -1;

    DIR *directory = opendir("/sys/class/power_supply");
    if (!directory) return cachedPercent;
    while (dirent *entry = readdir(directory)) {
        if (entry->d_name[0] == '.') continue;
        const std::string base =
            std::string("/sys/class/power_supply/") + entry->d_name;
        std::ifstream typeFile(base + "/type");
        std::string type;
        std::getline(typeFile, type);
        if (type != "Battery") continue;

        std::ifstream capacityFile(base + "/capacity");
        int value = -1;
        if (capacityFile >> value && value >= 0 && value <= 100) {
            cachedPercent = value;
            break;
        }
    }
    closedir(directory);
    return cachedPercent;
}

static void setSdlDrawColor(Renderer &renderer, uint32_t color) {
    SDL_Renderer *sdl = static_cast<SDL_Renderer *>(renderer.sdlRenderer());
    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl, (color >> 24) & 0xFF,
                           (color >> 16) & 0xFF,
                           (color >> 8) & 0xFF, color & 0xFF);
}

static void drawCircleOutline(Renderer &renderer,
                              int centerX,
                              int centerY,
                              int radius,
                              uint32_t color) {
    SDL_Renderer *sdl = static_cast<SDL_Renderer *>(renderer.sdlRenderer());
    setSdlDrawColor(renderer, color);
    for (int y = -radius; y <= radius; ++y) {
        int x = static_cast<int>(std::round(
            std::sqrt(std::max(0, radius * radius - y * y))));
        SDL_RenderDrawPoint(sdl, centerX - x, centerY + y);
        SDL_RenderDrawPoint(sdl, centerX + x, centerY + y);
    }
}

}  // namespace

void parsePairValue(const std::string &value, float &x, float &y, float defaultX, float defaultY) {
    PropertyKey key{value, floatBits(defaultX), floatBits(defaultY), 1};
    if (const auto *cached = cachedProperty(key)) {
        x = bitsFloat(cached->a); y = bitsFloat(cached->b); return;
    }
    x = defaultX; y = defaultY;
    std::istringstream in(value);
    in >> x;
    if (!(in >> y)) y = x;
    storeProperty(std::move(key), floatBits(x), floatBits(y));
}

void propertyPair(const ThemeElement &element, const char *property,
                  float &x, float &y, float defaultX, float defaultY) {
    const auto found = element.props.find(property);
    if (found == element.props.end()) { x = defaultX; y = defaultY; return; }
    parsePairValue(found->second, x, y, defaultX, defaultY);
}

MotionScope::MotionScope(uint32_t elapsedMs, int offsetX, int offsetY,
                         uint8_t opacity, const char *event)
    : elapsed_(motion.elapsed), x_(motion.x), y_(motion.y),
      opacity_(motion.opacity), event_(motion.event), enabled_(motion.enabled) {
    motion = {elapsedMs, offsetX, offsetY, opacity, event, true};
}
MotionScope::~MotionScope() { motion = {elapsed_, x_, y_, opacity_, event_, enabled_}; }

bool PaintedRegion::contains(int x, int y) const {
    if (rect.w <= 0 || rect.h <= 0 || clip.w <= 0 || clip.h <= 0 ||
        x < clip.x || x >= clip.x + clip.w || y < clip.y || y >= clip.y + clip.h)
        return false;
    if (cosine == 1 && sine == 0)
        return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
    // Invert SDL's clockwise rotation at the pointer pixel's center.
    const double dx = x + 0.5 - pivotX, dy = y + 0.5 - pivotY;
    const double localX = pivotX + dx * cosine + dy * sine;
    const double localY = pivotY - dx * sine + dy * cosine;
    return localX >= rect.x && localX < rect.x + rect.w &&
           localY >= rect.y && localY < rect.y + rect.h;
}

std::string upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

bool visible(const ThemeElement &element) {
    return element.getBool("visible", true);
}

float zIndex(const ThemeElement &element, float fallback) {
    return propertyFloat(element, "zindex", fallback);
}

Rect elementRect(const ThemeElement &element,
                 Renderer &renderer,
                 float defaultX,
                 float defaultY,
                 float defaultW,
                 float defaultH) {
    float x, y, width, height, originX, originY;
    propertyPair(element, "pos", x, y, defaultX, defaultY);
    propertyPair(element, "size", width, height, defaultW, defaultH);
    propertyPair(element, "origin", originX, originY, 0.0f, 0.0f);
    int pixelW = std::max(0, static_cast<int>(std::round(width * renderer.width())));
    int pixelH = std::max(0, static_cast<int>(std::round(height * renderer.height())));
    return {
        static_cast<int>(std::round(x * renderer.width() - originX * pixelW)),
        static_cast<int>(std::round(y * renderer.height() - originY * pixelH)),
        pixelW,
        pixelH,
    };
}

void drawImage(Renderer &renderer,
               const ThemeElement &element,
               const std::string &overridePath,
               float defaultX,
               float defaultY,
               float defaultW,
               float defaultH,
               uint8_t opacity,
               PaintedRegion *painted,
               const ElementLayout *layout) {
    if (painted) *painted = {};
    if (!visible(element)) return;
    float baseX = defaultX, baseY = defaultY;
    if (layout && layout->pos) { baseX = (*layout->pos)[0]; baseY = (*layout->pos)[1]; }
    else propertyPair(element, "pos", baseX, baseY, defaultX, defaultY);
    const auto values = animated(element, baseX, baseY, layout);
    opacity = floatBits(values.opacity) == floatBits(1.0f)
        ? static_cast<uint8_t>((static_cast<unsigned>(opacity) * motion.opacity + 127U) / 255U)
        : static_cast<uint8_t>(std::lround(opacity * values.opacity * motion.opacity / 255.0f));
    if (!opacity || values.scale <= 0) return;
    const std::string path = overridePath.empty() ? element.get("path") : overridePath;
    if (path.empty()) {
        if (!(layout && (layout->size || layout->maxSize)) &&
            element.get("size").empty() && element.get("maxsize").empty()) return;
        Rect rect;
        if (layout) {
            float x = baseX, y = baseY, w = defaultW, h = defaultH, ox = 0, oy = 0;
            if (layout->size) { w = (*layout->size)[0]; h = (*layout->size)[1]; }
            else if (!layout->ignoreSize) propertyPair(element, "size", w, h, defaultW, defaultH);
            if (layout->origin) { ox = (*layout->origin)[0]; oy = (*layout->origin)[1]; }
            else propertyPair(element, "origin", ox, oy, 0, 0);
            const int pw = std::max(0, static_cast<int>(std::round(w * renderer.width())));
            const int ph = std::max(0, static_cast<int>(std::round(h * renderer.height())));
            rect = {static_cast<int>(std::round(x * renderer.width() - ox * pw)),
                    static_cast<int>(std::round(y * renderer.height() - oy * ph)), pw, ph};
        } else rect = elementRect(element, renderer, defaultX, defaultY, defaultW, defaultH);
        animateRect(element, renderer, rect, values, baseX, baseY, layout);
        uint32_t color = withOpacity(propertyColor(element, "color", 0), opacity);
        if ((color & 0xFF) != 0) {
            renderer.fillRect(rect.x, rect.y, rect.w, rect.h, color);
            captureRegion(renderer, rect, 0, 0, 0, painted);
        }
        return;
    }

    SDL_Texture *texture = renderer.loadImage(path);
    if (!texture) return;
    Rect rect = imageRect(element, renderer, texture, defaultX, defaultY, defaultW, defaultH, layout);
    animateRect(element, renderer, rect, values, baseX, baseY, layout);
    uint32_t color = withOpacity(layout && layout->ignoreColor ? 0xFFFFFFFF
        : propertyColor(element, "color", 0xFFFFFFFF), opacity);
    if ((color & 0xFF) == 0) return;
    SDL_SetTextureColorMod(texture, (color >> 24) & 0xFF,
                          (color >> 16) & 0xFF, (color >> 8) & 0xFF);
    SDL_SetTextureAlphaMod(texture, color & 0xFF);

    if (element.getBool("tile", false)) {
        renderer.drawTextureTiled(texture, rect.x, rect.y, rect.w, rect.h);
        captureRegion(renderer, rect, 0, 0, 0, painted);
    } else {
        const double rotation = values.rotation;
        float rotationX = 0.5f, rotationY = 0.5f;
        if (rotation != 0) {
            if (layout && layout->rotationOrigin) {
                rotationX = (*layout->rotationOrigin)[0]; rotationY = (*layout->rotationOrigin)[1];
            } else propertyPair(element, "rotationorigin", rotationX, rotationY, 0.5f, 0.5f);
        }
        const int centerX = static_cast<int>(rotationX * rect.w);
        const int centerY = static_cast<int>(rotationY * rect.h);
        renderer.drawTextureEx(texture, rect.x, rect.y, rect.w, rect.h,
                               rotation, centerX, centerY,
                               element.getBool("flipx", false),
                               element.getBool("flipy", false));
        captureRegion(renderer, rect, rotation, centerX, centerY, painted);
    }
    SDL_SetTextureColorMod(texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(texture, 255);
}

void drawText(Renderer &renderer,
              Font &font,
              const ThemeElement &element,
              const std::string &overrideText,
              uint32_t defaultColor,
              float defaultX,
              float defaultY,
              float defaultW,
              float defaultH,
              PaintedRegion *painted,
              const ElementLayout *layout) {
    if (painted) *painted = {};
    if (!visible(element)) return;
    std::string text = overrideText.empty() ? element.get("text") : overrideText;
    if (text.empty()) return;
    if (element.getBool("forceuppercase", false)) text = upper(text);

    const int pixelSize = std::max(
        6, static_cast<int>(std::round(
               (layout && layout->fontSize ? *layout->fontSize
                   : propertyFloat(element, "fontsize", 0.035f)) * renderer.height())));
    const std::string fontPath = element.get("fontpath");
    int naturalW = 0, naturalH = 0;
    font.textSizeStyled(text, fontPath, pixelSize, naturalW, naturalH);

    float x = defaultX, y = defaultY, originX = 0.0f, originY = 0.0f;
    if (layout && layout->pos) { x = (*layout->pos)[0]; y = (*layout->pos)[1]; }
    else propertyPair(element, "pos", x, y, defaultX, defaultY);
    const auto values = animated(element, x, y, layout);
    if (values.opacity <= 0 || !motion.opacity || values.scale <= 0) return;
    if (layout && layout->origin) { originX = (*layout->origin)[0]; originY = (*layout->origin)[1]; }
    else propertyPair(element, "origin", originX, originY, 0.0f, 0.0f);

    float width = defaultW, height = defaultH;
    const bool hasSize = layout && layout->size ? true
        : !(layout && layout->ignoreSize) && !element.get("size").empty();
    if (layout && layout->size) { width = (*layout->size)[0]; height = (*layout->size)[1]; }
    else if (hasSize) propertyPair(element, "size", width, height, defaultW, defaultH);
    int pixelW = hasSize && width > 0.0f
                     ? static_cast<int>(std::round(width * renderer.width()))
                     : naturalW;
    int pixelH = hasSize && height > 0.0f
                     ? static_cast<int>(std::round(height * renderer.height()))
                     : naturalH;
    if (pixelW <= 0) pixelW = naturalW;
    if (pixelH <= 0) pixelH = naturalH;
    int pixelX = static_cast<int>(std::round(x * renderer.width() - originX * pixelW));
    int pixelY = static_cast<int>(std::round(y * renderer.height() - originY * pixelH));
    Rect animatedBounds{pixelX, pixelY, pixelW, pixelH};
    animateRect(element, renderer, animatedBounds, values, x, y, layout);
    pixelX = animatedBounds.x;
    pixelY = animatedBounds.y;
    pixelW = animatedBounds.w;
    pixelH = animatedBounds.h;
    const uint8_t opacity = floatBits(values.opacity) == floatBits(1.0f) ? motion.opacity
        : static_cast<uint8_t>(std::lround(values.opacity * motion.opacity));
    const int animatedFontSize = std::clamp(floatBits(values.scale) == floatBits(1.0f) ? pixelSize
        : static_cast<int>(std::lround(pixelSize * values.scale)), 6, 192);

    uint32_t background = withOpacity(propertyColor(element, "backgroundcolor", 0), opacity);
    if ((background & 0xFF) != 0)
        renderer.fillRect(pixelX, pixelY, pixelW, pixelH, background);

    uint32_t color = withOpacity(layout && layout->ignoreColor ? defaultColor
        : propertyColor(element, "color", defaultColor), opacity);
    if ((color & 0xFF) || (background & 0xFF))
        captureRegion(renderer, animatedBounds, 0, 0, 0, painted);
    int glow = static_cast<int>(std::round(propertyFloat(element, "glowsize", 0.0f)));
    if (glow > 0) {
        uint32_t glowColor = withOpacity(propertyColor(element, "glowcolor", 0x000000CC), opacity);
        for (int dx = -glow; dx <= glow; ++dx) {
            for (int dy = -glow; dy <= glow; ++dy) {
                if (dx == 0 && dy == 0) continue;
                font.renderStyled(renderer, text, pixelX + dx, pixelY + dy,
                                  pixelW, pixelH, glowColor, fontPath, animatedFontSize,
                                  element.get("alignment", "left"),
                                  element.get("verticalalignment", "top"),
                                  hasSize, element.getFloat("linespacing", 1.0f),
                                  values.rotation);
            }
        }
    }
    font.renderStyled(renderer, text, pixelX, pixelY, pixelW, pixelH, color,
                      fontPath, animatedFontSize, element.get("alignment", "left"),
                      element.get("verticalalignment", "top"), hasSize,
                      element.getFloat("linespacing", 1.0f),
                      values.rotation);
}

void drawExtras(Renderer &renderer,
                Font &font,
                const ThemeView *view,
                float lowerZ,
                float upperZ,
                float defaultZ,
                const std::unordered_map<std::string, std::string> &dynamicText) {
    if (!view) return;
    struct Ordered {
        const ThemeElement *element;
        float z;
        size_t order;
    };
    std::vector<Ordered> ordered;
    ordered.reserve(view->elements.size());
    for (size_t i = 0; i < view->elements.size(); ++i) {
        const ThemeElement &element = view->elements[i];
        if (!isExtra(element) || !visible(element)) continue;
        float z = zIndex(element, defaultZ);
        if (z < lowerZ || z >= upperZ) continue;
        if (element.type != "image" && element.type != "text" &&
            element.type != "ninepatch") {
            continue;
        }
        ordered.push_back({&element, z, i});
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const Ordered &a, const Ordered &b) {
                         if (a.z != b.z) return a.z < b.z;
                         return a.order < b.order;
                     });

    for (const Ordered &item : ordered) {
        const ThemeElement &element = *item.element;
        if (element.type == "image" || element.type == "ninepatch") {
            drawImage(renderer, element);
        } else {
            auto it = dynamicText.find(element.name);
            drawText(renderer, font, element,
                     it == dynamicText.end() ? "" : it->second);
        }
    }
}

void drawSystemExtras(Renderer &renderer, Font &font,
                      const ExtraLayer *layers, size_t layerCount,
                      float lowerZ, float upperZ, float defaultZ) {
    if (!layers || layerCount > 3) return;
    struct Item {
        const ThemeElement *element;
        const ExtraLayer *layer;
        float z;
        size_t order;
    };
    auto fixedBackground = [](const ThemeElement &element) {
        if (element.type != "image" && element.type != "ninepatch") return false;
        static constexpr char prefix[] = "staticbackground";
        if (element.name.size() < sizeof(prefix) - 1) return false;
        for (size_t i = 0; i < sizeof(prefix) - 1; ++i)
            if (std::tolower(static_cast<unsigned char>(element.name[i])) != prefix[i]) return false;
        return true;
    };
    std::vector<Item> ordered;
    for (size_t n = 0; n < layerCount; ++n) {
        const auto &layer = layers[n];
        if (!layer.view || layer.clip.w <= 0 || layer.clip.h <= 0) continue;
        for (size_t i = 0; i < layer.view->elements.size(); ++i) {
            const auto &element = layer.view->elements[i];
            const bool shared = layer.stationary && std::find(layer.stationary->begin(),
                layer.stationary->end(), &element) != layer.stationary->end();
            if ((fixedBackground(element) || shared) != layer.fixedBackgrounds ||
                (!layer.fixedBackgrounds && !isExtra(element)) || !visible(element)) continue;
            if (element.type != "image" && element.type != "ninepatch" && element.type != "text") continue;
            const float z = zIndex(element, defaultZ);
            if (z >= lowerZ && z < upperZ) ordered.push_back({&element, &layer, z, i});
        }
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const Item &a, const Item &b) {
        return a.z != b.z ? a.z < b.z : a.order < b.order;
    });
    for (const auto &item : ordered) {
        const auto &element = *item.element;
        const auto &layer = *item.layer;
        const std::string extra = element.get("extra");
        const bool explicitStatic = extra.size() == 6 &&
            std::equal(extra.begin(), extra.end(), "static", [](unsigned char a, char b) {
                return std::tolower(a) == b;
            });
        const bool shared = layer.stationary && std::find(layer.stationary->begin(),
            layer.stationary->end(), &element) != layer.stationary->end();
        const bool stationary = layer.fixedBackgrounds || explicitStatic || shared;
        MotionScope scope(layer.elapsed, stationary ? 0 : layer.offsetX,
                          stationary ? 0 : layer.offsetY,
                          stationary ? 255 : layer.opacity, layer.event);
        renderer.setClipRect(layer.clip.x, layer.clip.y, layer.clip.w, layer.clip.h);
        if (element.type == "text") drawText(renderer, font, element);
        else drawImage(renderer, element);
    }
    renderer.clearClipRect();
}

void drawHelp(Renderer &renderer,
              Font &font,
              const ThemeView *view,
              const std::string &prompts) {
    if (!view || prompts.empty()) return;
    const ThemeElement *help = view->find("helpsystem", "help");
    ThemeElement fallback;
    if (!help) {
        fallback.type = "helpsystem";
        fallback.name = "help";
        fallback.props["pos"] = "0.012 0.955";
        fallback.props["fontsize"] = "0.028";
        fallback.props["textcolor"] = "777777ff";
        help = &fallback;
    }
    if (!visible(*help)) return;

    const std::vector<HelpPromptEntry> entries = parseHelpPrompts(prompts);
    if (entries.empty()) {
        ThemeElement text = *help;
        text.type = "text";
        text.props["color"] = help->get("textcolor", "777777ff");
        if (text.get("fontsize").empty()) text.props["fontsize"] = "0.028";
        if (text.get("origin").empty()) text.props["origin"] = "0 0";
        drawText(renderer, font, text, prompts, 0x777777FF);
        return;
    }

    const int pixelSize = std::max(6, static_cast<int>(std::round(
        help->getFloat("fontsize", 0.028f) * renderer.height())));
    const std::string fontPath = help->get("fontpath");
    const uint32_t textColor = help->getColor("textcolor", 0x777777FF);
    const uint32_t iconColor = help->getColor("iconcolor", 0x777777FF);
    const int iconTextSpacing = 8;
    const int entrySpacing = 16;

    struct LayoutEntry {
        const HelpPromptEntry *prompt = nullptr;
        SDL_Texture *texture = nullptr;
        int iconW = 0;
        int textW = 0;
    };
    std::vector<LayoutEntry> layout;
    layout.reserve(entries.size());

    int lineHeight = pixelSize;
    int totalWidth = 0;
    for (const HelpPromptEntry &entry : entries) {
        int textW = 0, textH = 0;
        font.textSizeStyled(entry.label, fontPath, pixelSize, textW, textH);
        lineHeight = std::max(lineHeight, textH);

        SDL_Texture *texture = nullptr;
        const std::string property = helpIconProperty(entry.icon);
        if (!property.empty()) {
            const std::string path = help->get(property);
            if (!path.empty()) texture = renderer.loadImage(path);
        }

        const int iconH = std::max(10, static_cast<int>(
            std::round(lineHeight * 1.25f)));
        int iconW = iconH;
        if (texture) {
            int textureW = 1, textureH = 1;
            renderer.textureSize(texture, textureW, textureH);
            iconW = std::max(1, static_cast<int>(std::round(
                iconH * static_cast<float>(textureW) /
                std::max(1, textureH))));
        } else if (entry.icon.find('/') != std::string::npos) {
            iconW = static_cast<int>(std::round(iconH * 1.55f));
        } else if (entry.icon.size() > 1) {
            iconW = static_cast<int>(std::round(iconH * 1.65f));
        }
        layout.push_back({&entry, texture, iconW, textW});
        totalWidth += iconW + iconTextSpacing + textW + entrySpacing;
    }
    if (!layout.empty()) totalWidth -= entrySpacing;

    const int rowHeight = std::max(10, static_cast<int>(
        std::round(lineHeight * 1.25f)));
    float posX = 0.012f, posY = 0.9515f;
    float originX = 0.0f, originY = 0.0f;
    help->getPos(posX, posY, posX, posY);
    help->getOrigin(originX, originY, originX, originY);
    int x = static_cast<int>(std::round(posX * renderer.width() -
                                        originX * totalWidth));
    const int y = static_cast<int>(std::round(posY * renderer.height() -
                                              originY * rowHeight));

    for (const LayoutEntry &item : layout) {
        if (item.texture) {
            SDL_SetTextureColorMod(item.texture,
                                   (iconColor >> 24) & 0xFF,
                                   (iconColor >> 16) & 0xFF,
                                   (iconColor >> 8) & 0xFF);
            SDL_SetTextureAlphaMod(item.texture, iconColor & 0xFF);
            renderer.drawTexture(item.texture, x, y, item.iconW, rowHeight);
            SDL_SetTextureColorMod(item.texture, 255, 255, 255);
            SDL_SetTextureAlphaMod(item.texture, 255);
        } else {
            const std::string &icon = item.prompt->icon;
            const int centerX = x + item.iconW / 2;
            const int centerY = y + rowHeight / 2;
            const int thickness = std::max(2, rowHeight / 5);
            if (icon == "UP/DOWN" || icon == "LEFT/RIGHT" ||
                icon == "UP/DOWN/LEFT/RIGHT") {
                const bool vertical = icon != "LEFT/RIGHT";
                const bool horizontal = icon != "UP/DOWN";
                if (vertical) {
                    renderer.fillRect(centerX - thickness / 2, y + 1,
                                      thickness, rowHeight - 2, iconColor);
                    renderer.fillRect(centerX - thickness, y + 1,
                                      thickness * 2, thickness, iconColor);
                    renderer.fillRect(centerX - thickness,
                                      y + rowHeight - thickness - 1,
                                      thickness * 2, thickness, iconColor);
                }
                if (horizontal) {
                    renderer.fillRect(x + 1, centerY - thickness / 2,
                                      item.iconW - 2, thickness, iconColor);
                    renderer.fillRect(x + 1, centerY - thickness,
                                      thickness, thickness * 2, iconColor);
                    renderer.fillRect(x + item.iconW - thickness - 1,
                                      centerY - thickness, thickness,
                                      thickness * 2, iconColor);
                }
            } else if (icon.size() == 1) {
                drawCircleOutline(renderer, centerX, centerY,
                                  std::max(3, std::min(item.iconW, rowHeight) / 2 - 1),
                                  iconColor);
                font.renderStyled(renderer, icon, x, y, item.iconW, rowHeight,
                                  iconColor, fontPath,
                                  std::max(7, static_cast<int>(pixelSize * 0.72f)),
                                  "center", "center");
            } else {
                renderer.drawRect(x, y + rowHeight / 4, item.iconW,
                                  std::max(7, rowHeight / 2), iconColor);
                font.renderStyled(renderer, icon, x, y, item.iconW, rowHeight,
                                  iconColor, fontPath,
                                  std::max(6, static_cast<int>(pixelSize * 0.55f)),
                                  "center", "center");
            }
        }
        x += item.iconW + iconTextSpacing;
        font.renderStyled(renderer, item.prompt->label, x, y, item.textW,
                          rowHeight, textColor, fontPath, pixelSize,
                          "left", "center");
        x += item.textW + entrySpacing;
    }
}

void drawScreenOverlay(Renderer &renderer,
                       Font &font,
                       const Theme &theme) {
    const ThemeView *screen = theme.findView("screen");
    if (!screen) return;

    // EmulationStation only renders the clock when DrawClock is enabled.
    // Most handheld distributions leave it off by default.
    if (environmentEnabled("LILSMOKE_DRAW_CLOCK", false)) {
        if (const ThemeElement *clock = screen->find("text", "clock")) {
            if (visible(*clock)) {
                char value[16] = {};
                std::time_t now = std::time(nullptr);
                std::tm local = *std::localtime(&now);
                std::strftime(value, sizeof(value), "%H:%M", &local);
                drawText(renderer, font, *clock, value, 0xFFFFFFFF);
            }
        }
    }

    if (const ThemeElement *battery =
            screen->find("batteryindicator", "batteryIndicator")) {
        if (visible(*battery)) {
            std::string mode = "text";
            if (const char *configured =
                    std::getenv("LILSMOKE_SHOW_BATTERY")) {
                mode = configured;
                std::transform(mode.begin(), mode.end(), mode.begin(),
                               [](unsigned char c) {
                                   return static_cast<char>(std::tolower(c));
                               });
            }
            if (mode == "off" || mode == "false" || mode == "0") return;

            const int percent = currentBatteryPercent();
            if (percent < 0) return;

            const bool smallScreen = renderer.width() < 700 ||
                                     renderer.height() < 700;
            float x = smallScreen ? 0.010f : 0.955f;
            float y = smallScreen ? 0.0f : 0.0125f;
            float width = smallScreen ? 0.065f : 0.033f;
            float height = smallScreen ? 0.065f : 0.033f;
            battery->getPos(x, y, x, y);
            battery->getSize(width, height, width, height);

            const int regionX = static_cast<int>(std::round(x * renderer.width()));
            const int regionY = static_cast<int>(std::round(y * renderer.height()));
            const int regionW = std::max(1, static_cast<int>(
                std::round(width * renderer.width())));
            const int regionH = std::max(10, static_cast<int>(
                std::round(height * renderer.height())));
            const int iconCellW = regionH;
            const bool showText = mode == "text";
            const std::string percentage = std::to_string(percent) + "%";
            const int fontPixels = std::max(
                8, static_cast<int>(regionH * (smallScreen ? 0.55f : 0.70f)));
            int textW = 0, textH = 0;
            if (showText)
                font.textSizeStyled(percentage, "", fontPixels, textW, textH);
            int spacing = std::max(1, renderer.height() / 200);
            if (!battery->get("itemspacing").empty()) {
                spacing = std::max(0, static_cast<int>(std::round(
                    battery->getFloat("itemspacing", 0.0f) *
                    renderer.width())));
            }
            const int itemsW = iconCellW +
                (showText ? spacing + textW : 0);
            int itemX = regionX;
            const std::string alignment = battery->get("horizontalalignment", "left");
            if (alignment == "right") itemX += regionW - itemsW;
            else if (alignment == "center") itemX += (regionW - itemsW) / 2;

            ThemeElement icon = *battery;
            icon.type = "image";
            if (percent > 75) icon.props["path"] = battery->get("full");
            else if (percent > 50) icon.props["path"] = battery->get("at75");
            else if (percent > 25) icon.props["path"] = battery->get("at50");
            else if (percent > 5) icon.props["path"] = battery->get("at25");
            else icon.props["path"] = battery->get("empty");
            icon.props["pos"] =
                std::to_string(itemX / static_cast<float>(renderer.width())) +
                " " + std::to_string(y);
            icon.props.erase("size");
            icon.props["maxsize"] =
                std::to_string(iconCellW /
                               static_cast<float>(renderer.width())) +
                " " + std::to_string(height);
            icon.props["origin"] = "0 0";
            const uint32_t color = battery->getColor("color", 0xFFFFFFFF);
            if (!icon.get("path").empty()) {
                drawImage(renderer, icon);
            } else {
                const int outlineH = std::max(8, regionH / 2);
                const int outlineW = std::min(iconCellW - 3,
                                              std::max(14, outlineH * 2));
                const int outlineX = itemX + std::max(0, (iconCellW - outlineW) / 2);
                const int outlineY = regionY + std::max(0, (regionH - outlineH) / 2);
                renderer.drawRect(outlineX, outlineY, outlineW, outlineH, color);
                renderer.fillRect(outlineX + outlineW, outlineY + outlineH / 3, 2,
                                  std::max(2, outlineH / 3), color);
                renderer.fillRect(outlineX + 2, outlineY + 2,
                                  std::max(1, (outlineW - 4) * percent / 100),
                                  std::max(1, outlineH - 4), color);
            }

            if (showText) {
                font.renderStyled(renderer, percentage,
                                  itemX + iconCellW + spacing, regionY,
                                  textW, regionH, color, "", fontPixels,
                                  "left", "center");
            }
        }
    }

}

}  // namespace themeui
}  // namespace lilsmoke
