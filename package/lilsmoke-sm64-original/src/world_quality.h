#ifndef LILSMOKE_SM64_WORLD_QUALITY_H
#define LILSMOKE_SM64_WORLD_QUALITY_H
#include <stdint.h>

static inline unsigned sm64_width_index(unsigned width) {
    return width == 160 ? 0 : width == 192 ? 1 : width == 256 ? 2 : 3;
}

struct sm64_world_quality {
    unsigned width, adaptive, samples, warmup, cooldown, headroom_windows;
    uint64_t work_us, raster_us, peak_us;
};

static inline struct sm64_world_quality sm64_quality_init(unsigned width, int adaptive) {
    return (struct sm64_world_quality){.width = width, .adaptive = adaptive, .warmup = 2};
}

/* Whole-frame work excludes the pacer's sleep. Never control game or audio ticks. */
static inline unsigned sm64_quality_sample(struct sm64_world_quality *q,
                                           uint64_t work_us, uint64_t raster_us) {
    if (!q->adaptive) return q->width;
    /* Detail mode never lowers the 256 floor. One expensive 320 frame is
     * enough to return to it; game, camera and audio continue unchanged. */
    if (q->adaptive == 2 && q->width == 320 && work_us > 30000) {
        q->width = 256; q->cooldown = 6; q->headroom_windows = 0;
        q->samples = 0; q->work_us = q->raster_us = q->peak_us = 0;
        return q->width;
    }
    q->work_us += work_us;
    q->raster_us += raster_us;
    uint64_t predicted = work_us;
    if (q->adaptive == 2 && q->width == 256) {
        /* Only renderer work grows with pixel count; conservatively scale
         * all of it, including triangle setup and world scaling. */
        uint64_t extra = raster_us / 16 * 9 + (raster_us % 16 * 9 + 15) / 16;
        predicted = UINT64_MAX - work_us < extra ? UINT64_MAX : work_us + extra;
    }
    if (predicted > q->peak_us) q->peak_us = predicted;
    if (++q->samples < 30) return q->width;
    uint64_t work = q->work_us / 30, raster = q->raster_us / 30;
    uint64_t peak = q->peak_us;
    q->samples = 0; q->work_us = q->raster_us = q->peak_us = 0;
    if (q->warmup) { --q->warmup; return q->width; }
    if (q->cooldown) { --q->cooldown; return q->width; }
    if (q->adaptive == 2) {
        /* Require six windows whose worst predicted 320 frame is <=29 ms,
         * leaving headroom below the original 33.3 ms pacing target. */
        if (q->width == 256 && peak <= 29000) ++q->headroom_windows;
        else q->headroom_windows = 0;
        if (q->headroom_windows >= 6) {
            q->width = 320; q->headroom_windows = 0;
        }
        return q->width;
    }
    if (q->width == 256 && work > 36000 && raster >= work / 2) {
        q->width = 192; q->cooldown = 3; q->headroom_windows = 0;
    } else if (q->width == 192) {
        /* 15 ms * (256/192)^2 < 27 ms: leave margin below the 33.3 ms budget. */
        if (work <= 15000) ++q->headroom_windows;
        else q->headroom_windows = 0;
        if (q->headroom_windows >= 6) {
            q->width = 256; q->cooldown = 3; q->headroom_windows = 0;
        }
    }
    return q->width;
}
#endif
