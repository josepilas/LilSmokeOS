// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>

namespace lilsmoke {

struct ViewActivity {
    enum class State { Unknown, Active, Settled };
    State state = State::Unknown;
    uint64_t revision = 0;
    uint32_t redrawAt = 0;
    bool hasDeadline = false;

    bool reusable(uint32_t now) const {
        return state == State::Settled &&
            (!hasDeadline || static_cast<int32_t>(now - redrawAt) < 0);
    }
};

// No pixels or ownership inferred here. Renderer must independently approve retention.
class FrameReuseGuard {
public:
    void invalidate() { valid_ = false; }
    bool needsComposition(const void *owner, ViewActivity activity, uint32_t now,
                          uint64_t themeRevision, bool retained, bool overlay) const {
        return !retained || overlay || !activity.reusable(now) || !valid_ ||
            owner_ != owner || revision_ != activity.revision || themeRevision_ != themeRevision;
    }
    void composed(const void *owner, ViewActivity activity, uint32_t now,
                  uint64_t themeRevision, bool overlay) {
        owner_ = owner; revision_ = activity.revision; themeRevision_ = themeRevision;
        valid_ = !overlay && activity.reusable(now);
    }
private:
    const void *owner_ = nullptr;
    uint64_t revision_ = 0, themeRevision_ = 0;
    bool valid_ = false;
};
} // namespace lilsmoke
