// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "Theme.h"
#include <cstdlib>
#include <cstring>

namespace lilsmoke {
namespace themeui {

inline bool activityOptionOff(const char *name) {
    const char *value = std::getenv(name);
    return value && (!std::strcmp(value, "0") || !std::strcmp(value, "off") ||
                     !std::strcmp(value, "false"));
}

// An allowlist, not a guess from identical recent pixels. All authored tracks
// (including delayed/finite ones) deliberately keep the original render cadence.
inline bool systemThemeMayChange(const Theme &theme) {
    for (const char *name : {"system", "screen"}) {
        const ThemeView *view = theme.findView(name);
        if (!view) continue;
        for (const auto &element : view->elements) {
            if (!element.animations.empty()) return true;
            const auto &type = element.type;
            if (type == "batteryindicator") {
                if (!activityOptionOff("LILSMOKE_SHOW_BATTERY")) return true;
            } else if (type != "image" && type != "ninepatch" && type != "text" &&
                       type != "carousel" && type != "helpsystem") return true;
            if (element.name == "clock" && !activityOptionOff("LILSMOKE_DRAW_CLOCK")) {
                const char *clock = std::getenv("LILSMOKE_DRAW_CLOCK");
                if (clock && *clock) return true;
            }
            for (const auto &property : element.props) {
                const auto &key = property.first;
                if (key.find("scroll") != std::string::npos ||
                    key.find("marquee") != std::string::npos ||
                    key.find("video") != std::string::npos || key == "random" ||
                    key == "playlist") return true;
            }
        }
    }
    return false;
}
} // namespace themeui
} // namespace lilsmoke
