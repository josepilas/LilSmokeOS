#ifndef M8_POWER_UI_H
#define M8_POWER_UI_H
#include <stdint.h>
enum m8_power_screen { M8_POWER_BUSY, M8_POWER_SUCCESS, M8_POWER_CHECK_CARD, M8_POWER_BLACK };
const char *m8_power_message(enum m8_power_screen state);
void m8_power_render(uint16_t *pixels,unsigned width,unsigned height,enum m8_power_screen state);
int m8_power_show(enum m8_power_screen state);
#endif
