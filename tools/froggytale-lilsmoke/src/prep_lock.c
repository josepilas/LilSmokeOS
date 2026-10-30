/* SPDX-License-Identifier: AGPL-3.0-only */
#include "prep_lock.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int process_start(long pid, unsigned long long* start) {
    char path[64],data[1024];snprintf(path,sizeof path,"/proc/%ld/stat",pid);
    FILE* file=fopen(path,"r");if(!file)return -1;
    int ok=fgets(data,sizeof data,file)!=NULL;fclose(file);
    char* cursor=ok?strrchr(data,')'):NULL;
    if(!cursor){errno=EIO;return -1;}cursor+=2;
    for(unsigned field=3;field<22;++field) {
        cursor=strchr(cursor,' ');if(!cursor){errno=EIO;return -1;}++cursor;
    }
    char* end;errno=0;*start=strtoull(cursor,&end,10);
    if(errno||end==cursor||(*end!=' '&&*end!='\n')){errno=EIO;return -1;}
    return 0;
}

static int boot_id(char id[40]) {
    FILE* file=fopen("/proc/sys/kernel/random/boot_id","r");if(!file)return -1;
    int ok=fgets(id,40,file)!=NULL;fclose(file);
    if(!ok){errno=EIO;return -1;}id[strcspn(id,"\n")]=0;
    if(strlen(id)!=36){errno=EIO;return -1;}
    for(unsigned i=0;i<36;++i)if(!((id[i]>='0'&&id[i]<='9')||(id[i]>='a'&&id[i]<='f')||id[i]=='-')){errno=EIO;return -1;}
    return 0;
}

int ft_prep_lock(FtPrepLock* lock,const char* cache) {
    char boot[40],claim[700];unsigned long long start;
    lock->fd=-1;lock->path[0]=0;
    if(boot_id(boot)||process_start((long)getpid(),&start))return -1;
    if(snprintf(lock->path,sizeof lock->path,"%s/.prepare-lock-v2-%s",cache,boot)>=(int)sizeof lock->path||
       snprintf(claim,sizeof claim,"%s/.prepare-claim-%s-%ld-%llu",cache,boot,(long)getpid(),start)>=(int)sizeof claim){errno=ENAMETOOLONG;return -1;}
    if(mkdir(claim,0700))return -1;
    int claimfd=open(claim,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if(claimfd<0){rmdir(claim);return -1;}
    int owner=openat(claimfd,"owner.pid",O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC|O_NOFOLLOW,0600);
    char record[96];int bytes=snprintf(record,sizeof record,"%ld %llu\n",(long)getpid(),start);
    int ok=owner>=0&&write(owner,record,bytes)==bytes&&fsync(owner)==0;
    if(owner>=0)close(owner);
    if(!ok)goto failed;
    // Publish a NONEMPTY directory atomically. rename cannot replace a live
    // nonempty claim, even on FAT without flock or renameat2 support.
    for(unsigned retry=0;retry<8;++retry) {
        if(!rename(claim,lock->path)){
            lock->fd=claimfd;lock->pid=(long)getpid();lock->start=start;memcpy(lock->boot,boot,sizeof boot);return 0;
        }
        if(errno!=EEXIST&&errno!=ENOTEMPTY)break;
        int held=open(lock->path,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
        if(held<0){if(errno==ENOENT)continue;break;}
        int fd=openat(held,"owner.pid",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
        ssize_t count=fd<0?-1:read(fd,record,sizeof record-1);int error=errno;
        if(fd>=0)close(fd);
        if(count<=0) {
            close(held);
            // A concurrent stale-owner reaper may have just emptied it.
            if(count<0&&error==ENOENT){rmdir(lock->path);continue;}
            errno=EIO;break;
        }
        record[count]=0;long pid;unsigned long long expected,actual;char extra;
        if(sscanf(record,"%ld %llu %c",&pid,&expected,&extra)!=2||pid<=1){close(held);errno=EIO;break;}
        int state=process_start(pid,&actual);error=errno;
        if(!state&&actual==expected){close(held);errno=EBUSY;break;}
        if(state&&error!=ENOENT&&error!=ESRCH){close(held);errno=error;break;}
        // Descriptor-relative removal cannot delete a newly published owner's
        // file if another reaper has already removed/replaced this directory.
        unlinkat(held,"owner.pid",0);close(held);rmdir(lock->path);
    }
failed:
    {
        int error=errno?errno:EIO;
        unlinkat(claimfd,"owner.pid",0);close(claimfd);rmdir(claim);errno=error;return -1;
    }
}

void ft_prep_unlock(FtPrepLock* lock) {
    if(lock->fd<0)return;
    if(lock->pid!=(long)getpid()){close(lock->fd);lock->fd=-1;return;}
    unlinkat(lock->fd,"owner.pid",0);close(lock->fd);lock->fd=-1;rmdir(lock->path);
}
