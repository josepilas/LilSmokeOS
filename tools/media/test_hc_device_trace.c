static void *trace_dlsym(void *,const char *);
#define dlsym trace_dlsym
#define main hc_worker_program_main
#include "../../package/lilsmoke-media/src/hcplayer_worker.c"
#undef main
#undef dlsym
#include <assert.h>

static int opens,ioctls,maps;
static int trace_open(const char *name,int flags,...){
    (void)flags;++opens;
    if(!strcmp(name,"/dev/mmz"))return 27;
    errno=ENODEV;return -1;
}
static int trace_ioctl(int fd,unsigned long request,...){
    assert(fd==27);++ioctls;
    va_list ap;va_start(ap,request);unsigned long argument=va_arg(ap,unsigned long);va_end(ap);
    assert(argument==0x12345678UL);
    errno=(uint32_t)request==0xc0101901U?ENOMEM:EINVAL;return -1;
}
static int trace_close(int fd){assert(fd==27);return 0;}
static void *trace_mmap(void *address,size_t length,int prot,int flags,int fd,int64_t offset){
    assert(!address&&length==4096&&prot==3&&flags==MAP_SHARED&&fd==27);
    assert((uint64_t)offset==UINT64_C(0x123456789abcdef));
    ++maps;errno=ENOMEM;return MAP_FAILED;
}
static void *trace_dlsym(void *handle,const char *name){
    assert(handle==RTLD_NEXT);
    if(!strcmp(name,"open")||!strcmp(name,"open64"))return (void *)trace_open;
    if(!strcmp(name,"ioctl"))return (void *)trace_ioctl;
    if(!strcmp(name,"close"))return (void *)trace_close;
    if(!strcmp(name,"mmap64"))return (void *)trace_mmap;
    assert(0);return NULL;
}
int main(void){
    assert(open("/dev/auddec",O_RDWR)==-1&&errno==ENODEV&&!opens);
    assert(open64("/dev/mmz",O_RDWR)==27&&opens==1&&atomic_load(&mmz_fd)==27);
    assert(ioctl(27,(ioctl_request)0xc0101901U,0x12345678UL)==-1&&errno==ENOMEM&&atomic_load(&mmz_alloc_error)==ENOMEM);
    assert(ioctl(27,(ioctl_request)0x82980400U,0x12345678UL)==-1&&errno==EINVAL&&atomic_load(&video_init_error)==EINVAL);
    assert(mmap64(NULL,4096,3,MAP_SHARED,27,(int64_t)UINT64_C(0x123456789abcdef))==MAP_FAILED);
    assert(errno==ENOMEM&&atomic_load(&mmz_map_error)==ENOMEM&&ioctls==2&&maps==1);
    atomic_store(&mmz_map_error,0);assert(!close(27)&&atomic_load(&mmz_fd)==-1);
    assert(mmap64(NULL,4096,3,MAP_SHARED,27,(int64_t)UINT64_C(0x123456789abcdef))==MAP_FAILED);
    assert(errno==ENOMEM&&atomic_load(&mmz_map_error)==0&&maps==2);
    puts("HC_DEVICE_TRACE_PASS request_bits=32 offset_bits=64 errno_preserved=1 audio_denied=1");
}
