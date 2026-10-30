#ifndef M8_POCKETSNES_LOGIC_H
#define M8_POCKETSNES_LOGIC_H

#include <stdint.h>
#include <string.h>
#include <sal.h>

#define M8_INPUT_EXIT (1U << 4)
#define M8_MAX_SKIP 5

struct m8_pacer {
    uint64_t deadline;
    unsigned period, skipped;
};

static inline void m8_pacer_reset(struct m8_pacer *p, uint64_t now, unsigned period)
{
    p->deadline = now;
    p->period = period == 20000 ? 20000 : 16667;
    p->skipped = 0;
}

/* Called once per completed guest frame; choose rendering for the next frame. */
static inline int m8_pacer_step(struct m8_pacer *p, uint64_t now, int manual_skip,
                               uint64_t *wait_us)
{
    uint64_t late;
    p->deadline += p->period;
    *wait_us = now < p->deadline ? p->deadline - now : 0;
    late = now > p->deadline ? now - p->deadline : 0;
    if (late > (uint64_t)p->period * M8_MAX_SKIP)
        p->deadline = now - (uint64_t)p->period * M8_MAX_SKIP;
    if (manual_skip >= 0) {
        if (manual_skip > M8_MAX_SKIP) manual_skip = M8_MAX_SKIP;
        if (p->skipped++ < (unsigned)manual_skip) return 0;
    } else if (late >= p->period && p->skipped < M8_MAX_SKIP) {
        ++p->skipped;
        return 0;
    }
    p->skipped = 0;
    return 1;
}

struct m8_chord {
    uint32_t blocked;
    uint64_t start_since;
    int start_pending, start_sent;
};

static inline uint32_t m8_chord_filter(struct m8_chord *c, uint32_t raw, uint64_t now)
{
    uint32_t combo = 0, event = 0;
    if (c->blocked) {
        uint32_t mask = c->blocked;
        if (!(raw & mask)) c->blocked = 0;
        return raw & ~mask;
    }
    if ((raw & (SAL_INPUT_START | SAL_INPUT_SELECT)) == (SAL_INPUT_START | SAL_INPUT_SELECT)) {
        combo = SAL_INPUT_START | SAL_INPUT_SELECT;
        event = M8_INPUT_EXIT;
    } else if ((raw & (SAL_INPUT_START | SAL_INPUT_X)) == (SAL_INPUT_START | SAL_INPUT_X)) {
        combo = SAL_INPUT_START | SAL_INPUT_X;
        event = SAL_INPUT_MENU;
    } else if (raw & SAL_INPUT_MENU) {
        combo = SAL_INPUT_MENU;
        event = SAL_INPUT_MENU;
    } else if ((raw & (SAL_INPUT_SELECT | SAL_INPUT_L)) == (SAL_INPUT_SELECT | SAL_INPUT_L)) {
        combo = SAL_INPUT_SELECT | SAL_INPUT_L;
        event = SAL_INPUT_QUICKLOAD;
    } else if ((raw & (SAL_INPUT_SELECT | SAL_INPUT_R)) == (SAL_INPUT_SELECT | SAL_INPUT_R)) {
        combo = SAL_INPUT_SELECT | SAL_INPUT_R;
        event = SAL_INPUT_QUICKSAVE;
    }
    if (combo) {
        c->blocked = combo;
        c->start_pending = c->start_sent = 0;
        return (raw & ~combo) | event;
    }
    /* Give the usual Start-first chord 80 ms before exposing its prefix. */
    if (raw & SAL_INPUT_START) {
        if (!c->start_pending && !c->start_sent) {
            c->start_pending = 1;
            c->start_since = now;
        }
        if (c->start_pending && now - c->start_since < 80000)
            return raw & ~SAL_INPUT_START;
        c->start_pending = 0;
        c->start_sent = 1;
    } else {
        if (c->start_pending) raw |= SAL_INPUT_START;
        c->start_pending = c->start_sent = 0;
    }
    return raw;
}

#endif
