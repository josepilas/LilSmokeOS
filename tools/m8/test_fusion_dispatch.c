/* SPDX-License-Identifier: MPL-2.0 */
#define _GNU_SOURCE
#include <fusion/fusion.h>
#include <fusion/fusion_internal.h>
#include <fusion/call.h>
#include <direct/os/mutex.h>
#include <direct/os/waitqueue.h>
#include <assert.h>
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if FUSION_BUILD_MULTI
#error This regression targets the single-process Fusion dispatcher.
#endif

static unsigned tasks(void) {
    DIR *dir = opendir("/proc/self/task");
    if (!dir) return 0;
    struct dirent *entry;
    unsigned count = 0;
    while ((entry = readdir(dir))) if (entry->d_name[0] != '.') ++count;
    closedir(dir);
    return count;
}

static void check_tasks(unsigned before) {
    if (!before) return;
    for (int i = 0; i < 100 && tasks() > before; ++i) usleep(1000);
    assert(tasks() <= before);
}

static FusionWorld *enter(void) {
    FusionWorld *world = NULL;
    assert(fusion_enter(-1, 23, FER_MASTER, &world) == DR_OK);
    assert(world);
    return world;
}

static FusionCallHandlerResult count_call(int caller, int arg, void *ptr,
                                         void *ctx, unsigned serial, int *ret) {
    (void)caller; (void)ptr; (void)serial;
    ++*(int *)ctx;
    if (ret) *ret = arg + 1;
    return FCHR_RETURN;
}

struct Gate {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int started, release, finished, exiting, exited;
    FusionWorld *world;
};

static FusionCallHandlerResult blocked_call(int caller, int arg, void *ptr,
                                           void *ctx, unsigned serial, int *ret) {
    (void)caller; (void)arg; (void)ptr; (void)serial; (void)ret;
    struct Gate *gate = ctx;
    pthread_mutex_lock(&gate->mutex);
    gate->started = 1;
    pthread_cond_broadcast(&gate->cond);
    while (!gate->release) pthread_cond_wait(&gate->cond, &gate->mutex);
    gate->finished = 1;
    pthread_mutex_unlock(&gate->mutex);
    return FCHR_RETURN;
}

static void *exit_world(void *ctx) {
    struct Gate *gate = ctx;
    pthread_mutex_lock(&gate->mutex);
    gate->exiting = 1;
    pthread_cond_broadcast(&gate->cond);
    pthread_mutex_unlock(&gate->mutex);
    assert(fusion_exit(gate->world, false) == DR_OK);
    pthread_mutex_lock(&gate->mutex);
    gate->exited = 1;
    pthread_cond_broadcast(&gate->cond);
    pthread_mutex_unlock(&gate->mutex);
    return NULL;
}

static void busy(void) {
    unsigned before = tasks();
    struct Gate gate = { .mutex = PTHREAD_MUTEX_INITIALIZER,
                        .cond = PTHREAD_COND_INITIALIZER };
    gate.world = enter();
    FusionCall call = {0};
    assert(fusion_call_init(&call, blocked_call, &gate, gate.world) == DR_OK);
    DirectResult result = fusion_call_execute(&call, FCEF_NODIRECT | FCEF_ONEWAY, 0, NULL, NULL);
    assert(result == DR_OK || result == DR_INCOMPLETE);
    pthread_mutex_lock(&gate.mutex);
    while (!gate.started) pthread_cond_wait(&gate.cond, &gate.mutex);
    pthread_mutex_unlock(&gate.mutex);
    pthread_t exiter;
    assert(pthread_create(&exiter, NULL, exit_world, &gate) == 0);
    pthread_mutex_lock(&gate.mutex);
    while (!gate.exiting) pthread_cond_wait(&gate.cond, &gate.mutex);
    pthread_mutex_unlock(&gate.mutex);
    usleep(50000);
    pthread_mutex_lock(&gate.mutex);
    if (gate.exited) {
        puts("FAIL world was freed before its active callback finished");
        fflush(stdout);
        _Exit(9);
    }
    gate.release = 1;
    pthread_cond_broadcast(&gate.cond);
    pthread_mutex_unlock(&gate.mutex);
    assert(pthread_join(exiter, NULL) == 0);
    assert(gate.finished && gate.exited);
    pthread_cond_destroy(&gate.cond);
    pthread_mutex_destroy(&gate.mutex);
    check_tasks(before);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    setbuf(stdout, NULL);
    alarm(20);
    if (!strcmp(argv[1], "busy")) busy();
    else for (int i = 0; i < 40; ++i) {
        unsigned before = tasks();
        FusionWorld *world = enter();
        if (!strcmp(argv[1], "wake")) {
            for (int n = 0; n < 5; ++n) {
                direct_mutex_lock(&world->event_dispatcher_mutex);
                direct_waitqueue_broadcast(&world->event_dispatcher_cond);
                direct_mutex_unlock(&world->event_dispatcher_mutex);
                usleep(1000);
            }
        } else if (!strcmp(argv[1], "queue")) {
            int calls = 0;
            FusionCall call = {0};
            assert(fusion_call_init(&call, count_call, &calls, world) == DR_OK);
            for (int n = 0; n < 20; ++n) {
                int value = 0;
                assert(fusion_call_execute(&call, FCEF_NODIRECT, n, NULL, &value) == DR_OK);
                assert(value == n + 1 && calls == n + 1);
            }
            assert(fusion_call_destroy(&call) == DR_OK);
        } else assert(!strcmp(argv[1], "idle"));
        assert(fusion_exit(world, false) == DR_OK);
        check_tasks(before);
    }
    alarm(0);
    printf("PASS Fusion dispatcher %s\n", argv[1]);
    return 0;
}
