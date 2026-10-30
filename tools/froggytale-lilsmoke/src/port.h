/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef FROGGY_PORT_H
#define FROGGY_PORT_H
#include "platformdefs.h"
#include "runner_keyboard.h"
#include "runner_gamepad.h"
#include <SDL.h>
#include <signal.h>
enum { FT_UP, FT_DOWN, FT_LEFT, FT_RIGHT, FT_A, FT_B, FT_X, FT_Y,
       FT_L, FT_R, FT_SELECT, FT_START, FT_COUNT };
typedef struct { bool buttons[FT_COUNT]; float axes[4]; bool connected; } FtPad;
void ft_input_init(void);
void ft_input_close(void);
bool ft_input_poll(void);
void ft_input_apply(RunnerKeyboardState*, RunnerGamepadState*, const FtPad*, const bool*);
void ft_input_runner(Runner*);
void ft_input_clear(void);
bool ft_platform_open(bool);
void ft_platform_close(void);
void ft_frame(const void*, unsigned, unsigned, unsigned);
extern unsigned ft_frames;
extern uint32_t ft_frame_hash;
extern uint16_t ft_pixels[320*240];
extern volatile sig_atomic_t ft_stop;
int ft_self_test(const char*);
int ft_validate_data(const char*, char*, size_t);
#endif
