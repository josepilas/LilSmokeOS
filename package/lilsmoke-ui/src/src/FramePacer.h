// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>

namespace lilsmoke {

class FramePacer {
public:
    explicit FramePacer(uint32_t now) : deadline_(now) {}

    uint32_t delayAfterFrame(uint32_t now) {
        deadline_ += 16;
        remainder_ += 40;
        if (remainder_ >= 60) {
            ++deadline_;
            remainder_ -= 60;
        }
        const int32_t remaining = static_cast<int32_t>(deadline_ - now);
        if (remaining >= 0) return static_cast<uint32_t>(remaining);
        // A missed deadline must not cause a burst of catch-up frames.
        deadline_ = now;
        remainder_ = 0;
        return 0;
    }

private:
    uint32_t deadline_;
    unsigned remainder_ = 0;
};

} // namespace lilsmoke
