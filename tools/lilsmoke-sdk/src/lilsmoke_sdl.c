/* SPDX-License-Identifier: MPL-2.0
 * Copyright (c) 2026 Jose Pilas.
 */
#define _POSIX_C_SOURCE 200809L
#include <lilsmoke/sdl.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void color(LS_App *a, uint32_t c) {
    SDL_SetRenderDrawColor(a->renderer, (Uint8)(c >> 16), (Uint8)(c >> 8), (Uint8)c, 255);
}
static void pad_add(LS_App *a, int index) {
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(index);
    if (id < 0) return;
    for (int i = 0; i < 8; ++i) if (a->pads[i].joystick && a->pads[i].id == id) return;
    for (int i = 0; i < 8; ++i) if (!a->pads[i].joystick) {
        LS_Pad *p = &a->pads[i];
        memset(p, 0, sizeof *p);
        if (SDL_IsGameController(index)) {
            p->controller = SDL_GameControllerOpen(index);
            if (p->controller) p->joystick = SDL_GameControllerGetJoystick(p->controller);
        } else p->joystick = SDL_JoystickOpen(index);
        if (p->joystick) p->id = id;
        return;
    }
}
static void pad_remove(LS_App *a, SDL_JoystickID id) {
    for (int i = 0; i < 8; ++i) {
        LS_Pad *p = &a->pads[i];
        if (!p->joystick || p->id != id) continue;
        if (p->controller) SDL_GameControllerClose(p->controller);
        else SDL_JoystickClose(p->joystick);
        memset(p, 0, sizeof *p);
    }
}
static LS_Pad *pad_find(LS_App *a, SDL_JoystickID id) {
    for (int i = 0; i < 8; ++i)
        if (a->pads[i].joystick && a->pads[i].id == id) return &a->pads[i];
    return NULL;
}
int ls_app_open(LS_App *a, const char *title, int capture_audio) {
    SDL_AudioSpec wanted, got;
    memset(a, 0, sizeof *a);
    ls_lifecycle_init(&a->lifecycle, 1500);
    const char *native=getenv("LILSMOKE_OD_NATIVE");
    a->native_keys=native&&!strcmp(native,"1");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS,"0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS,"0");
    if (SDL_Init(SDL_INIT_VIDEO | (a->native_keys?0:SDL_INIT_JOYSTICK|SDL_INIT_GAMECONTROLLER)) < 0) {
        ls_app_close(a); return -1;
    }
    a->window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 LS_WIDTH, LS_HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (a->window) a->renderer = SDL_CreateRenderer(a->window, -1, SDL_RENDERER_SOFTWARE);
    if (!a->renderer) { ls_app_close(a); return -1; }
    SDL_RenderSetLogicalSize(a->renderer, LS_WIDTH, LS_HEIGHT);
    SDL_JoystickEventState(SDL_ENABLE);
    SDL_GameControllerEventState(SDL_ENABLE);
    if(!a->native_keys)for (int i = 0; i < SDL_NumJoysticks(); ++i) pad_add(a, i);
    if (capture_audio) {
        a->capture = (int16_t *)calloc(LS_CAPTURE_FRAMES * 2, sizeof(int16_t));
        if (!a->capture) { ls_app_close(a); return -1; }
    }
    memset(&wanted, 0, sizeof wanted);
    wanted.freq = 44100; wanted.format = AUDIO_S16SYS; wanted.channels = 2; wanted.samples = 512;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        a->audio = SDL_OpenAudioDevice(NULL, 0, &wanted, &got, 0);
    if (!a->audio) a->audio_failed = 1;
    return 0;
}
void ls_app_close(LS_App *a) {
    if (a->audio) {
        SDL_PauseAudioDevice(a->audio, 1);
        SDL_ClearQueuedAudio(a->audio);
        SDL_CloseAudioDevice(a->audio);
    }
    for (int i = 0; i < 8; ++i) if (a->pads[i].joystick) pad_remove(a, a->pads[i].id);
    free(a->capture); a->capture = NULL;
    if (a->renderer) SDL_DestroyRenderer(a->renderer);
    if (a->window) SDL_DestroyWindow(a->window);
    a->audio = 0; a->renderer = NULL; a->window = NULL;
    SDL_Quit();
}
static LS_Action key_action(SDL_Keycode key) {
    switch (key) {
        case SDLK_UP: return LS_UP; case SDLK_DOWN: return LS_DOWN;
        case SDLK_LEFT: return LS_LEFT; case SDLK_RIGHT: return LS_RIGHT;
        case SDLK_LCTRL: case SDLK_SPACE: case SDLK_a: return LS_ACTIVATE;
        case SDLK_ESCAPE: case SDLK_LALT: case SDLK_BACKSPACE: return LS_BACK;
        case SDLK_RETURN: case SDLK_F1: case SDLK_PAUSE: return LS_MENU;
        default: return LS_NONE;
    }
}
static LS_Action axis_action(LS_Pad *p, int axis, int value) {
    if (!p || axis > 1) return LS_NONE;
    int next = value < -16000 ? -1 : value > 16000 ? 1 : 0;
    int *old = axis ? &p->y : &p->x;
    if (*old == next) return LS_NONE;
    *old = next;
    return !next ? LS_NONE : axis ? (next < 0 ? LS_UP : LS_DOWN) : (next < 0 ? LS_LEFT : LS_RIGHT);
}
/* Observe combo breaks even when release+press are drained in one frame. Keep
 * per-source state so releasing one alias cannot release another held source. */
static void combo_event(LS_App *a, const SDL_Event *e) {
    int relevant = 0;
    LS_Pad *p = NULL;
    uint32_t bit = 0;
    int down = 0;
    if (e->type == SDL_KEYDOWN || e->type == SDL_KEYUP) {
        relevant = e->key.keysym.sym == SDLK_RETURN || e->key.keysym.sym == SDLK_ESCAPE ||
                   e->key.keysym.sym == SDLK_RSHIFT;
    } else if (e->type == SDL_CONTROLLERBUTTONDOWN || e->type == SDL_CONTROLLERBUTTONUP) {
        p = pad_find(a, e->cbutton.which);
        if (p && p->controller) {
            if (e->cbutton.button == SDL_CONTROLLER_BUTTON_START) bit = LS_BUTTON_START;
            if (e->cbutton.button == SDL_CONTROLLER_BUTTON_BACK) bit = LS_BUTTON_SELECT;
            down = e->type == SDL_CONTROLLERBUTTONDOWN;
        }
    } else if (e->type == SDL_JOYBUTTONDOWN || e->type == SDL_JOYBUTTONUP) {
        p = pad_find(a, e->jbutton.which);
        if (p && !p->controller) {
            if (e->jbutton.button == 7) bit = LS_BUTTON_START;
            if (e->jbutton.button == 6) bit = LS_BUTTON_SELECT;
            down = e->type == SDL_JOYBUTTONDOWN;
        }
    }
    if (p && bit) {
        if (down && !a->input_focus_lost) p->combo_held |= bit;
        else p->combo_held &= ~bit;
        relevant = 1;
    }
    if (!relevant) return;
    uint32_t held = a->input_keys[SDL_SCANCODE_RETURN] ? LS_BUTTON_START : 0;
    if (a->input_keys[SDL_SCANCODE_ESCAPE] || a->input_keys[SDL_SCANCODE_RSHIFT]) held |= LS_BUTTON_SELECT;
    for (int i = 0; i < LS_INPUT_PADS; ++i)
        if (a->pads[i].joystick && SDL_JoystickGetAttached(a->pads[i].joystick))
            held |= a->pads[i].combo_held;
    if ((held & LS_EXIT_COMBO) != LS_EXIT_COMBO) ls_lifecycle_cancel_combo(&a->lifecycle);
}
LS_Action ls_input(LS_App *a, const SDL_Event *e) {
    ++a->events;
    if (e->type == SDL_KEYDOWN || e->type == SDL_KEYUP) {
        SDL_Scancode scan = SDL_GetScancodeFromKey(e->key.keysym.sym);
        if (scan > SDL_SCANCODE_UNKNOWN && scan < SDL_NUM_SCANCODES)
            a->input_keys[scan] = e->type == SDL_KEYDOWN && !a->input_focus_lost;
    }
    combo_event(a, e);
    switch (e->type) {
        case SDL_QUIT:
            ls_lifecycle_request_exit(&a->lifecycle, LS_EXIT_SDL_QUIT);
            return LS_QUIT;
        case SDL_WINDOWEVENT:
            if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                memset(a->input_keys, 0, sizeof a->input_keys);
                a->input_focus_lost = 1;
                ls_lifecycle_focus(&a->lifecycle, 0);
                for (int i = 0; i < LS_INPUT_PADS; ++i)
                    a->pads[i].x = a->pads[i].y = a->pads[i].hat = a->pads[i].combo_held = 0;
                return LS_PAUSE;
            }
            if (e->window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                a->input_focus_lost = 0;
                ls_lifecycle_focus(&a->lifecycle, 1);
            }
            if (e->window.event == SDL_WINDOWEVENT_CLOSE) {
                ls_lifecycle_request_exit(&a->lifecycle, LS_EXIT_SDL_QUIT);
                return LS_QUIT;
            }
            break;
        case SDL_KEYDOWN: {
            if (a->input_focus_lost) return LS_NONE;
            ++a->keyboard_events;
            LS_Action action = key_action(e->key.keysym.sym);
            if (e->key.repeat && action != LS_UP && action != LS_DOWN &&
                action != LS_LEFT && action != LS_RIGHT) return LS_NONE;
            return action;
        }
        case SDL_JOYDEVICEADDED: if(!a->native_keys)pad_add(a, e->jdevice.which); break;
        case SDL_CONTROLLERDEVICEADDED: if(!a->native_keys)pad_add(a, e->cdevice.which); break;
        case SDL_JOYDEVICEREMOVED:
        case SDL_CONTROLLERDEVICEREMOVED:
            pad_remove(a, e->jdevice.which);
            ls_lifecycle_cancel_combo(&a->lifecycle);
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            if (a->input_focus_lost || !pad_find(a, e->cbutton.which)) break;
            ++a->joystick_events;
            switch (e->cbutton.button) {
                case SDL_CONTROLLER_BUTTON_A: return LS_ACTIVATE;
                case SDL_CONTROLLER_BUTTON_B: case SDL_CONTROLLER_BUTTON_BACK: return LS_BACK;
                case SDL_CONTROLLER_BUTTON_START: return LS_MENU;
                case SDL_CONTROLLER_BUTTON_DPAD_UP: return LS_UP;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return LS_DOWN;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return LS_LEFT;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return LS_RIGHT;
                default: break;
            }
            break;
        case SDL_CONTROLLERAXISMOTION:
            if (a->input_focus_lost) break;
            ++a->joystick_events;
            return axis_action(pad_find(a, e->caxis.which), e->caxis.axis, e->caxis.value);
        case SDL_JOYBUTTONDOWN: {
            LS_Pad *p = pad_find(a, e->jbutton.which);
            if (!p || p->controller || a->input_focus_lost) break;
            ++a->joystick_events;
            if (e->jbutton.button == 0) return LS_ACTIVATE;
            if (e->jbutton.button == 1 || e->jbutton.button == 6) return LS_BACK;
            if (e->jbutton.button == 7) return LS_MENU;
            break;
        }
        case SDL_JOYAXISMOTION: {
            LS_Pad *p = pad_find(a, e->jaxis.which);
            if (!p || p->controller || a->input_focus_lost) break;
            ++a->joystick_events;
            return axis_action(p, e->jaxis.axis, e->jaxis.value);
        }
        case SDL_JOYHATMOTION: {
            LS_Pad *p = pad_find(a, e->jhat.which);
            if (!p || p->controller || a->input_focus_lost) break;
            Uint8 pressed = e->jhat.value & ~p->hat;
            p->hat = e->jhat.value; ++a->joystick_events;
            if (pressed & SDL_HAT_UP) return LS_UP;
            if (pressed & SDL_HAT_DOWN) return LS_DOWN;
            if (pressed & SDL_HAT_LEFT) return LS_LEFT;
            if (pressed & SDL_HAT_RIGHT) return LS_RIGHT;
            break;
        }
        case SDL_MOUSEBUTTONDOWN:
            ++a->mouse_events;
            if (e->button.button == SDL_BUTTON_RIGHT) return LS_BACK;
            if (e->button.button == SDL_BUTTON_LEFT) {
                a->pointer_x = e->button.x; a->pointer_y = e->button.y; return LS_POINTER;
            }
            break;
        case SDL_MOUSEWHEEL: ++a->mouse_events; return e->wheel.y > 0 ? LS_UP : e->wheel.y < 0 ? LS_DOWN : LS_NONE;
        case SDL_FINGERDOWN:
            ++a->touch_events;
            a->pointer_x = (int)(e->tfinger.x * LS_WIDTH);
            a->pointer_y = (int)(e->tfinger.y * LS_HEIGHT);
            return LS_POINTER;
        default: break;
    }
    return LS_NONE;
}
void ls_input_poll(LS_App *a, LS_InputState *state) {
    static const SDL_Keycode keys[] = {SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT,
        SDLK_LCTRL, SDLK_SPACE, SDLK_a, SDLK_ESCAPE, SDLK_LALT, SDLK_BACKSPACE,
        SDLK_RETURN, SDLK_F1, SDLK_PAUSE, SDLK_RSHIFT};
    static const uint32_t buttons[] = {LS_BUTTON_UP, LS_BUTTON_DOWN, LS_BUTTON_LEFT,
        LS_BUTTON_RIGHT, LS_BUTTON_ACTIVATE, LS_BUTTON_ACTIVATE, LS_BUTTON_ACTIVATE,
        LS_BUTTON_BACK | LS_BUTTON_SELECT, LS_BUTTON_BACK, LS_BUTTON_BACK,
        LS_BUTTON_MENU | LS_BUTTON_START, LS_BUTTON_MENU, LS_BUTTON_MENU, LS_BUTTON_SELECT};
    static const SDL_GameControllerButton mapped[] = {SDL_CONTROLLER_BUTTON_DPAD_UP,
        SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT,
        SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_A,
        SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_START};
    static const uint32_t pad_buttons[] = {LS_BUTTON_UP, LS_BUTTON_DOWN, LS_BUTTON_LEFT,
        LS_BUTTON_RIGHT, LS_BUTTON_ACTIVATE, LS_BUTTON_BACK,
        LS_BUTTON_BACK | LS_BUTTON_SELECT, LS_BUTTON_MENU | LS_BUTTON_START};
    uint32_t held = 0;
    memset(state->pads, 0, sizeof state->pads);
    if (a->input_focus_lost) {
        ls_input_update(state, 0);
        return;
    }
    /* Track delivered events: native adapters may use SDL_PushEvent, which
     * does not update SDL_GetKeyboardState. Keep each alias independently. */
    const Uint8 *keyboard = a->input_keys;
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; ++i)
        if (keyboard[SDL_GetScancodeFromKey(keys[i])]) held |= buttons[i];
    /* The legacy native adapter is intentionally digital-only. */
    if (!a->native_keys) {
        SDL_JoystickUpdate();
        for (int i = 0; i < LS_INPUT_PADS; ++i) {
            LS_Pad *p = &a->pads[i];
            LS_InputPad *out = &state->pads[i];
            if (!p->joystick || !SDL_JoystickGetAttached(p->joystick)) continue;
            out->connected = 1; out->mapped = p->controller != NULL; out->id = p->id;
            if (p->controller) {
                for (size_t b = 0; b < sizeof mapped / sizeof mapped[0]; ++b)
                    if (SDL_GameControllerGetButton(p->controller, mapped[b])) out->held |= pad_buttons[b];
                for (int axis = 0; axis < LS_INPUT_AXES; ++axis) {
                    SDL_GameControllerAxis code = (SDL_GameControllerAxis)axis;
                    if (!SDL_GameControllerHasAxis(p->controller, code)) continue;
                    int16_t raw = SDL_GameControllerGetAxis(p->controller, code);
                    out->axes_available |= 1u << axis;
                    out->axes[axis] = axis >= 4 ? (raw > 0 ? (float)raw / 32767.0f : 0.0f) : ls_axis_normalize(raw);
                }
            } else {
                int count = SDL_JoystickNumAxes(p->joystick);
                for (int axis = 0; axis < count && axis < LS_INPUT_AXES; ++axis) {
                    out->axes_available |= 1u << axis;
                    out->axes[axis] = ls_axis_normalize(SDL_JoystickGetAxis(p->joystick, axis));
                }
                int count_buttons = SDL_JoystickNumButtons(p->joystick);
                if (count_buttons > 0 && SDL_JoystickGetButton(p->joystick, 0)) out->held |= LS_BUTTON_ACTIVATE;
                if (count_buttons > 1 && SDL_JoystickGetButton(p->joystick, 1)) out->held |= LS_BUTTON_BACK;
                if (count_buttons > 6 && SDL_JoystickGetButton(p->joystick, 6))
                    out->held |= LS_BUTTON_BACK | LS_BUTTON_SELECT;
                if (count_buttons > 7 && SDL_JoystickGetButton(p->joystick, 7))
                    out->held |= LS_BUTTON_MENU | LS_BUTTON_START;
                Uint8 hat = SDL_JoystickNumHats(p->joystick) > 0 ? SDL_JoystickGetHat(p->joystick, 0) : 0;
                if (hat & SDL_HAT_UP) out->held |= LS_BUTTON_UP;
                if (hat & SDL_HAT_DOWN) out->held |= LS_BUTTON_DOWN;
                if (hat & SDL_HAT_LEFT) out->held |= LS_BUTTON_LEFT;
                if (hat & SDL_HAT_RIGHT) out->held |= LS_BUTTON_RIGHT;
            }
            /* Digital navigation is a threshold view, not the analog source. */
            if (out->axes[0] < -0.49f) out->held |= LS_BUTTON_LEFT;
            if (out->axes[0] > 0.49f) out->held |= LS_BUTTON_RIGHT;
            if (out->axes[1] < -0.49f) out->held |= LS_BUTTON_UP;
            if (out->axes[1] > 0.49f) out->held |= LS_BUTTON_DOWN;
            held |= out->held;
            p->combo_held = out->held & LS_EXIT_COMBO;
        }
    }
    ls_input_update(state, held);
}
void ls_app_poll(LS_App *a, LS_InputState *state) {
    ls_input_poll(a, state);
    ls_lifecycle_update(&a->lifecycle, state->held, SDL_GetTicks());
}
void ls_frame_pace(LS_FrameClock *clock) {
    uint32_t work_end = SDL_GetTicks(), now = work_end;
    uint32_t wait = ls_frame_wait_ms(clock, now);
    while (wait) {
        SDL_Delay(wait);
        now = SDL_GetTicks();
        wait = ls_frame_wait_ms(clock, now);
    }
    ls_frame_finish(clock, work_end, now);
}
void ls_begin(LS_App *a, uint32_t rgb) { color(a, rgb); SDL_RenderClear(a->renderer); }
void ls_fill(LS_App *a, int x, int y, int w, int h, uint32_t rgb) {
    SDL_Rect r = {x,y,w,h}; color(a,rgb); SDL_RenderFillRect(a->renderer,&r);
}
void ls_line(LS_App *a, int x1, int y1, int x2, int y2, uint32_t rgb) {
    color(a,rgb); SDL_RenderDrawLine(a->renderer,x1,y1,x2,y2);
}
/* Original small 5x7 uppercase bitmap alphabet; no external font dependency. */
typedef struct { char c; Uint8 rows[7]; } Glyph;
static const Glyph glyphs[] = {
    {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
    {'C',{14,17,16,16,16,17,14}}, {'D',{30,17,17,17,17,17,30}},
    {'E',{31,16,16,30,16,16,31}}, {'F',{31,16,16,30,16,16,16}},
    {'G',{14,17,16,23,17,17,15}}, {'H',{17,17,17,31,17,17,17}},
    {'I',{14,4,4,4,4,4,14}}, {'J',{7,2,2,2,18,18,12}},
    {'K',{17,18,20,24,20,18,17}}, {'L',{16,16,16,16,16,16,31}},
    {'M',{17,27,21,21,17,17,17}}, {'N',{17,25,21,19,17,17,17}},
    {'O',{14,17,17,17,17,17,14}}, {'P',{30,17,17,30,16,16,16}},
    {'Q',{14,17,17,17,21,18,13}}, {'R',{30,17,17,30,20,18,17}},
    {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}},
    {'U',{17,17,17,17,17,17,14}}, {'V',{17,17,17,17,17,10,4}},
    {'W',{17,17,17,21,21,21,10}}, {'X',{17,17,10,4,10,17,17}},
    {'Y',{17,17,10,4,4,4,4}}, {'Z',{31,1,2,4,8,16,31}},
    {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},
    {'2',{14,17,1,2,4,8,31}}, {'3',{30,1,1,14,1,1,30}},
    {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
    {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}},
    {'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,1,14}},
    {':',{0,4,4,0,4,4,0}}, {'.',{0,0,0,0,0,6,6}},
    {'/',{1,2,2,4,8,8,16}}, {'-',{0,0,0,31,0,0,0}},
    {'%',{17,2,4,8,17,0,0}}, {'?',{14,17,1,2,4,0,4}},
    {'+',{0,4,4,31,4,4,0}}, {'<',{2,4,8,16,8,4,2}},
    {'>',{8,4,2,1,2,4,8}}
};
void ls_text(LS_App *a, const char *text, int x, int y, int scale, uint32_t rgb) {
    if (scale < 1 || scale > 4) return;
    for (int count = 0; *text && count < 100; ++text, ++count, x += 6 * scale) {
        unsigned char c = (unsigned char)toupper((unsigned char)*text);
        const Glyph *g = NULL;
        if (c == ' ') continue;
        for (size_t i = 0; i < sizeof glyphs / sizeof glyphs[0]; ++i) if (glyphs[i].c == c) { g=&glyphs[i]; break; }
        if (!g) continue;
        for (int row=0; row<7; ++row) for (int col=0; col<5; ++col)
            if (g->rows[row] & (1 << (4-col))) ls_fill(a,x+col*scale,y+row*scale,scale,scale,rgb);
    }
}
void ls_present(LS_App *a) { SDL_RenderPresent(a->renderer); ++a->frames; }
int ls_screenshot(LS_App *a, const char *path) {
    int w=0,h=0;
    if(SDL_GetRendererOutputSize(a->renderer,&w,&h)||w<1||h<1||w>1920||h>1080)return -1;
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);
    if (!s) return -1;
    int result = SDL_RenderReadPixels(a->renderer,NULL,s->format->format,s->pixels,s->pitch);
    if (!result) result = SDL_SaveBMP(s,path);
    SDL_FreeSurface(s); return result;
}
int ls_audio_pump(LS_App *a, int enabled, int volume) {
    int16_t pcm[1024*2];
    if (!a->audio || a->audio_failed) return -1;
    if (!enabled) {
        if (a->audio_active) { SDL_PauseAudioDevice(a->audio,1); SDL_ClearQueuedAudio(a->audio); }
        a->audio_active=0; return 0;
    }
    if (!a->audio_active) { SDL_ClearQueuedAudio(a->audio); SDL_PauseAudioDevice(a->audio,0); a->audio_active=1; }
    if (SDL_GetAudioDeviceStatus(a->audio) == SDL_AUDIO_STOPPED) { a->audio_failed=1; return -1; }
    if (volume < 0) volume=0;
    if (volume > 100) volume=100;
    for (int packet=0; packet<4 && SDL_GetQueuedAudioSize(a->audio)<4096*4; ++packet) {
        uint64_t nonzero=0;
        for (int i=0; i<1024; ++i) {
            unsigned p=a->phase >> 16;
            int wave = p<32768 ? (int)p*2-32768 : 98303-(int)p*2;
            int16_t sample=(int16_t)(wave*(volume*100)/32768);
            pcm[i*2]=pcm[i*2+1]=sample;
            if (sample) nonzero+=2;
            a->phase += (uint32_t)((440ULL<<32)/44100);
        }
        if (SDL_QueueAudio(a->audio,pcm,sizeof pcm)) { a->audio_failed=1; return -1; }
        a->tone_queued+=1024; a->nonzero_samples+=nonzero;
        if (a->capture && a->capture_frames<LS_CAPTURE_FRAMES) {
            size_t n=LS_CAPTURE_FRAMES-a->capture_frames;
            if (n>1024) n=1024;
            memcpy(a->capture+a->capture_frames*2,pcm,n*4);
            a->capture_frames+=n;
        }
    }
    return 0;
}
static void le16(FILE *f, unsigned n) { fputc(n&255,f); fputc((n>>8)&255,f); }
static void le32(FILE *f, uint32_t n) { le16(f,n&65535); le16(f,n>>16); }
int ls_audio_wav(LS_App *a, const char *path) {
    FILE *f=fopen(path,"wb");
    if (!f) return -1;
    unsigned bytes=(unsigned)a->capture_frames*4;
    fwrite("RIFF",1,4,f); le32(f,36+bytes); fwrite("WAVEfmt ",1,8,f);
    le32(f,16); le16(f,1); le16(f,2); le32(f,44100); le32(f,176400); le16(f,4); le16(f,16);
    fwrite("data",1,4,f); le32(f,bytes);
    for (size_t i=0; i<a->capture_frames*2; ++i) le16(f,(uint16_t)a->capture[i]);
    int bad=ferror(f);
    if (fclose(f)) bad=1;
    return bad ? -1 : 0;
}
int ls_data_path(char *out, size_t capacity, const char *leaf) {
    char cwd[4096], directory[4096];
    const char *root=getenv("LILSMOKE_APP_DIR");
    struct stat st;
    if (!out || !capacity || !leaf || !*leaf || strlen(leaf)>120 || leaf[0]=='.') { errno=EINVAL; return -1; }
    for (const unsigned char *p=(const unsigned char *)leaf; *p; ++p)
        if (*p<32 || *p==127 || *p=='/' || *p=='\\') { errno=EINVAL; return -1; }
    if (!root || !*root) root=getcwd(cwd,sizeof cwd);
    if (!root) return -1;
    int n=snprintf(directory,sizeof directory,"%s/userdata",root);
    if (n<0 || (size_t)n>=sizeof directory) { errno=ENAMETOOLONG; return -1; }
    if (mkdir(directory,0700) && errno!=EEXIST) return -1;
    if (lstat(directory,&st) || !S_ISDIR(st.st_mode)) { errno=EINVAL; return -1; }
    n=snprintf(out,capacity,"%s/%s",directory,leaf);
    if (n<0 || (size_t)n>=capacity) { out[0]=0; errno=ENAMETOOLONG; return -1; }
    return 0;
}
static int setting_number(char **cursor,const char *key,int maximum,int *value) {
    size_t length=strlen(key);
    if(strncmp(*cursor,key,length))return -1;
    char *end;
    errno=0;
    long n=strtol(*cursor+length,&end,10);
    if(errno||end==*cursor+length||*end!='\n'||n<0||n>maximum)return -1;
    *value=(int)n; *cursor=end+1; return 0;
}
int ls_settings_load(LS_Settings *s) {
    char path[4096], buffer[257];
    struct stat st;
    LS_Settings loaded;
    if(!s) { errno=EINVAL; return -1; }
    if (ls_data_path(path,sizeof path,"studio.ini")) return -1;
    int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    if (fd<0) return errno==ENOENT ? 0 : -1;
    int good=!fstat(fd,&st) && S_ISREG(st.st_mode) && st.st_size>0 && st.st_size<=256;
    ssize_t n=good ? read(fd,buffer,256) : -1;
    close(fd);
    if (n<=0 || n!=st.st_size) return -1;
    buffer[n]=0;
    const char *magic="LILSMOKE_STUDIO_1\n";
    if(strncmp(buffer,magic,strlen(magic)))return -1;
    char *cursor=buffer+strlen(magic);
    if(setting_number(&cursor,"color=",7,&loaded.color)||
       setting_number(&cursor,"tone=",1,&loaded.tone)||
       setting_number(&cursor,"volume=",100,&loaded.volume)||*cursor)return -1;
    *s=loaded; return 1;
}
int ls_settings_save(const LS_Settings *s) {
    char path[4096], temporary[4128], data[128];
    if (!s || s->color<0 || s->color>7 || s->tone<0 || s->tone>1 || s->volume<0 || s->volume>100) {
        errno=EINVAL; return -1;
    }
    if (ls_data_path(path,sizeof path,"studio.ini")) return -1;
    snprintf(temporary,sizeof temporary,"%s.tmp.XXXXXX",path);
    int fd=mkstemp(temporary);
    if (fd<0) return -1;
    int count=snprintf(data,sizeof data,"LILSMOKE_STUDIO_1\ncolor=%d\ntone=%d\nvolume=%d\n",s->color,s->tone,s->volume);
    int good=write(fd,data,(size_t)count)==count && !fsync(fd);
    if (close(fd)) good=0;
    if (good && rename(temporary,path)) good=0;
    if (!good) { unlink(temporary); return -1; }
    char *slash=strrchr(path,'/'); if (slash) *slash=0;
    fd=open(path,O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if (fd<0) return -1;
    int rc=fsync(fd), saved=errno; close(fd);
    if (rc && saved!=EINVAL && saved!=ENOTSUP) { errno=saved; return -1; }
    return 0;
}
