// SPDX-License-Identifier: MPL-2.0
#pragma once

namespace lilsmoke {
struct UiResolution { int width, height; };

// Bound animated software UI bandwidth independently of HDMI and game modes.
inline UiResolution softwareUiResolution(int width, int height, bool balanced) {
    if (!balanced || width * height <= 856 * 480) return {width, height};
    const bool landscape = width >= height;
    const int major = landscape ? width : height;
    const int minor = landscape ? height : width;
    int targetMinor = 480;
    int targetMajor = (major * targetMinor + minor / 2) / minor;
    if (targetMajor > 856) {
        targetMajor = 856;
        targetMinor = (minor * targetMajor + major / 2) / major;
    }
    targetMajor = (targetMajor + 4) / 8 * 8;
    return landscape ? UiResolution{targetMajor, targetMinor}
                     : UiResolution{targetMinor, targetMajor};
}
}
