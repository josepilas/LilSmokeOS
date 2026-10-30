/* SPDX-License-Identifier: AGPL-3.0-only */
#include "src/prep_lock.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc,char** argv) {
    assert(argc==2);assert(mkdir(argv[1],0700)==0);
    FtPrepLock a={.fd=-1},b={.fd=-1};
    assert(ft_prep_lock(&a,argv[1])==0);
    assert(ft_prep_lock(&b,argv[1])<0&&errno==EBUSY);
    pid_t child=fork();assert(child>=0);
    if(!child){ft_prep_unlock(&a);_exit(0);}
    int status;assert(waitpid(child,&status,0)==child&&status==0);
    assert(ft_prep_lock(&b,argv[1])<0&&errno==EBUSY);
    ft_prep_unlock(&a);assert(ft_prep_lock(&b,argv[1])==0);ft_prep_unlock(&b);
    // An owner that dies without releasing must not force a reboot.
    child=fork();assert(child>=0);
    if(!child){FtPrepLock c={.fd=-1};assert(ft_prep_lock(&c,argv[1])==0);_exit(0);}
    assert(waitpid(child,&status,0)==child&&status==0);
    assert(ft_prep_lock(&a,argv[1])==0);
    // An alive reused PID with a DIFFERENT start identity is stale, not busy.
    char path[800];snprintf(path,sizeof path,"%s/owner.pid",a.path);
    FILE* owner=fopen(path,"w");assert(owner);fprintf(owner,"%ld %llu\n",a.pid,a.start+1);assert(fclose(owner)==0);
    assert(ft_prep_lock(&b,argv[1])==0);
    ft_prep_unlock(&a); // Descriptor-relative cleanup cannot remove b's owner.
    FtPrepLock c={.fd=-1};assert(ft_prep_lock(&c,argv[1])<0&&errno==EBUSY);
    ft_prep_unlock(&b);assert(ft_prep_lock(&c,argv[1])==0);ft_prep_unlock(&c);
    assert(rmdir(argv[1])==0);
    puts("UNDERTALE_LOCK PASS live_exclusion=1 crash_recovery=1 pid_reuse=1 fork_release=1 stale_fd_safe=1");
    return 0;
}
