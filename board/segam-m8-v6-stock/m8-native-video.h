#ifndef M8_NATIVE_VIDEO_H
#define M8_NATIVE_VIDEO_H
int m8_native_present(void *surface);
int m8_od_present(void *surface);
void m8_native_video_reset(void);
int lilsmoke_present_rgb(const void *pixels, unsigned width, unsigned height,
                        unsigned pitch, int rgb32, unsigned aspect_num, unsigned aspect_den);
int lilsmoke_present_menu(const void *pixels, unsigned width, unsigned height,
                         unsigned pitch, int rgb32);
void lilsmoke_present_reset(void);
#endif
