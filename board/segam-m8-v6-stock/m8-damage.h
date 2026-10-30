#ifndef M8_DAMAGE_H
#define M8_DAMAGE_H
#include <stdint.h>
/* Opaque ARGB8888/XRGB8888 source; each rectangle is four int32 x/y/w/h.
 * Return one only after all requested rectangles were presented. */
int lilsmoke_present_damage(const void *pixels, unsigned width, unsigned height,
                           unsigned pitch, const int32_t *rects, unsigned count);
void lilsmoke_damage_reset(void);
int m8_damage_present(const void *pixels, unsigned width, unsigned height,
                      unsigned pitch, const int32_t *rects, unsigned count);
#endif
