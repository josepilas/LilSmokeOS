#pragma once

#include "Theme.h"

#include <algorithm>
#include <cmath>

namespace lilsmoke {
namespace themeui {

struct ThemeAnimationValues {
    float x, y, scale, opacity, rotation;
    bool active;
};

namespace animation_detail {

inline float boundedValue(ThemeAnimation::Property property, float value,
                          float fallback = 0.0f) {
    if (!std::isfinite(value)) value = fallback;
    switch (property) {
        case ThemeAnimation::Property::X:
        case ThemeAnimation::Property::Y:
            return std::max(-16.0f, std::min(16.0f, value));
        case ThemeAnimation::Property::Scale:
            return std::max(0.0f, std::min(4.0f, value));
        case ThemeAnimation::Property::Opacity:
            return std::max(0.0f, std::min(1.0f, value));
        case ThemeAnimation::Property::Rotation:
            return std::max(-36000.0f, std::min(36000.0f, value));
    }
    return fallback;
}

inline uint32_t repeatCount(uint32_t count) {
    return std::max(1u, std::min(ThemeAnimation::MaxRepeat, count));
}

inline uint32_t trackDuration(const ThemeAnimation &track) {
    return std::min(track.durationMs, ThemeAnimation::MaxTimeMs) *
           repeatCount(track.repeat) * (track.autoReverse ? 2u : 1u);
}

inline float ease(float t, ThemeAnimation::Mode mode) {
    switch (mode) {
        case ThemeAnimation::Mode::EaseIn: return t * t;
        case ThemeAnimation::Mode::EaseOut: return t * (2.0f - t);
        case ThemeAnimation::Mode::EaseInOut:
            return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
        default: return t;
    }
}

}  // namespace animation_detail

// elapsedMs is time since the selected event, not a wall clock or frame delta.
inline ThemeAnimationValues sampleAnimation(const ThemeElement &element,
                                            uint32_t elapsedMs,
                                            const std::string &event,
                                            float baseX, float baseY) {
    using Property = ThemeAnimation::Property;
    using namespace animation_detail;
    const float base[5] = {
        boundedValue(Property::X, baseX),
        boundedValue(Property::Y, baseY),
        boundedValue(Property::Scale, element.getFloat("scale", 1.0f), 1.0f),
        boundedValue(Property::Opacity, element.getFloat("opacity", 1.0f), 1.0f),
        boundedValue(Property::Rotation, element.getFloat("rotation", 0.0f))
    };
    float values[5] = {base[0], base[1], base[2], base[3], base[4]};
    const size_t count = std::min(element.animations.size(),
                                  static_cast<size_t>(ThemeAnimation::MaxTracks));
    // A named storyboard replaces the default for that event, not layers over it.
    bool named = false;
    for (size_t i = 0; i < count; ++i)
        if (element.animations[i].event == event) named = true;
    uint32_t lastBegin[5] = {};
    bool assigned[5] = {};
    bool active = false;
    for (size_t i = 0; i < count; ++i) {
        const ThemeAnimation &track = element.animations[i];
        if (named ? track.event != event : !track.event.empty()) continue;
        const unsigned property = static_cast<unsigned>(track.property);
        if (property >= 5) continue;
        const uint32_t begin = std::min(track.beginMs, ThemeAnimation::MaxTimeMs);
        const uint32_t duration = std::min(track.durationMs, ThemeAnimation::MaxTimeMs);
        const uint32_t span = trackDuration(track);
        const uint32_t boardSpan = std::max(begin + span,
            std::min(track.storyboardDurationMs, ThemeAnimation::MaxStoryboardMs));
        const uint32_t boardTotal = boardSpan * repeatCount(track.storyboardRepeat);
        active = active || elapsedMs < boardTotal;
        uint32_t time = elapsedMs;
        if (boardSpan != 0)
            time = elapsedMs >= boardTotal ? boardSpan : elapsedMs % boardSpan;
        if (time < begin || (assigned[property] && begin < lastBegin[property])) continue;
        assigned[property] = true;
        lastBegin[property] = begin;
        const uint32_t local = time - begin;
        float t = track.autoReverse ? 0.0f : 1.0f;
        if (duration != 0 && local < span) {
            const uint32_t cycle = duration * (track.autoReverse ? 2u : 1u);
            const uint32_t phase = local % cycle;
            t = phase / static_cast<float>(duration);
            if (track.autoReverse && phase > duration)
                t = (cycle - phase) / static_cast<float>(duration);
        }
        const float from = track.hasFrom ? boundedValue(track.property, track.from, base[property])
                                         : base[property];
        const float to = track.hasTo ? boundedValue(track.property, track.to, base[property])
                                     : base[property];
        values[property] = boundedValue(track.property,
            from + (to - from) * ease(t, track.mode), base[property]);
    }
    return {values[0], values[1], values[2], values[3], values[4], active};
}

}  // namespace themeui
}  // namespace lilsmoke
