#ifndef __PSXPORT_H__
#define __PSXPORT_H__

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#define CONFIG_VERSION 0

unsigned get_ticks(void);
void wait_ticks(unsigned s);
void pad_update(void);
unsigned short pad_read(int num);

void video_flip(void);
#ifdef GPU_DFXVIDEO
void video_set(unsigned short *pVideo, unsigned int width, unsigned int height);
#endif
void video_clear(void);
void port_printf(int x, int y, const char *text);

extern unsigned short *SCREEN;

int state_load(int slot);
int state_save(int slot);

int SelectGame(void);
int GameMenu(void);

#include "rkgame_bridge.h"

#endif
