#ifndef M8_BOOT_LOGO_H
#define M8_BOOT_LOGO_H
#include <linux/fb.h>
#include <stdint.h>
#include <stddef.h>

#define M8_BOOT_MIN_MS UINT64_C(6000)
#define M8_BOOT_MAX_MAP (32U * 1024U * 1024U)
#define M8_BOOT_STATE "/run/m8-boot-logo.state"
#define M8_LOADING_DIR "/run/m8-loading"
#define M8_BOOT_SCALE 0x80080d00U
#define M8_BOOT_CACHE 0x20000d04U
struct m8_boot_screen { int32_t type; uint16_t x, y, width, height; };
struct m8_boot_scale { uint16_t h_div, v_div, h_mul, v_mul; };

uint64_t m8_boot_now(void);
uint64_t m8_boot_remaining(uint64_t start, uint64_t now);
int m8_boot_geometry(const struct fb_var_screeninfo *v, const struct fb_fix_screeninfo *f);
int m8_boot_mode(const struct fb_var_screeninfo *v, uint32_t memory, uint32_t *stride);
int m8_boot_read_state(const char *path, uint64_t *start);
int m8_boot_draw(void *map, const struct fb_var_screeninfo *v,
                 const struct fb_fix_screeninfo *f, unsigned screen_w, unsigned screen_h,
                 const unsigned char *argb, unsigned width, unsigned height);
int m8_boot_progress(void *map, const struct fb_var_screeninfo *v,
                     const struct fb_fix_screeninfo *f, unsigned percent);
#endif
