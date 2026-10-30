#include "NativeVideo.h"
#include "OverlayBlit.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char **environ;

namespace lilsmoke::media {
namespace {
#ifndef LILSMOKE_HCPLAYER_ROOT
#define LILSMOKE_HCPLAYER_ROOT "/usr/lib/lilsmoke/hcplayer"
#endif
constexpr const char *Root=LILSMOKE_HCPLAYER_ROOT;
constexpr unsigned long ScreenInfo=0xc00c0e0cUL,LayerOrder=0x80140e09UL,Scale=0x80080d00UL;
struct Screen {int32_t type;uint16_t x,y,width,height;};
struct Order {int32_t type,main,aux,gmas,gmaf;};
struct Scaling {uint16_t hdiv,vdiv,hmul,vmul;};
static_assert(sizeof(Screen)==12&&sizeof(Order)==20&&sizeof(Scaling)==8,"HC ioctl ABI");
int openDevice(const char *path,int flags) {
    char resolved[PATH_MAX];
    if(!realpath(path,resolved)||std::strncmp(resolved,"/dev/",5)){errno=ENODEV;return -1;}
    const int fd=open(resolved,flags|O_CLOEXEC|O_NOFOLLOW);
    struct stat info{};
    if(fd>=0&&(fstat(fd,&info)||!S_ISCHR(info.st_mode))){const int error=errno?errno:ENODEV;close(fd);errno=error;return -1;}
    return fd;
}
bool device(const char *path){struct stat st{};return stat(path,&st)==0&&S_ISCHR(st.st_mode);}
bool env(const char *name){const char *p=std::getenv(name);return p&&std::strcmp(p,"1")==0;}
}
struct NativeVideo::State {
    int fb=-1,display=-1,commands=-1,status=-1;
    pid_t child=-1;
    uint8_t *mapping=nullptr;
    size_t span=0;
    fb_var_screeninfo mode{};fb_fix_screeninfo memory{};
    int width=0,height=0,screenW=1920,screenH=1080,sourceW=0,sourceH=0;
    bool ready=false,playing=false,paused=false,fill=false,loadingPrepared=false;
    uint64_t deadline=0;
    double position=0,duration=0;
    std::string partial,error;
    std::vector<uint32_t> pixels;
    OverlayBlit blit;
    bool send(const std::string &line){
        if(commands<0)return false;
        const std::string message=line+'\n';
        sigset_t one,previous;sigemptyset(&one);sigaddset(&one,SIGPIPE);
        if(sigprocmask(SIG_BLOCK,&one,&previous)!=0)return false;
        const ssize_t count=write(commands,message.data(),message.size());
        if(count<0&&errno==EPIPE&&!sigismember(&previous,SIGPIPE)){
            timespec zero{};while(sigtimedwait(&one,nullptr,&zero)<0&&errno==EINTR){}
        }
        sigprocmask(SIG_SETMASK,&previous,nullptr);
        if(count!=ssize_t(message.size())){error="Native video command channel unavailable";return false;}
        return true;
    }
};
NativeVideo::NativeVideo():state_(new State){}
NativeVideo::~NativeVideo(){
    stop();auto &s=*state_;
    if(s.mapping){for(unsigned y=0;y<s.mode.yres;++y)std::memset(s.mapping+(size_t(y)+s.mode.yoffset)*s.memory.line_length+s.mode.xoffset*4,0,s.mode.xres*4);munmap(s.mapping,s.span);}
    if(s.display>=0){Order restore{1,0,1,2,3};ioctl(s.display,LayerOrder,&restore);close(s.display);}
    if(s.fb>=0)close(s.fb);
}
bool NativeVideo::available(){
    if(!env("LILSMOKE_HCFB"))return false;
    const bool decoder=device("/dev/viddec"),allocator=device("/dev/mmz"),overlay=device("/dev/fb0"),display=device("/dev/dis");
    const bool worker=access((std::string(Root)+"/hcplayer-worker").c_str(),R_OK)==0;
    const bool loader=access((std::string(Root)+"/ld.so.1").c_str(),R_OK)==0;
    std::fprintf(stderr,"MEDIA_NATIVE_PROBE viddec=%d mmz=%d primary_overlay=%d display=%d worker=%d loader=%d\n",
        decoder,allocator,overlay,display,worker,loader);
    return decoder&&allocator&&overlay&&display&&worker&&loader;
}
bool NativeVideo::openDisplay(int width,int height){
    auto &s=*state_;s.width=width;s.height=height;
    if(width<320||width>856||height<240||height>480){s.error="Invalid native canvas";return false;}
    s.fb=openDevice("/dev/fb0",O_RDWR);
    if(s.fb<0||ioctl(s.fb,FBIOGET_VSCREENINFO,&s.mode)||ioctl(s.fb,FBIOGET_FSCREENINFO,&s.memory)){
        s.error="Native video overlay could not be opened";return false;
    }
    const auto &v=s.mode;const auto &f=s.memory;
    const uint64_t last=(uint64_t(v.yoffset)+v.yres-1)*f.line_length+(uint64_t(v.xoffset)+v.xres)*4;
    if(std::memcmp(f.id,"hcfb",4)||v.bits_per_pixel!=32||v.red.offset!=16||v.green.offset!=8||v.blue.offset!=0||
       v.red.length!=8||v.green.length!=8||v.blue.length!=8||v.transp.length!=8||v.transp.offset!=24||
       v.xres<320||v.xres>1920||v.yres<240||v.yres>1080||!f.smem_len||f.smem_len>32*1024*1024||
       last>f.smem_len||uint64_t(v.xoffset)+v.xres>v.xres_virtual||uint64_t(v.yoffset)+v.yres>v.yres_virtual||
       (uint64_t(v.xoffset)+v.xres)*4>f.line_length){s.error="Unsupported native overlay geometry";return false;}
    s.span=size_t(last);
    if(ioctl(s.fb,0x20000d04UL,0UL)){s.error="Native overlay cache mode unavailable";return false;}
    void *memory=mmap(nullptr,s.span,PROT_READ|PROT_WRITE,MAP_SHARED,s.fb,0);
    if(memory==MAP_FAILED){s.error="Native overlay memory unavailable";return false;}
    s.mapping=static_cast<uint8_t*>(memory);s.pixels.resize(size_t(width)*height);
    s.blit.reset(width,height,v.xres,v.yres);
    s.display=openDevice("/dev/dis",O_RDWR);
    Screen screen{1,0,0,0,0};
    if(s.display<0||ioctl(s.display,ScreenInfo,&screen)||!screen.width||!screen.height||screen.width>4096||screen.height>2160){s.error="Native HDMI geometry unavailable";return false;}
    s.screenW=screen.width;s.screenH=screen.height;
    Scaling scaling{uint16_t(v.xres),uint16_t(v.yres),screen.width,screen.height};
    Order order{1,2,0,1,3};
    if(ioctl(s.fb,Scale,&scaling)||ioctl(s.display,LayerOrder,&order)||ioctl(s.fb,FBIOBLANK,FB_BLANK_UNBLANK)){
        s.error="Native video plane setup failed";return false;
    }
    std::fprintf(stderr,"MEDIA_NATIVE_OVERLAY canvas=%dx%d fb=%ux%u hdmi=%dx%d\n",width,height,v.xres,v.yres,s.screenW,s.screenH);
    return true;
}
bool NativeVideo::start(const std::string &file,int sourceWidth,int sourceHeight){
    stop();auto &s=*state_;
    if(s.child>0){s.error="Previous native video process did not stop";return false;}
    s.error.clear();s.ready=s.playing=s.paused=false;s.position=s.duration=0;s.partial.clear();
    s.sourceW=sourceWidth;s.sourceH=sourceHeight;
    int commands[2],status[2];
    if(pipe2(commands,O_CLOEXEC|O_NONBLOCK)){s.error="Native command pipe unavailable";return false;}
    if(pipe2(status,O_CLOEXEC|O_NONBLOCK)){close(commands[0]);close(commands[1]);s.error="Native status pipe unavailable";return false;}
    const std::string loader=std::string(Root)+"/ld.so.1",worker=std::string(Root)+"/hcplayer-worker";
    std::vector<std::string> environment;
    for(char **entry=environ;entry&&*entry;++entry){
        if(std::strncmp(*entry,"LD_PRELOAD=",11)&&std::strncmp(*entry,"LD_LIBRARY_PATH=",16)&&
           std::strncmp(*entry,"LD_AUDIT=",9))environment.emplace_back(*entry);
    }
    std::vector<char*> childEnvironment;
    for(auto &entry:environment)childEnvironment.push_back(entry.data());
    childEnvironment.push_back(nullptr);
    char *arguments[]={const_cast<char*>(loader.c_str()),const_cast<char*>("--library-path"),
        const_cast<char*>(Root),const_cast<char*>(worker.c_str()),const_cast<char*>(file.c_str()),nullptr};
    pid_t parent=getpid();s.child=fork();
    if(!s.child){
        if(prctl(PR_SET_PDEATHSIG,SIGTERM)||getppid()!=parent)_exit(126);
        if(dup2(commands[0],STDIN_FILENO)<0||dup2(status[1],STDOUT_FILENO)<0)_exit(126);
        // No allocation/environment mutation after forking SDL's input threads.
        // The isolated glibc process must not inherit musl preload modules.
        rlimit memory{64*1024*1024,64*1024*1024};
        if(setrlimit(RLIMIT_AS,&memory))_exit(126);
        for(int fd=3;fd<1024;++fd)close(fd);
        execve(loader.c_str(),arguments,childEnvironment.data());
        _exit(127);
    }
    close(commands[0]);close(status[1]);
    if(s.child<0){close(commands[1]);close(status[0]);s.error="Native video process unavailable";return false;}
    s.commands=commands[1];s.status=status[0];s.deadline=SDL_GetTicks64()+8000;
    std::fprintf(stderr,"MEDIA_NATIVE_START pid=%ld video_abi=664 audio=musl-pcm\n",long(s.child));return true;
}
void NativeVideo::update(){
    auto &s=*state_;if(s.child<=0||!s.error.empty())return;
    char bytes[256];
    for(unsigned reads=0;reads<8;++reads){
        const ssize_t n=read(s.status,bytes,sizeof(bytes));
        if(n<0&&(errno==EAGAIN||errno==EINTR))break;
        if(n<=0){s.error="Native video process closed unexpectedly";break;}
        for(ssize_t i=0;i<n;++i){
            if(bytes[i]!='\n'){
                if(s.partial.size()>=127){s.error="Native video status overflow";break;}
                s.partial+=bytes[i];continue;
            }
            long long value=0;char tail;
            if(std::sscanf(s.partial.c_str(),"HC_READY %lld %c",&value,&tail)==1&&value>=-1&&value<=7LL*24*60*60*1000){
                // The firmware opens streams asynchronously; -1 is unknown,
                // not a malformed message or evidence that create failed.
                s.ready=true;s.duration=std::max(0LL,value)/1000.0;aspect(s.sourceW,s.sourceH,s.fill);
                if(s.display>=0){Order order{1,2,0,1,3};if(ioctl(s.display,LayerOrder,&order))s.error="Native video layer order failed";}
                std::fprintf(stderr,"MEDIA_NATIVE_READY duration=%.3f\n",s.duration);
            }else if(std::sscanf(s.partial.c_str(),"HC_DURATION %lld %c",&value,&tail)==1&&value>=0&&value<=7LL*24*60*60*1000){
                s.duration=value/1000.0;
            }else if(std::sscanf(s.partial.c_str(),"HC_POSITION %lld %c",&value,&tail)==1&&value>=-1&&value<=7LL*24*60*60*1000){
                if(value>=0){
                    if(value/1000.0>s.position)s.deadline=SDL_GetTicks64()+8000;
                    s.position=value/1000.0;
                }
            }
            else if(s.partial.rfind("HC_ERROR",0)==0)s.error="Native decoder: "+s.partial.substr(9);
            else if(s.partial=="HC_SEEK_FAILED")s.error="Native decoder seek failed";
            else {s.error="Invalid native video status";}
            s.partial.clear();
            if(!s.error.empty())break;
        }
        if(!s.error.empty())break;
    }
    if(!s.ready&&SDL_GetTicks64()>s.deadline)s.error="Native video startup timed out";
    if(s.ready&&s.playing&&!s.paused&&(s.duration<=0||s.position<s.duration-.25)&&SDL_GetTicks64()>s.deadline)
        s.error="Native video clock stopped";
    int result=0;if(waitpid(s.child,&result,WNOHANG)==s.child){
        s.child=-1;
        if(s.error.empty())s.error=WIFSIGNALED(result)?"Native video terminated by signal "+std::to_string(WTERMSIG(result)):
            "Native video exited (status "+std::to_string(WIFEXITED(result)?WEXITSTATUS(result):-1)+")";
    }
}
void NativeVideo::play(){auto &s=*state_;if(s.ready&&!s.playing&&s.send("PLAY")){s.playing=true;s.deadline=SDL_GetTicks64()+8000;}}
void NativeVideo::pause(bool value){auto &s=*state_;if(s.ready){s.send(value?"PAUSE":"RESUME");s.paused=value;s.deadline=SDL_GetTicks64()+8000;}}
bool NativeVideo::seek(double seconds){return std::isfinite(seconds)&&seconds>=0&&seconds<=7*24*60*60&&state_->send("SEEK "+std::to_string(int64_t(seconds*1000)));}
void NativeVideo::aspect(int width,int height,bool fill){
    auto &s=*state_;s.fill=fill;s.sourceW=width;s.sourceH=height;
    if(!s.ready||width<=0||height<=0)return;
    const double ratio=std::min(double(s.screenW)/width,double(s.screenH)/height);
    int w=fill?s.screenW:int(width*ratio),h=fill?s.screenH:int(height*ratio);
    s.send("RECT "+std::to_string(width)+" "+std::to_string(height)+" "+std::to_string((s.screenW-w)/2)+" "+std::to_string((s.screenH-h)/2)+" "+std::to_string(w)+" "+std::to_string(h));
}
bool NativeVideo::present(SDL_Renderer *renderer){
    auto &s=*state_;if(!s.mapping)return false;
    if(SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,s.pixels.data(),s.width*4)){s.error="Native overlay render failed";return false;}
    if(!s.loadingPrepared){
        using Prepare=int(*)(const void*,unsigned,unsigned,unsigned);
        auto prepare=reinterpret_cast<Prepare>(dlsym(RTLD_DEFAULT,"lilsmoke_loading_prepare_rgb"));
        if(prepare){
            const unsigned w=std::min(s.width,640),h=std::min(s.height,480);
            std::vector<uint16_t> first(size_t(w)*h);
            for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
                const auto p=s.pixels[size_t(y)*s.height/h*s.width+size_t(x)*s.width/w];
                first[size_t(y)*w+x]=uint16_t(((p>>8)&0xf800)|((p>>5)&0x07e0)|((p>>3)&0x001f));
            }
            s.loadingPrepared=prepare(first.data(),w,h,w*2)==0;
        }else s.loadingPrepared=true;
    }
    s.blit.copy(s.pixels.data(),s.mapping+size_t(s.mode.yoffset)*s.memory.line_length+s.mode.xoffset*4,s.memory.line_length);
    return true;
}
bool NativeVideo::ready()const{return state_->ready;}
double NativeVideo::position()const{return state_->position;}
double NativeVideo::duration()const{return state_->duration;}
const std::string &NativeVideo::error()const{return state_->error;}
bool NativeVideo::stop(){
    auto &s=*state_;
    if(s.child>0){
        s.send("QUIT");
        for(unsigned i=0;i<60;++i){
            const auto result=waitpid(s.child,nullptr,WNOHANG);
            if(result==s.child||(result<0&&errno==ECHILD)){s.child=-1;break;}
            if(i==20)kill(s.child,SIGTERM);if(i==40)kill(s.child,SIGKILL);
            SDL_Delay(5);
        }
    }
    if(s.commands>=0)close(s.commands);if(s.status>=0)close(s.status);
    s.commands=s.status=-1;s.ready=s.playing=false;
    return s.child<=0;
}
}
