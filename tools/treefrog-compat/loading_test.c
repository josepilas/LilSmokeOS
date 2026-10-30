/* SPDX-License-Identifier: MPL-2.0 */
#define _GNU_SOURCE
#include "treefrog-loading.h"
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/wait.h>

static void touch(const char *path) {
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);assert(fd>=0);close(fd);
}
int main(void) {
    char root[]="/tmp/treefrog-loading-test-XXXXXX";assert(mkdtemp(root));
    char active[256],stopped[256],state[256];
    snprintf(active,sizeof active,"%s/active",root);snprintf(stopped,sizeof stopped,"%s/stopped",root);
    snprintf(state,sizeof state,"%s/state",root);
    touch(active);touch(state);
    pid_t ack=fork();assert(ack>=0);
    if(!ack) {
        while(!access(active,F_OK))usleep(1000);
        touch(stopped);_exit(0);
    }
    assert(!treefrog_loading_end(root,state,1000));int status;assert(waitpid(ack,&status,0)==ack && !status);
    assert(access(active,F_OK)<0 && access(state,F_OK)<0);assert(!unlink(stopped));
    touch(active);touch(state);
    assert(treefrog_loading_end(root,state,20)<0 && errno==ETIMEDOUT);
    assert(!access(state,F_OK));assert(!unlink(state));
    assert(!treefrog_loading_end(root,state,20));
    assert(!rmdir(root));puts("TREEFROG_LOADING_HANDOFF passed=6");return 0;
}
