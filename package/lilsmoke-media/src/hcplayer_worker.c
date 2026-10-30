/* Firmware ABI worker. Never loads glibc/firmware FFmpeg into the musl UI. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

/* No C++ constructors or root-executable static initializers. The isolated
 * glibc loader runs dependency constructors when dlopen loads the player. */
void __libc_csu_init(void) {}
void __libc_csu_fini(void) {}

static volatile sig_atomic_t stopping;
static int reply_fd=STDOUT_FILENO;
static _Atomic int video_open_error;
static _Atomic int mmz_fd=-1,mmz_open_error,mmz_alloc_error,mmz_map_error,video_init_error;
static void stop(int value){(void)value;stopping=1;}
static void reply(const char *format,...){
    va_list ap;va_start(ap,format);vdprintf(reply_fd,format,ap);va_end(ap);dprintf(reply_fd,"\n");
}

static int guarded_open(const char *name,int flags,mode_t mode,const char *symbol){
    /* The device's known-working musl PCM worker owns audio. Never send the
     * R36 firmware's different compressed-audio ABI to this M8 kernel. */
    if(!strcmp(name,"/dev/auddec")||!strncmp(name,"/dev/snd",8)){errno=ENODEV;return -1;}
    int (*real_open)(const char *,int,...)=dlsym(RTLD_NEXT,symbol);
    if(!real_open){errno=ENOSYS;return -1;}
    int result=real_open(name,flags,mode);
    if(!strcmp(name,"/dev/viddec")){
        int saved=errno;
        atomic_store_explicit(&video_open_error,result<0?saved:0,memory_order_release);
        fprintf(stderr,"HC_DEVICE_OPEN path=/dev/viddec flags=%x result=%d errno=%d\n",flags,result,result<0?saved:0);
        errno=saved;
    }else if(!strcmp(name,"/dev/mmz")){
        int saved=errno;
        atomic_store_explicit(&mmz_fd,result,memory_order_release);
        atomic_store_explicit(&mmz_open_error,result<0?saved:0,memory_order_release);
        fprintf(stderr,"HC_DEVICE_OPEN path=/dev/mmz flags=%x result=%d errno=%d\n",flags,result,result<0?saved:0);
        errno=saved;
    }
    return result;
}
int open(const char *name,int flags,...){
    mode_t mode=0;if(flags&O_CREAT){va_list ap;va_start(ap,flags);mode=va_arg(ap,int);va_end(ap);}
    return guarded_open(name,flags,mode,"open");
}
int open64(const char *name,int flags,...){
    mode_t mode=0;if(flags&O_CREAT){va_list ap;va_start(ap,flags);mode=va_arg(ap,int);va_end(ap);}
    return guarded_open(name,flags,mode,"open64");
}
int close(int fd){
    int (*real_close)(int)=dlsym(RTLD_NEXT,"close");
    if(!real_close){errno=ENOSYS;return -1;}
    int result=real_close(fd),saved=errno;
    if(result==0||saved==EINTR){
        int expected=fd;atomic_compare_exchange_strong_explicit(&mmz_fd,&expected,-1,memory_order_acq_rel,memory_order_acquire);
    }
    errno=saved;return result;
}

#ifdef __GLIBC__
typedef unsigned long ioctl_request;
#else
typedef int ioctl_request;
#endif
int ioctl(int fd,ioctl_request request,...){
    va_list ap;va_start(ap,request);unsigned long argument=va_arg(ap,unsigned long);va_end(ap);
    int (*real_ioctl)(int,unsigned long,...)=dlsym(RTLD_NEXT,"ioctl");
    if(!real_ioctl){errno=ENOSYS;return -1;}
    int result=real_ioctl(fd,(unsigned long)request,argument),saved=errno;
    uint32_t operation=(uint32_t)request;
    if(operation==0xc0101901U||operation==0x82980400U){
        fprintf(stderr,"HC_DEVICE_IOCTL kind=%s fd=%d request=%08x result=%d errno=%d\n",
            operation==0xc0101901U?"mmz-alloc":"viddec-init",fd,operation,result,result<0?saved:0);
        if(result<0)atomic_store_explicit(operation==0xc0101901U?&mmz_alloc_error:&video_init_error,saved,memory_order_release);
    }
    errno=saved;return result;
}

/* Preserve the stock large-file ABI. Never alias glibc's 32-bit mmap here. */
#ifdef mmap64
#undef mmap64
#endif
void *mmap64(void *address,size_t length,int protection,int flags,int fd,int64_t offset){
    void *(*real_mmap)(void *,size_t,int,int,int,int64_t)=dlsym(RTLD_NEXT,"mmap64");
    if(!real_mmap){errno=ENOSYS;return MAP_FAILED;}
    void *result=real_mmap(address,length,protection,flags,fd,offset);int saved=errno;
    int allocator=atomic_load_explicit(&mmz_fd,memory_order_acquire);
    if(allocator>=0&&fd==allocator){
        fprintf(stderr,"HC_DEVICE_MMAP kind=mmz fd=%d bytes=%zu failed=%d errno=%d\n",fd,length,result==MAP_FAILED,result==MAP_FAILED?saved:0);
        if(result==MAP_FAILED)atomic_store_explicit(&mmz_map_error,saved,memory_order_release);
    }
    errno=saved;return result;
}

struct area {uint16_t x,y,w,h;};
struct display_rect {struct area source,destination;};
typedef int (*init_fn)(int);
typedef void *(*create_fn)(void *);
typedef void (*action_fn)(void *);
typedef void (*stop_fn)(void *,int,int);
typedef int (*seek_fn)(void *,int64_t);
typedef int64_t (*time_fn)(void *);
typedef int (*rect_fn)(void *,struct display_rect *);

int main(int argc,char **argv){
    if(argc!=2||(!strcmp(argv[1],"--probe")?0:argv[1][0]!='/'))return 2;
    reply_fd=dup(STDOUT_FILENO);
    if(reply_fd<0||dup2(STDERR_FILENO,STDOUT_FILENO)<0)return 3;
    signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGPIPE,SIG_IGN);
    void *library=dlopen("libffplayer.so",RTLD_NOW|RTLD_LOCAL);
    if(!library){fprintf(stderr,"HC_LIBRARY_ERROR %s\n",dlerror());return 3;}
#define LOAD(type,name) type name=(type)dlsym(library,#name);if(!name){fprintf(stderr,"HC_SYMBOL_ERROR %s\n",#name);return 3;}
    LOAD(init_fn,hcplayer_init)
    LOAD(create_fn,hcplayer_create)
    LOAD(action_fn,hcplayer_play)
    LOAD(action_fn,hcplayer_pause)
    LOAD(action_fn,hcplayer_resume)
    LOAD(stop_fn,hcplayer_stop2)
    LOAD(seek_fn,hcplayer_seek)
    LOAD(time_fn,hcplayer_get_position)
    LOAD(time_fn,hcplayer_get_duration)
    LOAD(rect_fn,hcplayer_set_display_rect)
#undef LOAD
    if(!strcmp(argv[1],"--probe")){reply("HC_RUNTIME_LOADED version=1 video_only=1 abi=664");return 0;}
    if(hcplayer_init(3)<0){reply("HC_ERROR init");return 4;}
    /* Offsets verified in this pinned firmware's hcplayer_create: URI=0,
     * queue=20, sync=24, disable_audio=164. Aligned allocation covers its
     * entire 248-byte argument block, including reserved zeroed fields. */
    union {uint64_t alignment;unsigned char bytes[248];} args={0};
    char *uri=argv[1];int no_queue=-1,video_master=1;
    memcpy(args.bytes,&uri,4);memcpy(args.bytes+20,&no_queue,4);
    memcpy(args.bytes+24,&video_master,4);args.bytes[164]=1;
    void *player=hcplayer_create(args.bytes);
    if(!player){reply("HC_ERROR create");return 4;}
    int64_t duration=hcplayer_get_duration(player);
    if(duration<0)fprintf(stderr,"HC_DURATION_PENDING value=%lld\n",(long long)duration);
    reply("HC_READY %lld",(long long)(duration<0?-1:duration));
    char line[128];size_t used=0;
    while(!stopping){
        int video_error=atomic_load_explicit(&video_open_error,memory_order_acquire);
        if(video_error){reply("HC_ERROR viddec-open errno=%d",video_error);break;}
        int allocator_error=atomic_load_explicit(&mmz_open_error,memory_order_acquire);
        int allocation_error=atomic_load_explicit(&mmz_alloc_error,memory_order_acquire);
        int mapping_error=atomic_load_explicit(&mmz_map_error,memory_order_acquire);
        int initialization_error=atomic_load_explicit(&video_init_error,memory_order_acquire);
        if(allocator_error){reply("HC_ERROR mmz-open errno=%d",allocator_error);break;}
        if(allocation_error){reply("HC_ERROR mmz-alloc errno=%d",allocation_error);break;}
        if(mapping_error){reply("HC_ERROR mmz-map errno=%d",mapping_error);break;}
        if(initialization_error){reply("HC_ERROR viddec-init errno=%d",initialization_error);break;}
        if(duration<0){
            int64_t current_duration=hcplayer_get_duration(player);
            if(current_duration>=0){duration=current_duration;reply("HC_DURATION %lld",(long long)duration);}
        }
        struct pollfd input={STDIN_FILENO,POLLIN,0};
        int result=poll(&input,1,200);
        if(result<0&&errno!=EINTR)break;
        if(result>0&&(input.revents&(POLLIN|POLLHUP))){
            char bytes[64];ssize_t count=read(STDIN_FILENO,bytes,sizeof(bytes));
            if(count<=0)break;
            for(ssize_t i=0;i<count;++i){
                if(bytes[i]!='\n'){
                    if(used+1>=sizeof(line)){reply("HC_ERROR command-size");stopping=1;break;}
                    line[used++]=bytes[i];continue;
                }
                line[used]=0;used=0;
                if(!strcmp(line,"PLAY"))hcplayer_play(player);
                else if(!strcmp(line,"PAUSE"))hcplayer_pause(player);
                else if(!strcmp(line,"RESUME"))hcplayer_resume(player);
                else if(!strcmp(line,"QUIT")){stopping=1;break;}
                else if(!strncmp(line,"SEEK ",5)){
                    char *end=NULL;long long position=strtoll(line+5,&end,10);
                    if(!end||*end||position<0||position>7LL*24*60*60*1000){reply("HC_ERROR seek-range");stopping=1;break;}
                    if(hcplayer_seek(player,position)<0)reply("HC_SEEK_FAILED");
                }else if(!strncmp(line,"RECT ",5)){
                    unsigned sw,sh,x,y,w,h;char tail;
                    if(sscanf(line+5,"%u %u %u %u %u %u %c",&sw,&sh,&x,&y,&w,&h,&tail)!=6||
                       !sw||!sh||sw>4096||sh>2160||!w||!h||w>4096||h>2160||
                       x>4096-w||y>2160-h){
                        reply("HC_ERROR rect-range");stopping=1;break;
                    }
                    struct display_rect rect={{0,0,sw,sh},{x,y,w,h}};
                    if(hcplayer_set_display_rect(player,&rect)<0)reply("HC_ERROR display-rect");
                }else {reply("HC_ERROR command");stopping=1;break;}
            }
        }
        if(!stopping)reply("HC_POSITION %lld",(long long)hcplayer_get_position(player));
    }
    hcplayer_stop2(player,1,1);
    reply("HC_STOPPED");return (atomic_load_explicit(&video_open_error,memory_order_acquire)||
        atomic_load_explicit(&mmz_open_error,memory_order_acquire)||atomic_load_explicit(&mmz_alloc_error,memory_order_acquire)||
        atomic_load_explicit(&mmz_map_error,memory_order_acquire)||atomic_load_explicit(&video_init_error,memory_order_acquire))?5:0;
}
