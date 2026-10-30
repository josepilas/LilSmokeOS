/* SPDX-License-Identifier: MPL-2.0 */
#ifndef LILSMOKE_TREEFROG_LOADING_H
#define LILSMOKE_TREEFROG_LOADING_H
#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static int treefrog_loading_end(const char *directory,const char *state,unsigned timeout_ms) {
    char active[256],stopped[256];
    if(snprintf(active,sizeof active,"%s/active",directory)>=(int)sizeof active ||
       snprintf(stopped,sizeof stopped,"%s/stopped",directory)>=(int)sizeof stopped) return -1;
    int existed=access(active,F_OK)==0;
    if(unlink(active) && errno!=ENOENT)return -1;
    if(existed) {
        unsigned waited=0;
        while(access(stopped,F_OK)) {
            if(waited>=timeout_ms){errno=ETIMEDOUT;return -1;}
            struct timespec delay={0,10000000};nanosleep(&delay,NULL);waited+=10;
        }
    }
    return !unlink(state) || errno==ENOENT ? 0 : -1;
}
#endif
