#ifndef LILSMOKE_SM64_INPUT_BRIDGE_H
#define LILSMOKE_SM64_INPUT_BRIDGE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void sm64_native_input_init(void);
int sm64_native_input_poll(void);
int sm64_native_input_take_menu(void);
unsigned sm64_native_input_menu_buttons(void);
void sm64_native_input_suppress_buttons(void);
void sm64_native_input_read(uint16_t *buttons, int8_t *x, int8_t *y);
void sm64_native_input_close(void);
#ifdef __cplusplus
}
#endif
#endif
