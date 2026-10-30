#ifndef RKGAME_BRIDGE_H
#define RKGAME_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*rkgame_pcsx_video_cb)(const uint16_t *frame,
                                     int width,
                                     int height,
                                     int pitch_pixels);
typedef uint16_t (*rkgame_pcsx_input_cb)(int port);
typedef void (*rkgame_pcsx_log_cb)(const char *message);

void rkgame_pcsx_set_work_path(const char *path);
void rkgame_pcsx_set_video_buffer(uint16_t *buffer,
                                  int width,
                                  int height,
                                  int pitch_pixels);
void rkgame_pcsx_set_video_callback(rkgame_pcsx_video_cb cb);
void rkgame_pcsx_set_input_callback(rkgame_pcsx_input_cb cb);
void rkgame_pcsx_set_log_callback(rkgame_pcsx_log_cb cb);

void PCSX_Load(const char *game_path);
void *pcsx_Thread(void *unused);
int pcsx_load_game(const char **game_path);
int pcsx_save_state(char *path);
int pcsx_load_state(char *path);
int retro_save_state(char *path);
int retro_load_state(char *path);

#ifdef __cplusplus
}
#endif

#endif
