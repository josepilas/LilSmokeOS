#ifndef M8_POCKETSNES_AUDIO_H
#define M8_POCKETSNES_AUDIO_H
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif

#define M8_PCM_RATE 44100U
#define M8_PCM_CHANNELS 2U
#define M8_PCM_CHUNK_FRAMES 735U
#ifndef M8_PCM_INFLIGHT_LIMIT
#define M8_PCM_INFLIGHT_LIMIT 2U
#endif
enum { M8_PCM_IPC_NONE, M8_PCM_IPC_SEQPACKET, M8_PCM_IPC_PIPE };
struct m8_pcm_stats {
    uint64_t submitted_frames, delivered_frames, dropped_frames;
    unsigned inflight;
    int ready, error;
    pid_t worker;
    int transport;
};
/* Single game-thread owner. Init schedules an internal fork, never a device
 * call. Only exact LILSMOKE_M8_AUDIO=1 is enabled. Format must be 44100/2/16.
 * UNIX seqpacket failure falls back to atomic, nonblocking paired pipes.
 * Every initialization failure logs its stage and errno. Volume is 0..50,
 * default 25. All vendor-device operations remain confined to the child. */
int m8_pcm_requested(void);
int m8_pcm_init(unsigned rate, unsigned channels, unsigned bits);
/* Return 1 accepted, 0 deliberately dropped/unavailable, -1 invalid input.
 * At most two copied packets; no waits, vendor syscalls, or retry loops here. */
int m8_pcm_send(const int16_t *samples, size_t frames);
/* Nonblocking status/child reap; also runs startup/progress watchdog. */
int m8_pcm_active(void);
void m8_pcm_get_stats(struct m8_pcm_stats *stats);
/* Menu pause/exit only: TERM then KILL, WNOHANG reap with <=120 ms user-space
 * wait budget. Return 0 if reaped; -1 retains PID (e.g. uninterruptible kernel
 * sleep) and prevents replacement workers. No global SIGCHLD handler. */
int m8_pcm_close(void);
#ifdef __cplusplus
}
#endif
#endif
