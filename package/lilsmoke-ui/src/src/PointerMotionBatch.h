#pragma once

#include "Input.h"
#include <algorithm>
#include <limits>

namespace lilsmoke {

// Callers flush before actions and do not batch held-button drags.
// Per-packet scaling and clamping remain in Input.
class PointerMotionBatch {
public:
    bool append(const PointerEvent &event) {
        if (event.type != PointerEventType::Moved ||
            (pending() && event.touchpad != motion_.touchpad)) return false;
        auto sum = [](int a, int b) {
            return static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(a) + b,
                std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
        };
        const int dx = sum(motion_.dx, event.dx), dy = sum(motion_.dy, event.dy);
        motion_ = event;
        motion_.dx = dx;
        motion_.dy = dy;
        return true;
    }
    bool pending() const { return motion_.type == PointerEventType::Moved; }
    PointerEvent take() {
        PointerEvent result = motion_;
        motion_ = {};
        return result;
    }
private:
    PointerEvent motion_;
};

}  // namespace lilsmoke
