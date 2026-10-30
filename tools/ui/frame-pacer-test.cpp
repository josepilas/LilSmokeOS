// SPDX-License-Identifier: MPL-2.0
#include "FramePacer.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>

static void cadence(uint32_t start, unsigned work) {
    lilsmoke::FramePacer pacer(start);
    uint32_t now = start;
    for (unsigned frame = 1; frame <= 6000; ++frame) {
        now += work;
        const uint32_t delay = pacer.delayAfterFrame(now);
        assert(delay <= 17);
        now += delay;
        const uint32_t expected = frame * 1000U / 60U;
        assert(now - start == expected);
    }
}

int main() {
    for (uint32_t start : {0U, 42U, UINT32_MAX - 30U}) {
        cadence(start, 0);
        cadence(start, 8);
        cadence(start, 16);
        lilsmoke::FramePacer slow(start);
        assert(slow.delayAfterFrame(start + 2000U) == 0);
        assert(slow.delayAfterFrame(start + 2000U) == 16);
        assert(slow.delayAfterFrame(start + 2016U) == 17);
        assert(slow.delayAfterFrame(start + 2040U) == 10);
        // A second missed deadline resets again, rather than catching up.
        assert(slow.delayAfterFrame(start + 4000U) == 0);
        assert(slow.delayAfterFrame(start + 4001U) == 15);
    }
    puts("PASS 54000 frame cadence checks, rollover and long-stall recovery");
}
