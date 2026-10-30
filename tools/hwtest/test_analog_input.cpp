#include "Input.h"
#include <cassert>
#include <cstdio>
#include <climits>

namespace lilsmoke {
struct InputTestAccess {
    static void setup(Input &input, int minimum, int maximum, int flat, int x, int y) {
        input.devices_.clear();
        Input::Device pad;
        pad.gamepad = true;
        pad.axes[ABS_X] = {minimum, maximum, flat, x};
        pad.axes[ABS_Y] = {minimum, maximum, flat, y};
        input.devices_.push_back(pad);
    }
};
}
int main() {
    using namespace lilsmoke;
    Input input;
    assert(!input.analogState().hasLeft);
    InputTestAccess::setup(input, -32768, 32767, 800, -32768, 32767);
    auto result = input.analogState();
    assert(result.hasLeft && !result.hasRight && result.leftX == -32767 && result.leftY == 32767);
    InputTestAccess::setup(input, 0, 255, 6, 127, 132);
    result = input.analogState(); assert(result.leftX == 0 && result.leftY == 0);
    InputTestAccess::setup(input, 0, 255, 0, 0, 255);
    result = input.analogState(); assert(result.leftX == -32767 && result.leftY == 32767);
    InputTestAccess::setup(input, 0, 0, 0, 0, 0);
    result = input.analogState(); assert(result.leftX == 0 && result.leftY == 0);
    InputTestAccess::setup(input, INT_MIN, INT_MAX, 0, INT_MIN, INT_MAX);
    result = input.analogState(); assert(result.leftX == -32767 && result.leftY == 32767);
    std::puts("PASS canonical analog input: signed/unsigned ranges, endpoints, deadzone, zero range and overflow bounds");
}
