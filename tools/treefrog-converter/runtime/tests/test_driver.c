/* SPDX-License-Identifier: MPL-2.0
 * Copyright (c) 2026 Jose Pilas. QA board ABI only, not a hardware driver. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned frames,pcm,rate;
static uint64_t samples,energy;
static void report(void) {
    const char *path=getenv("TEST_DRIVER_REPORT");if(!path)return;
    FILE *f=fopen(path,"w");if(!f)return;
    fprintf(f,"{\"frames\":%u,\"pcm_calls\":%u,\"pcm_frames\":%llu,\"energy\":%llu,\"rate\":%u}\n",frames,pcm,(unsigned long long)samples,(unsigned long long)energy,rate);fclose(f);
}
int video_drivers_init(void) {return 1;}
void video_driver_deinit(void) {report();}
int video_driver_disp_frame(void *pixels,int w,int h,int pitch) {
    if(!pixels||w!=32||h!=24||pitch<64)return -1;
    ++frames;return 0;
}
int sound_driver_init(void *device,int hz,int channels) {(void)device;rate=(unsigned)hz;return channels==2?0:-1;}
int sound_driver_playframe(const int16_t *data,int count) {
    if(!data||count<0||count>8192)return -1;
    ++pcm;samples+=(unsigned)count;
    for(int i=0;i<count*2;i++)energy+=(uint64_t)(data[i]<0?-(int)data[i]:data[i]);
    return 0;
}
int sound_driver_deinit(void) {report();return 0;}
