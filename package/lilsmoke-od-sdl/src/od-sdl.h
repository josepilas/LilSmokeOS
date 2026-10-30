#ifndef OD_SDL_H
#define OD_SDL_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {
	OD_NONE,OD_UP,OD_DOWN,OD_LEFT,OD_RIGHT,OD_A,OD_B,OD_X,OD_Y,
	OD_L1,OD_R1,OD_L2,OD_R2,OD_L3,OD_R3,OD_SELECT,OD_START,OD_HOME,
	OD_VOLUME_UP,OD_VOLUME_DOWN,OD_POWER
};
int od_enabled(void);
int od_api(void);
void *od_real(const char *name,int api);
void od_error(const char *);
void od_input_pump(void);
void od_input_close(void);
void od_input_viewport(int,int);
void od_input_text(int);
void od_event_button(int,int,int);
void od_event_pointer(int,int,int,int,int,int,int);
void od_event_text(const char *);
void od_event_quit(void);
void od_event_menu(void);
int od_event_take(void *);
void od_event_reset(void);
unsigned char *od_event_keys(int,int *);
unsigned od_event_mouse(int *,int *,int);
int od_events_peep(void *,int,int,unsigned,unsigned);
int od1_button(int,int,int);
int od2_button(int,int,int);
void od1_pointer(int,int,int,int,int,int,int);
void od2_pointer(int,int,int,int,int,int,int);
void od1_text(const char *);
void od2_text(const char *);
void od1_quit(void);
void od2_quit(void);
void od1_menu(void);
void od2_menu(void);
int od1_take(void *);
int od2_take(void *);
void od1_reset(void);
void od2_reset(void);
unsigned od1_modifiers(void);
unsigned od2_modifiers(void);
unsigned char *od1_keys(int *);
unsigned char *od2_keys(int *);
unsigned od1_mouse(int *,int *,int);
unsigned od2_mouse(int *,int *,int);
int od1_peep(void *,int,int,unsigned,unsigned);
int od2_peep(void *,int,int,unsigned,unsigned);
int od1_backend_pointer(void *);
int od2_backend_pointer(void *);
void od_audio_shutdown(void);
int od_audio_initialized(void);
void od_audio_mark_initialized(void);
int od_video_logical(void *,int,int);
void od_video_shutdown(void);
#ifdef __cplusplus
}
#endif
#endif
