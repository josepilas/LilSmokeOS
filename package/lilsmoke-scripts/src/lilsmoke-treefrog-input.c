/* SPDX-License-Identifier: MPL-2.0 */
#define _GNU_SOURCE
#include "treefrog-input-map.h"
#include "treefrog-loading.h"
#include "owner-lock.h"
#include <SDL.h>
#include <errno.h>
#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <sys/prctl.h>

static volatile sig_atomic_t stopping;
static void interrupted(int signo) { stopping=signo; }
static struct ls_owner_lock port_lease=LS_OWNER_LOCK_INIT;
static void release_port_lease(void) {ls_owner_release(&port_lease);}
extern void treefrog_input_init(void);
extern uint32_t treefrog_input_read(void);
extern void treefrog_input_close(void);

struct exit_hotkey {
    uint64_t last, neutral_at, held_at;
    int sampled, neutral, armed, holding;
};

static int exit_hotkey_step(struct exit_hotkey *key,uint64_t now,uint32_t buttons,uint32_t combo,int connected) {
    if(!connected || (key->sampled && (now<key->last || now-key->last>150)))
        memset(key,0,sizeof *key);
    if(!connected)return 0;
    key->last=now;key->sampled=1;
    if(!(buttons&combo)) {
        if(!key->neutral){key->neutral=1;key->neutral_at=now;}
        if(now-key->neutral_at>=80)key->armed=1;
    } else key->neutral=0;
    if((buttons&combo)!=combo) {
        if(key->holding)key->armed=0;
        key->holding=0;return 0;
    }
    if(!key->armed)return 0;
    if(!key->holding){key->holding=1;key->held_at=now;}
    if(now-key->held_at<500)return 0;
    key->armed=key->holding=0;return 1;
}

struct exit_input {
    struct ls_gp_reader reader;
    struct ls_gp_state previous;
    uint64_t generation, observed;
    struct exit_hotkey pads[LS_GP_SLOTS], mapped;
};

/* 0: invalid; 1: writer busy, defer; 2: recovered coherent live packet. */
static int exit_snapshot_pending(const struct ls_gp_reader *reader,const struct ls_gp_state *previous,
                                 uint64_t generation,uint64_t observed,uint64_t now,struct ls_gp_state *recovered) {
    uint64_t fresh=ls_gp_now();
    if(!reader->map || reader->fd<0 || reader->generation!=generation || previous->magic!=LS_GP_MAGIC ||
       now<observed || now-observed>150 || previous->millis>fresh || fresh-previous->millis>500)return 0;
    const char *path=getenv("LILSMOKE_GAMEPAD_STATE");
    struct stat named,opened;
    if(!path)path="/run/lilsmoke-gamepads.state";
    if(stat(path,&named) || fstat(reader->fd,&opened) || named.st_dev!=opened.st_dev ||
       named.st_ino!=opened.st_ino || named.st_size!=sizeof *previous)return 0;
    uint32_t sequence=__atomic_load_n(&reader->map->sequence,__ATOMIC_ACQUIRE);
    if(sequence&1)return 1;
    struct ls_gp_state current;memcpy(&current,reader->map,sizeof current);
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if(sequence!=__atomic_load_n(&reader->map->sequence,__ATOMIC_ACQUIRE))return 1;
    fresh=ls_gp_now();
    if(current.magic!=LS_GP_MAGIC || current.abi!=1 ||
       current.millis>fresh || fresh-current.millis>500)return 0;
    *recovered=current;return 2;
}

static int exit_input_step(struct exit_input *input,uint64_t now,uint32_t mapped) {
    struct ls_gp_state state;
    int valid=ls_gp_read(&input->reader,&state),connected=0,exit=0;
    /* ls_gp_read clears its output even for a seqlock collision. Defer rather
       than invent a disconnect; only a subsequent valid packet can fire. */
    if(!valid) {
        int recovery=exit_snapshot_pending(&input->reader,&input->previous,input->generation,input->observed,now,&state);
        if(recovery==1)return 0;
        if(recovery==2)valid=1;
    }
    for(unsigned p=0;p<LS_GP_SLOTS;p++) {
        const struct ls_gp_pad *pad=&state.pads[p],*old=&input->previous.pads[p];
        if(input->generation!=input->reader.generation || input->previous.pid!=state.pid ||
           old->instance!=pad->instance || old->vendor!=pad->vendor ||
           old->product!=pad->product || old->version!=pad->version)
            memset(&input->pads[p],0,sizeof input->pads[p]);
        int present=valid && pad->connected;
        connected|=present;
        /* Physical Back/Start is global, including exported or remapped pads.
           Do not combine buttons from different controller slots. */
        if(exit_hotkey_step(&input->pads[p],now,pad->buttons,
                           (1u<<LS_GP_BACK)|(1u<<LS_GP_START),present)) {
            fprintf(stderr,"TREEFROG_EXIT_HOTKEY source=shared slot=%u buttons=%08x hold_ms=500\n",p,pad->buttons);
            exit=1;
        }
    }
    input->previous=state;input->generation=input->reader.generation;
    if(valid)input->observed=now;
    /* The legacy bridge exposes an aggregate mapped word without identities.
       Use it only without shared pads, never to join two physical controllers. */
    if(exit_hotkey_step(&input->mapped,now,mapped,9u,!connected)) {
        fprintf(stderr,"TREEFROG_EXIT_HOTKEY source=mapped buttons=%08x hold_ms=500\n",mapped);exit=1;
    }
    return exit;
}

static void signal_children(int signo,pid_t signaled[4096],unsigned *signaled_count) {
    DIR *proc=opendir("/proc");if(!proc)return;
    struct {pid_t pid,parent;} processes[4096];
    pid_t descendants[4097];unsigned count=0,descendant_count=1;
    descendants[0]=getpid();
    struct dirent *entry;
    while(count<4096 && (entry=readdir(proc))) {
        char *end;long pid=strtol(entry->d_name,&end,10);
        if(*end || pid<=1 || pid==getpid())continue;
        char path[80],line[1024];snprintf(path,sizeof path,"/proc/%ld/stat",pid);
        FILE *f=fopen(path,"r");if(!f)continue;
        char *read=fgets(line,sizeof line,f);fclose(f);if(!read)continue;
        char *name_end=strrchr(line,')');long parent;char state;
        if(name_end && sscanf(name_end+1," %c %ld",&state,&parent)==2) {
            processes[count].pid=(pid_t)pid;processes[count].parent=(pid_t)parent;count++;
        }
    }
    closedir(proc);
    /* Shells can defer TERM while waiting for grandchildren. Include the full
       descendant tree, including detached groups, without signaling our owner. */
    for(unsigned parent=0;parent<descendant_count;parent++)for(unsigned i=0;i<count;i++)
        if(processes[i].parent==descendants[parent]) {
            descendants[descendant_count++]=processes[i].pid;
            processes[i].parent=0;
        }
    while(descendant_count>1) {
        pid_t pid=descendants[--descendant_count];unsigned i;
        for(i=0;i<*signaled_count;i++)if(signaled[i]==pid)break;
        if(i<*signaled_count)continue;
        kill(pid,signo);
        if(*signaled_count<4096)signaled[(*signaled_count)++]=pid;
    }
}

static int run_engine(const char *root,const char *script) {
    /* SDL's legacy console watchdog may detach into another process group.
       Adopt/reap it before the mount owner releases this view. */
    if(prctl(PR_SET_CHILD_SUBREAPER,1,0,0,0)) {perror("TreeFrog child supervision");return 3;}
    signal(SIGTERM,interrupted);signal(SIGINT,interrupted);signal(SIGHUP,interrupted);
    pid_t engine=fork();if(engine<0)return 3;
    if(!engine) {
        if(chroot(root) || chdir("/")) {perror("TreeFrog legacy root");_exit(3);}
        setenv("LD_LIBRARY_PATH","/lib:/usr/lib",1);unsetenv("LD_PRELOAD");unsetenv("LD_AUDIT");
        char *args[]={"sh",(char*)script,NULL};
        execv("/bin/sh",args);perror("TreeFrog original launcher");_exit(127);
    }
    int main_done=0,main_status=0,shutting=0,escalated=0;uint64_t closing=0;
    pid_t signaled[4096];unsigned signaled_count=0;
    for(;;) {
        int status;pid_t done;
        while((done=waitpid(-1,&status,WNOHANG))>0)
            if(done==engine){main_done=1;main_status=status;}
        if(done<0 && errno==ECHILD)break;
        if(stopping || main_done) {
            uint64_t now=ls_gp_now();if(!shutting){shutting=1;closing=now;}
            if(now-closing>=800 && !escalated){escalated=1;signaled_count=0;}
            signal_children(escalated?SIGKILL:SIGTERM,signaled,&signaled_count);
        }
        struct timespec delay={0,20000000};nanosleep(&delay,NULL);
    }
    if(!main_done)return 3;
    return WIFEXITED(main_status)?WEXITSTATUS(main_status):128+WTERMSIG(main_status);
}

int main(int argc, char **argv) {
    if (argc==4 && !strcmp(argv[1],"--engine")) {
        /* Unlike a shell setsid applet, this handoff never forks behind the
           mount owner's PID. Its process group remains addressable on exit. */
        if (strcmp(argv[2],"/run/lilsmoke-treefrog-root")) return 2;
        if (setsid()<0 && getpgrp()!=getpid()) { perror("TreeFrog engine session"); return 3; }
        const char *gate=getenv("LILSMOKE_M8_BOOT_STATE"),*game=getenv("LILSMOKE_M8_LOADING_GAME");
        if(gate && game && !strcmp(gate,"/run/m8-boot-logo.state") && !strcmp(game,"1")) {
            if(treefrog_loading_end("/run/m8-loading","/run/m8-boot-logo.state",2000)) {
                perror("TreeFrog loading-screen release");return 69;
            }
        }
        unsetenv("LILSMOKE_M8_BOOT_STATE");unsetenv("LILSMOKE_M8_LOADING_GAME");
        unsetenv("LILSMOKE_M8_LOADING_DIR");unsetenv("LILSMOKE_LOADING_PROGRESS");
        return run_engine(argv[2],argv[3]);
    }
    if (argc<2) return 2;
    /* Reuse the device-tested controller lease: no advisory-lock syscalls. */
    if(ls_owner_acquire(&port_lease,"/run/lilsmoke-treefrog-port")) {
        fprintf(stderr,"TreeFrog Ports: cannot acquire port ownership: %s (errno=%d)\n",strerror(errno),errno);
        return 3;
    }
    if(atexit(release_port_lease)){release_port_lease();return 3;}
    int keyfd=open("/tmp/joy_key",O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
    if (keyfd<0) return 3;
    close(keyfd);
    key_t key=ftok("/tmp/joy_key",'a');
    int segment=key==(key_t)-1 ? -1 : shmget(key,4,IPC_CREAT|0600);
    if (segment<0) { perror("TreeFrog shared input"); return 3; }
    struct shmid_ds info;
    if (shmctl(segment,IPC_STAT,&info) || info.shm_segsz!=4 || info.shm_perm.uid!=geteuid()) return 3;
    volatile uint32_t *word=shmat(segment,NULL,0);
    if (word==(void *)-1) return 3;
    *word=0;
    signal(SIGTERM,interrupted); signal(SIGINT,interrupted); signal(SIGHUP,interrupted);
    pid_t child=fork();
    if (child<0) { shmdt((void*)word); return 3; }
    if (!child) {
        setsid();
        const char *libraries=getenv("LILSMOKE_TREEFROG_LIBS");
        if (!libraries || !*libraries) _exit(126);
        setenv("LD_LIBRARY_PATH",libraries,1);
        unsetenv("LD_PRELOAD"); unsetenv("LD_AUDIT");
        execv(argv[1],argv+1); _exit(127);
    }
    /* Only the musl bridge uses SDL2. Never preload it into the glibc child.
       HIDAPI is already owned by LilSmoke's daemon, read its shared ABI instead. */
    unsetenv("LD_LIBRARY_PATH");
    setenv("SDL_JOYSTICK_HIDAPI","0",1);
    treefrog_input_init();
    signal(SIGTERM,interrupted); signal(SIGINT,interrupted); signal(SIGHUP,interrupted);
    struct exit_input exit_input={.reader=LS_GP_READER_INIT};
    int terminating=0;
    int status=0, finished=0;
    while (1) {
        finished=waitpid(child,&status,WNOHANG);
        if (finished>0 || (finished<0 && errno!=EINTR)) break;
        uint32_t raw=treefrog_input_read();
        *word=terminating?0:raw;
        uint64_t now=ls_gp_now();
        if (!terminating && (stopping || exit_input_step(&exit_input,now,raw))) {
            /* The session wrapper owns its engine process group and bind cleanup. */
            *word=0;
            kill(child,SIGTERM);
            terminating=1;
        }
        /* The wrapper escalates its engine after two seconds. Never SIGKILL
           the storage owner while it may still be releasing bind mounts. */
        SDL_Delay(8);
    }
    *word=0;
    ls_gp_close(&exit_input.reader);
    treefrog_input_close(); shmdt((void*)word);
    /* Keep the zeroed segment: stale attachers cannot receive stuck buttons. */
    release_port_lease();
    if (finished<0) { perror("TreeFrog waitpid"); return 3; }
    if (terminating && WIFEXITED(status) && (WEXITSTATUS(status)==0 || WEXITSTATUS(status)==143)) return 0;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128+WTERMSIG(status);
}
