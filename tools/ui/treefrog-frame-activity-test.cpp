// SPDX-License-Identifier: MPL-2.0
#include "FrameActivityProfile.h"
#include "FramePacer.h"
#include "UiResolution.h"

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <time.h>
#include <vector>

using lilsmoke::FrameActivityProfile;
static unsigned checks = 0;
static void check(bool condition, const char *message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
static void distribution(const FrameActivityProfile::Distribution &actual,
                         std::vector<uint64_t> expected) {
    std::sort(expected.begin(), expected.end());
    check(actual.samples == expected.size(), "sample count");
    if (expected.empty()) {
        check(!actual.p50 && !actual.p95 && !actual.maximum, "empty distribution");
        return;
    }
    check(actual.p50 == expected[(expected.size() + 1) / 2 - 1], "nearest-rank p50");
    check(actual.p95 == expected[(expected.size() * 95 + 99) / 100 - 1], "nearest-rank p95");
    check(actual.maximum == expected.back(), "maximum");
}

static void accounting() {
    static_assert(sizeof(FrameActivityProfile) <= 4096, "profile must stay below 4 KiB");
    FrameActivityProfile disabled(0);
    check(!disabled.record(0, 1, 2, 1, true, false, 1), "disabled recording");
    check(disabled.summarize().frames == 0, "disabled profile stays empty");
    FrameActivityProfile p(60000);
    uint64_t now = 0;
    for (unsigned i = 0; i < 120; ++i) {
        const uint64_t work = i < 114 ? 900 : 9000;
        const unsigned events = i % 5 == 0 ? 3 : 0;
        check(p.record(now, now + 1, now + work, events, true, false, i % 2), "record");
        now += 10000;
    }
    check(!p.record(now, now + 1, now + 2, 0, true, false, 0), "full window cannot overwrite");
    auto s = p.summarize();
    check(s.frames == 120 && s.changedNative == 60 && s.unchangedNative == 60, "output accounting");
    check(s.noInputUnchanged == 48 && s.inputFrames == 24 && s.inputEvents == 72, "input accounting");
    check(s.workOver60HzBudget == 6, "tail stalls must not disappear into mean");
    check(s.work.p50 == 900 && s.work.p95 == 900 && s.work.maximum == 9000, "tail boundary");
    check(s.interval.samples == 119 && s.interval.maximum == 18100, "real presentation intervals");
    p.clearWindow();
    check(p.summarize().frames == 0, "clear current window");
    check(p.record(now, now, now + 900, 0, true, false, 0), "next window");
    check(p.summarize().interval.samples == 1 && p.summarize().interval.p50 == 1900,
          "cross-window interval retained");

    FrameActivityProfile unknown(1000);
    check(unknown.record(0, 0, 1, 0, false, false, 0), "non-native observation");
    check(unknown.record(2, 2, 3, 0, false, true, 0), "SDL fallback observation");
    check(unknown.record(4, 4, 5, 0, true, true, 0), "contradictory result is conservative");
    s = unknown.summarize();
    check(s.unknown == 3 && !s.unchangedNative && !s.changedNative, "fallback is not static evidence");
    check(!unknown.record(6, 5, 7, 0, true, false, 0), "backward dispatch rejected");
    check(!unknown.record(6, 7, 6, 0, true, false, 0), "backward finish rejected");
    check(!unknown.record(4, 6, 7, 0, true, false, 0), "overlapping frame rejected");
    check(unknown.summarize().frames == 3, "rejected samples do not contaminate window");
}

static void randomized() {
    uint32_t random = 0x19283746U;
    auto next = [&]() { random = random * 1664525U + 1013904223U; return random; };
    for (uint64_t frequency : {1ULL, 59ULL, 60ULL, 1000ULL, 1000000000ULL,
                               std::numeric_limits<unsigned long long>::max()}) {
        FrameActivityProfile p(frequency);
        uint64_t now = uint64_t(1) << 40, previousEnd = 0;
        for (unsigned window = 0; window < 45; ++window) {
            std::vector<uint64_t> work, dispatch, intervals;
            unsigned changed = 0, unchanged = 0, unknown = 0, inputFrames = 0;
            unsigned noInputUnchanged = 0, over = 0;
            uint64_t events = 0;
            const unsigned count = 1 + next() % 120;
            for (unsigned i = 0; i < count; ++i) {
                const uint64_t a = next() % 10000, b = next() % 10000;
                const auto end = now + a + b;
                const unsigned input = next() % 257, rectangles = next() % 513;
                const bool native = (next() % 3) != 0, fallback = (next() % 3) == 0;
                check(p.record(now, now + a, end, input, native, fallback, rectangles), "random record");
                work.push_back(a + b); dispatch.push_back(b);
                if (previousEnd) intervals.push_back(end - previousEnd);
                previousEnd = end; now = end + next() % 100000;
                events += input; inputFrames += input != 0; over += a + b > frequency / 60;
                if (!native || fallback) ++unknown;
                else if (rectangles) ++changed;
                else { ++unchanged; noInputUnchanged += input == 0; }
            }
            const auto s = p.summarize();
            distribution(s.work, work); distribution(s.interval, intervals);
            distribution(s.dispatchToPresent, dispatch);
            check(s.changedNative == changed && s.unchangedNative == unchanged && s.unknown == unknown,
                  "random presentation partition");
            check(s.changedNative + s.unchangedNative + s.unknown == count, "exclusive partition");
            check(s.inputEvents == events && s.inputFrames == inputFrames &&
                  s.noInputUnchanged == noInputUnchanged && s.workOver60HzBudget == over, "random totals");
            p.clearWindow();
        }
    }
    FrameActivityProfile high(1000000000);
    const auto end = std::numeric_limits<uint64_t>::max();
    check(high.record(end - 10000, end - 3000, end - 1000, 0, true, false, 0), "large counter");
    check(high.record(end - 900, end - 800, end, 0, true, false, 1), "large counter second");
    check(high.summarize().interval.maximum == 1000, "counter subtraction stays exact");
}

struct Trace {
    std::vector<uint32_t> renderTimes, delays;
    unsigned actions = 0, powerAt = 0, skipped = 0;
    bool operator==(const Trace &b) const {
        return renderTimes == b.renderTimes && delays == b.delays && actions == b.actions &&
               powerAt == b.powerAt && skipped == b.skipped;
    }
};
static Trace schedule(bool profiling, uint32_t start) {
    Trace trace;
    lilsmoke::FramePacer pacer(start);
    FrameActivityProfile activity(profiling ? 1000 : 0);
    uint32_t tick = start;
    uint64_t absolute = 0;
    for (unsigned frame = 0; frame <= 360; ++frame) {
        if (frame == 360) { trace.powerAt = frame; break; } // Dispatch precedes rendering.
        trace.actions += frame % 3 == 0;
        trace.renderTimes.push_back(tick);
        const unsigned work = frame == 137 ? 200 : (frame % 3 ? 4 : 9);
        if (profiling) {
            check(activity.record(absolute, absolute + 1, absolute + work, frame % 3 == 0,
                                  true, false, frame < 210), "schedule observation");
            if ((frame + 1) % 120 == 0) { activity.summarize(); activity.clearWindow(); }
        }
        tick += work; absolute += work;
        const auto delay = pacer.delayAfterFrame(tick);
        trace.delays.push_back(delay); tick += delay; absolute += delay;
    }
    return trace;
}
static void cadence() {
    for (uint32_t start : {0U, 0xFFFFFF00U}) {
        const auto original = schedule(false, start), observed = schedule(true, start);
        check(original == observed, "observation must not gate render/input/power or alter pacing");
        check(observed.renderTimes.size() == 360 && observed.powerAt == 360 && observed.actions == 120,
              "animation/input cadence retained");
    }
    const auto tv = lilsmoke::softwareUiResolution(1280, 720, true);
    const auto r36 = lilsmoke::softwareUiResolution(640, 480, true);
    const auto native = lilsmoke::softwareUiResolution(1280, 720, false);
    check(tv.width == 856 && tv.height == 480 && r36.width == 640 && r36.height == 480 &&
          native.width == 1280 && native.height == 720, "C43 resolution policy retained");
}

static uint64_t cpuNs() {
    timespec t{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t)) std::abort();
    return uint64_t(t.tv_sec) * 1000000000ULL + t.tv_nsec;
}
static void overhead() {
    FrameActivityProfile profile(1000000000);
    uint64_t time = 0, checksum = 0;
    const auto start = cpuNs();
    for (unsigned i = 0; i < 24000; ++i) {
        if (!profile.record(time, time + 1, time + 1 + i % 20000, 0, true, false, i % 3)) std::abort();
        time += 100000;
        if ((i + 1) % 120 == 0) {
            const auto s = profile.summarize();
            checksum += s.work.p95 + s.interval.p95 + s.dispatchToPresent.p95 + s.unchangedNative;
            profile.clearWindow();
        }
    }
    std::printf("FRAME_ACTIVITY_COST frames=24000 windows=200 thread_cpu_ns=%llu bytes=%zu checksum=%llu\n",
                static_cast<unsigned long long>(cpuNs() - start), sizeof(profile),
                static_cast<unsigned long long>(checksum));
}

int main() {
    try {
        accounting(); randomized(); cadence(); overhead();
        std::printf("FRAME_ACTIVITY_OK checks=%u windows=270 cadence_frames=720\n", checks);
        return 0;
    } catch (const std::exception &e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
}
