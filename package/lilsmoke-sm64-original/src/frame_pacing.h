#ifndef LILSMOKE_SM64_FRAME_PACING_H
#define LILSMOKE_SM64_FRAME_PACING_H
#include <stdint.h>

struct sm64_frame_pacer { uint64_t deadline; unsigned remainder; };

/* Exact average 30 Hz, with no burst of catch-up frames after a slow frame. */
static inline uint64_t sm64_frame_deadline(struct sm64_frame_pacer *p,
                                         uint64_t now, uint64_t frequency) {
    p->deadline += frequency / 30;
    p->remainder += (unsigned)(frequency % 30);
    if (p->remainder >= 30) { ++p->deadline; p->remainder -= 30; }
    if (now > p->deadline) p->deadline = now;
    return p->deadline;
}
#endif
