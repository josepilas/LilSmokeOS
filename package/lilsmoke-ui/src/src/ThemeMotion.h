#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace lilsmoke {
class CarouselMotion {
public:
    void reset(int index) { from_ = target_ = static_cast<float>(index); ready_ = true; }
    float position(uint32_t now) const {
        if (!ready_) return 0.0f;
        const float t = std::min(1.0f, (now - started_) / 500.0f);
        const float inverse = 1.0f - t;
        return from_ + (target_ - from_) * (1.0f - inverse * inverse * inverse);
    }
    void select(int index, int count, uint32_t now) {
        if (!ready_ || count < 2) { reset(index); return; }
        from_ = position(now);
        const float turns = std::round((from_ - index) / count);
        target_ = index + turns * count;
        started_ = now;
    }
    bool moving(uint32_t now) const { return ready_ && from_ != target_ && now - started_ < 500; }
private:
    float from_ = 0, target_ = 0;
    uint32_t started_ = 0;
    bool ready_ = false;
};
inline int wrapThemeIndex(int index, int count) {
    if (count < 1) return 0;
    return (index % count + count) % count;
}
inline float themeFade(uint32_t elapsed, uint32_t duration = 150) {
    return duration ? std::min(1.0f, elapsed / static_cast<float>(duration)) : 1.0f;
}
} // namespace lilsmoke
