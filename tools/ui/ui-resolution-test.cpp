#include "UiResolution.h"
#include <cassert>
#include <cstdio>
int main() {
    using lilsmoke::softwareUiResolution;
    unsigned cases = 0;
    for (int w = 320; w <= 1920; w += 8)
        for (int h = 240; h <= 1080; h += 8) {
            auto native = softwareUiResolution(w, h, false);
            assert(native.width == w && native.height == h);
            auto balanced = softwareUiResolution(w, h, true);
            assert(balanced.width > 0 && balanced.height > 0);
            assert(balanced.width <= w + 4 && balanced.height <= h + 4);
            assert(balanced.width * balanced.height <= 860 * 480);
            const long difference = long(balanced.width) * h - long(balanced.height) * w;
            const long absolute = difference < 0 ? -difference : difference;
            assert(absolute * 100 <= long(balanced.height) * w * 2);
            ++cases;
        }
    auto tv = softwareUiResolution(1280, 720, true);
    assert(tv.width == 856 && tv.height == 480);
    auto lcd = softwareUiResolution(640, 480, true);
    assert(lcd.width == 640 && lcd.height == 480);
    std::printf("UI_RESOLUTION_OK cases=%u native_preserved=1 r36sx_640x480=1 tv_856x480=1\n", cases);
}
