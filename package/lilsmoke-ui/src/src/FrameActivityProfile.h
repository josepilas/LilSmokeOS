// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace lilsmoke {

// Observation only: unchanged output is not permission to skip a view's render().
class FrameActivityProfile {
public:
    static constexpr size_t capacity = 120;

    struct Distribution {
        unsigned samples = 0;
        uint64_t p50 = 0, p95 = 0, maximum = 0;
    };

    struct Summary {
        unsigned frames = 0, changedNative = 0, unchangedNative = 0, unknown = 0;
        unsigned noInputUnchanged = 0, inputFrames = 0, workOver60HzBudget = 0;
        uint64_t inputEvents = 0;
        Distribution work, interval, dispatchToPresent;
    };

    explicit FrameActivityProfile(uint64_t frequency) : frequency_(frequency) {}

    bool record(uint64_t workStart, uint64_t dispatchStart, uint64_t presentEnd,
                unsigned inputEvents, bool nativeAccepted, bool fallback,
                unsigned damageRects) {
        if (!frequency_ || count_ == capacity || dispatchStart < workStart ||
            presentEnd < dispatchStart || (havePrevious_ && workStart < previousEnd_))
            return false;
        const uint64_t work = presentEnd - workStart;
        work_[count_] = work;
        dispatch_[count_] = presentEnd - dispatchStart;
        if (havePrevious_) intervals_[intervalCount_++] = presentEnd - previousEnd_;
        previousEnd_ = presentEnd;
        havePrevious_ = true;
        ++count_;
        totals_.inputEvents += inputEvents;
        totals_.inputFrames += inputEvents != 0;
        totals_.workOver60HzBudget += work > frequency_ / 60;
        if (!nativeAccepted || fallback) ++totals_.unknown;
        else if (damageRects) ++totals_.changedNative;
        else {
            ++totals_.unchangedNative;
            totals_.noInputUnchanged += inputEvents == 0;
        }
        return true;
    }

    Summary summarize() const {
        Summary result = totals_;
        result.frames = static_cast<unsigned>(count_);
        result.work = distribution(work_, count_);
        result.dispatchToPresent = distribution(dispatch_, count_);
        result.interval = distribution(intervals_, intervalCount_);
        return result;
    }

    // Preserve the preceding presentation time across report boundaries.
    void clearWindow() {
        count_ = intervalCount_ = 0;
        totals_ = {};
    }

private:
    static Distribution distribution(std::array<uint64_t, capacity> values, size_t count) {
        if (!count) return {};
        std::sort(values.begin(), values.begin() + count);
        return {static_cast<unsigned>(count), values[(count - 1) / 2],
                values[(count * 95 + 99) / 100 - 1], values[count - 1]};
    }

    uint64_t frequency_, previousEnd_ = 0;
    bool havePrevious_ = false;
    size_t count_ = 0, intervalCount_ = 0;
    std::array<uint64_t, capacity> work_{}, intervals_{}, dispatch_{};
    Summary totals_;
};

} // namespace lilsmoke
