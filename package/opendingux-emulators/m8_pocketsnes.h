#ifndef M8_POCKETSNES_H
#define M8_POCKETSNES_H

#include "m8_pocketsnes_logic.h"

#ifdef __cplusplus
extern "C" {
#endif
uint64_t m8_now_us(void);
void m8_timing_reset(unsigned period);
int m8_sync(int manual_skip);
void m8_frame_completed(void);
void m8_frame_rendered(uint64_t start);
const char *m8_fps_text(void);
int m8_video_aspect(void);
int m8_video_native(void);
void m8_video_set_native(int value);
uint32_t m8_input_filter(unsigned player, uint32_t raw, int repeat);
void m8_evdev_init(void);
void m8_evdev_close(void);
int m8_evdev_poll(unsigned player, uint32_t *held);
void m8_input_reset(unsigned player);
int m8_evdev_load_map(const char *path);
uint32_t m8_evdev_key(unsigned code);
#ifdef __cplusplus
}
#endif
#endif
