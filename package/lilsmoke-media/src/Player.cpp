#include "AudioSink.h"
#include "NativeVideo.h"
#include "Captions.h"
#include "Renderer.h"
#include "Font.h"
#include "Input.h"
#include "Cursor.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <unistd.h>

using namespace lilsmoke;
using namespace lilsmoke::media;
namespace {
volatile sig_atomic_t stopping=0;
void stop(int){stopping=1;}
double now(){return double(SDL_GetPerformanceCounter())/SDL_GetPerformanceFrequency();}
std::string clockText(double seconds) {
    unsigned value=unsigned(std::max(0.0,seconds));char text[40];
    if(value>=3600)std::snprintf(text,sizeof(text),"%u:%02u:%02u",value/3600,value/60%60,value%60);
    else std::snprintf(text,sizeof(text),"%u:%02u",value/60,value%60);return text;
}
int environmentInt(const char *key,int fallback,int lo,int hi) {
    const char *s=std::getenv(key);if(!s)return fallback;char *end=nullptr;long n=std::strtol(s,&end,10);
    return !*s||*end?fallback:int(std::clamp(n,long(lo),long(hi)));
}
enum class Action { Pause,Previous,Next,Stop,Shuffle,Repeat,Options,Playlist,SeekBack,SeekForward,VolumeDown,VolumeUp,Mute,ZoomIn,ZoomOut,Rotate,Fit,Slide,Aspect,Subtitles,Audio,Info,Quit };
const std::vector<std::pair<std::string,Action>> actionNames={{"pause",Action::Pause},{"previous",Action::Previous},{"next",Action::Next},{"stop",Action::Stop},{"shuffle",Action::Shuffle},{"repeat",Action::Repeat},{"options",Action::Options},{"playlist",Action::Playlist},{"seek-back",Action::SeekBack},{"seek-forward",Action::SeekForward},{"volume-down",Action::VolumeDown},{"volume-up",Action::VolumeUp},{"mute",Action::Mute},{"zoom-in",Action::ZoomIn},{"zoom-out",Action::ZoomOut},{"rotate",Action::Rotate},{"fit",Action::Fit},{"slide",Action::Slide},{"aspect",Action::Aspect},{"subtitles",Action::Subtitles},{"audio",Action::Audio},{"info",Action::Info},{"quit",Action::Quit}};
struct TestAction {double at;Action action;};
class Player {
public:
    Player(Renderer &r,Font &f,NativeVideo *native=nullptr):renderer(r),font(f),native(native){}
    ~Player(){if(texture)SDL_DestroyTexture(texture);}
    Renderer &renderer;Font &font;Input input;Playlist list;Decoder decoder;AudioSink sound;
    NativeVideo *native=nullptr;
    bool hardware=false;
    SDL_Texture *texture=nullptr;
    Kind type=Kind::Unknown;
    std::string file,error,tooltip;
    VideoFrame picture;
    std::vector<Caption> captions;
    bool quit=false,power=false,paused=false,ended=false,muted=false,slideshow=false,subtitles=true,stretch=false,dirty=true;
    int volume=80,rotation=0,panel=0,row=0,playlistScroll=0,focused=2;
    double zoom=1,panX=0,panY=0,base=0,started=0,slideAt=0,controlsUntil=0;
    unsigned decoded=0,dropped=0,changes=0;
    int nativeInput=0;
    CursorAtlas cursors;
    int pointerX=0,pointerY=0;
    bool pointerVisible=false;
    double position()const{return paused||ended?base:hardware?native->position():decoder.hasAudio?base+sound.played()/44100.0:base+now()-started;}
    std::vector<std::pair<std::string,Action>> options()const {
        std::vector<std::pair<std::string,Action>> items;
        if(type==Kind::Photo)items={{"Zoom in",Action::ZoomIn},{"Zoom out",Action::ZoomOut},{"Rotate clockwise",Action::Rotate},{"Fit picture",Action::Fit},{slideshow?"Slideshow: On":"Slideshow: Off",Action::Slide}};
        else {
            items={{list.shuffle?"Shuffle: On":"Shuffle: Off",Action::Shuffle},{"Repeat: "+std::string(list.repeat==0?"Off":list.repeat==1?"All":"One"),Action::Repeat},{muted?"Mute: On":"Mute: Off",Action::Mute},{"Volume: "+std::to_string(volume)+"%",Action::VolumeUp}};
            if(decoder.hasVideo) {items.push_back({stretch?"Aspect: Fill":"Aspect: Original",Action::Aspect});items.push_back({captions.empty()?"Subtitles: None":subtitles?"Subtitles: On":"Subtitles: Off",Action::Subtitles});}
            if(decoder.audioTracks>1)items.push_back({"Audio track: "+std::to_string(decoder.audioTrack+1)+" / "+std::to_string(decoder.audioTracks),Action::Audio});
        }
        items.push_back({"Playlist",Action::Playlist});items.push_back({"File information",Action::Info});return items;
    }
    bool load() {
        dirty=true;
        if(native)native->stop();hardware=false;
        sound.close();if(texture){SDL_DestroyTexture(texture);texture=nullptr;}picture={};captions.clear();
        if(list.files.empty()){error="The playlist contains no available local media";return false;}
        file=list.files[list.index];type=kind(file);error.clear();paused=ended=false;zoom=1;panX=panY=0;rotation=0;
        int width=type==Kind::Photo?1536:type==Kind::Audio?256:std::min(854,renderer.width());
        int height=type==Kind::Photo?1536:type==Kind::Audio?256:std::min(480,renderer.height());
        if(!decoder.open(file,type,width,height,native&&type==Kind::Video)){error=decoder.error;return false;}
        hardware=native&&decoder.hasVideo&&type==Kind::Video;
        if(hardware&&!native->start(file,decoder.sourceWidth,decoder.sourceHeight)&&!softwareVideo(native->error()))return false;
        if(decoder.hasAudio&&!decoder.hasVideo)type=Kind::Audio;
        if(decoder.hasAudio&&!sound.open()){error="Audio output could not be opened";return false;}
        auto stem=std::filesystem::path(file);stem.replace_extension(".srt");captions=loadCaptions(stem.string());
        if(captions.empty()){stem.replace_extension(".vtt");captions=loadCaptions(stem.string());}
        base=0;started=now();slideAt=started+5;controlsUntil=started+3;++changes;
        std::fprintf(stderr,"MEDIA_OPEN index=%zu kind=%d source=%dx%d codec=%s audio=%d video=%d\n",list.index,int(type),decoder.sourceWidth,decoder.sourceHeight,decoder.codec.c_str(),decoder.hasAudio,decoder.hasVideo);
        return true;
    }
    bool softwareVideo(const std::string &reason) {
        const std::string failure=reason;
        std::fprintf(stderr,"MEDIA_NATIVE_FALLBACK reason=%s\n",failure.c_str());
        if(native&&!native->stop()){error="Native decoder did not stop; reopen the video";return false;}
        hardware=false;sound.close();
        if(!decoder.open(file,Kind::Video,std::min(640,renderer.width()),std::min(360,renderer.height()),false)){
            error=decoder.error;return false;
        }
        error.clear();dirty=true;return true;
    }
    void pause(bool value) {
        dirty=true;
        if(value==paused)return;
        if(value)base=position();
        else {if(decoder.hasAudio)decoder.seek(base);started=now();}
        paused=value;if(decoder.hasAudio)sound.pause(value);
        if(hardware)native->pause(value);
    }
    void seek(double where) {
        if(type==Kind::Photo)return;
        if(!decoder.seek(where)){std::fprintf(stderr,"MEDIA_SEEK_ERROR %s\n",decoder.error.c_str());tooltip=decoder.error;decoder.error.clear();return;}
        if(hardware&&!native->seek(where)){error=native->error();return;}
        sound.flush();if(paused)sound.pause(true);base=decoder.position;started=now();ended=false;
        std::fprintf(stderr,"MEDIA_SEEK position=%.3f\n",base);
    }
    void action(Action a) {
        dirty=true;
        controlsUntil=now()+4;
        switch(a) {
        case Action::Quit:quit=true;break;
        case Action::Pause:if(ended){seek(0);if(paused)pause(false);}else pause(!paused);break;
        case Action::Previous:if(type!=Kind::Photo&&position()>3){seek(0);break;}if(list.advance(true))load();break;
        case Action::Next:if(list.advance(false))load();break;
        case Action::Stop:seek(0);pause(true);break;
        case Action::Shuffle:list.shuffle=!list.shuffle;break;
        case Action::Repeat:list.repeat=(list.repeat+1)%3;break;
        case Action::Options:panel=panel==1?0:1;row=0;break;
        case Action::Playlist:panel=2;row=int(list.index);break;
        case Action::Info:panel=3;break;
        case Action::SeekBack:seek(std::max(0.0,position()-10));break;
        case Action::SeekForward:seek(position()+10);break;
        case Action::VolumeDown:volume=std::max(0,volume-5);break;
        case Action::VolumeUp:volume=std::min(100,volume+5);break;
        case Action::Mute:muted=!muted;break;
        case Action::ZoomIn:zoom=std::min(8.0,zoom*1.25);break;
        case Action::ZoomOut:zoom=std::max(0.25,zoom/1.25);break;
        case Action::Rotate:rotation=(rotation+90)%360;break;
        case Action::Fit:zoom=1;panX=panY=0;break;
        case Action::Slide:slideshow=!slideshow;slideAt=now()+5;break;
        case Action::Aspect:stretch=!stretch;if(hardware)native->aspect(decoder.sourceWidth,decoder.sourceHeight,stretch);break;
        case Action::Subtitles:subtitles=!subtitles;break;
        case Action::Audio:if(decoder.changeAudio()){sound.flush();base=decoder.position;started=now();}else decoder.error.clear();break;
        }
        std::fprintf(stderr,"MEDIA_ACTION id=%d paused=%d shuffle=%d repeat=%u zoom=%.2f volume=%d index=%zu\n",int(a),paused,list.shuffle,list.repeat,zoom,volume,list.index);
    }
    void button(InputButton b,InputEvent e) {
        if(e!=InputEvent::Pressed&&e!=InputEvent::Repeat)return;
        if(e==InputEvent::Repeat&&(b==InputButton::A||b==InputButton::B||b==InputButton::Start||b==InputButton::Select||b==InputButton::X||b==InputButton::Y||b==InputButton::L1||b==InputButton::R1))return;
        if(b==InputButton::None)return;
        dirty=true;controlsUntil=now()+4;
        if(b==InputButton::Power){power=quit=true;return;}
        if(b==InputButton::B){if(panel)panel=0;else quit=true;return;}
        if(panel) {
            int count=panel==1?int(options().size()):panel==2?int(list.files.size()):1;
            if(b==InputButton::Down)row=std::min(count-1,row+1);
            else if(b==InputButton::Up)row=std::max(0,row-1);
            else if(b==InputButton::L1)row=std::max(0,row-8);
            else if(b==InputButton::R1)row=std::min(count-1,row+8);
            else if(b==InputButton::A||b==InputButton::Start) {
                if(panel==1&&row>=0&&row<count)action(options()[row].second);
                else if(panel==2&&row>=0&&row<count){list.index=size_t(row);load();panel=0;}
            } else if(panel==1&&(b==InputButton::Left||b==InputButton::Right)&&options()[row].second==Action::VolumeUp)action(b==InputButton::Left?Action::VolumeDown:Action::VolumeUp);
            return;
        }
        if(b==InputButton::X){action(Action::Options);return;}
        if(b==InputButton::L1){action(Action::Previous);return;}
        if(b==InputButton::R1){action(Action::Next);return;}
        if(b==InputButton::Y){action(type==Kind::Photo?Action::Slide:Action::Shuffle);return;}
        if(b==InputButton::Select){action(type==Kind::Photo?Action::Fit:Action::Repeat);return;}
        if(b==InputButton::A||b==InputButton::Start){action(type==Kind::Photo?Action::Slide:Action::Pause);return;}
        if(type==Kind::Photo) {
            if(b==InputButton::L2)action(Action::ZoomOut);else if(b==InputButton::R2)action(Action::ZoomIn);
            else if(b==InputButton::Left)panX-=24;else if(b==InputButton::Right)panX+=24;
            else if(b==InputButton::Up)panY-=24;else if(b==InputButton::Down)panY+=24;
            else if(b==InputButton::R3)action(Action::Rotate);
        } else {
            if(b==InputButton::Left||b==InputButton::L2)action(Action::SeekBack);
            else if(b==InputButton::Right||b==InputButton::R2)action(Action::SeekForward);
            else if(b==InputButton::Up||b==InputButton::VolUp)action(Action::VolumeUp);
            else if(b==InputButton::Down||b==InputButton::VolDown)action(Action::VolumeDown);
        }
    }
    std::array<Action,8> toolbar()const {
        if(type==Kind::Photo)return {Action::Previous,Action::Next,Action::Slide,Action::ZoomOut,Action::ZoomIn,Action::Rotate,Action::Playlist,Action::Options};
        return {Action::Previous,Action::Next,Action::Pause,Action::Stop,Action::Shuffle,Action::Repeat,Action::Playlist,Action::Options};
    }
    std::string label(Action a)const {
        switch(a){case Action::Previous:return "Previous";case Action::Next:return "Next";case Action::Pause:return paused?"Play":"Pause";case Action::Stop:return "Stop";case Action::Shuffle:return "Shuffle";case Action::Repeat:return "Repeat";case Action::Options:return "Options";case Action::Playlist:return "Playlist";case Action::Slide:return "Slideshow";case Action::ZoomIn:return "Zoom in";case Action::ZoomOut:return "Zoom out";case Action::Rotate:return "Rotate";default:return "Playback";}
    }
    void pointer(const PointerEvent &p) {
        if(p.type==PointerEventType::None)return;dirty=true;controlsUntil=now()+4;
        const int w=renderer.width(),h=renderer.height(),step=(w-24)/8;
        if(p.type==PointerEventType::Scroll){action(type==Kind::Photo?(p.wheelY>0?Action::ZoomIn:Action::ZoomOut):(p.wheelY>0?Action::VolumeUp:Action::VolumeDown));return;}
        if(p.type==PointerEventType::Moved){
            pointerX=p.x;pointerY=p.y;pointerVisible=true;
            if(type==Kind::Photo&&!panel&&input.pointerState().primaryDown){panX+=p.dx;panY+=p.dy;return;}
            if(p.y>=h-50&&p.x>=12&&p.x<w-12){int i=std::clamp((p.x-12)/step,0,7);tooltip=label(toolbar()[i]);focused=i;}
            else tooltip.clear();return;
        }
        if(p.type!=PointerEventType::Pressed)return;
        if(p.button==PointerButton::Secondary){if(panel)panel=0;else action(Action::Options);return;}
        if(p.button!=PointerButton::Primary)return;
        if(panel&&panel!=3) {
            int i=(p.y-64)/26+playlistScroll;if(p.y>=64&&i>=0){row=i;button(InputButton::A,InputEvent::Pressed);}return;
        }
        if(p.y>=h-50&&p.x>=12&&p.x<w-12)action(toolbar()[std::clamp((p.x-12)/step,0,7)]);
        else if(type!=Kind::Photo&&p.y>=h-78&&p.y<h-52&&decoder.duration>0)seek(std::clamp(double(p.x-14)/(w-28),0.0,1.0)*decoder.duration);
        else if(type==Kind::Photo)action(Action::Slide);else action(Action::Pause);
    }
    void update() {
        if(!error.empty())return;
        if(hardware){
            native->update();
            if(!native->error().empty()){
                const double resume=std::max(0.0,native->position());
                if(!softwareVideo(native->error()))return;
                if(resume>0&&!decoder.seek(resume)){std::fprintf(stderr,"MEDIA_FALLBACK_SEEK %s\n",decoder.error.c_str());decoder.error.clear();}
                if(decoder.hasAudio&&!sound.open()){error="Audio output could not be opened";return;}
                base=decoder.position;started=now();if(paused)sound.pause(true);
            }else {
                if(!native->ready())return;
                if(native->duration()>0)decoder.duration=native->duration();
                if(!paused&&!ended)native->play();
            }
        }
        if(type==Kind::Photo) {
            if(!texture){decoder.pump();if(!decoder.video.empty()){
                show(std::move(decoder.video.front()));
                int sw=decoder.sourceWidth,sh=decoder.sourceHeight;std::string codec=decoder.codec,title=decoder.title;
                decoder.close();decoder.sourceWidth=sw;decoder.sourceHeight=sh;decoder.codec=codec;decoder.title=title;
            }else if(decoder.eof&&decoder.error.empty())error="No picture was decoded";}
            if(slideshow&&now()>=slideAt){if(list.advance(false)){load();}slideAt=now()+5;}
            if(!decoder.error.empty())error=decoder.error;return;
        }
        if(paused||ended)return;
        if(decoder.hasAudio&&sound.failed()){error=sound.failure();return;}
        decoder.setVideoClock(position());
        if(decoder.hasAudio)sound.feed(decoder,volume,muted);
        for(unsigned i=0;i<3&&(!hardware||decoder.hasAudio);++i) {
            if(!decoder.pump()&&!decoder.error.empty()){error=decoder.error;return;}
            if(decoder.hasAudio)sound.feed(decoder,volume,muted);
            if(decoder.audioFrames()>=8820||decoder.video.size()>=3)break;
        }
        double time=position();decoder.position=time;
        while(!decoder.video.empty()&&decoder.video.front().pts<=time+.015) {
            VideoFrame f=std::move(decoder.video.front());decoder.video.pop_front();
            if(!decoder.video.empty()&&f.pts<time-.080){++dropped;continue;}show(std::move(f));
        }
        const bool nativeEnd=!hardware||(decoder.duration>0&&time>=decoder.duration-.25);
        if((decoder.eof||(hardware&&!decoder.hasAudio))&&!decoder.audioFrames()&&decoder.video.empty()&&sound.empty()&&nativeEnd) {
            base=decoder.duration>0?decoder.duration:time;ended=true;
            if(list.advance(false,true)){load();}
        }
    }
    void show(VideoFrame f) {
        dirty=true;
        auto *r=static_cast<SDL_Renderer*>(renderer.sdlRenderer());
        if(!texture)rotation=f.rotation;
        const bool argb=!f.argb.empty();
        if(!texture||f.width!=picture.width||f.height!=picture.height||argb!=!picture.argb.empty()) {
            if(texture)SDL_DestroyTexture(texture);texture=SDL_CreateTexture(r,argb?SDL_PIXELFORMAT_ARGB8888:SDL_PIXELFORMAT_RGB565,SDL_TEXTUREACCESS_STREAMING,f.width,f.height);
            if(texture)SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_NONE);
        }
        if(!texture){error="Not enough display memory";return;}
        if(SDL_UpdateTexture(texture,nullptr,argb?static_cast<const void*>(f.argb.data()):f.pixels.data(),f.width*(argb?4:2))){error="Picture upload failed";return;}
        picture=std::move(f);++decoded;
    }
    void icon(Action a,int x,int y,int size) {
        const int cx=x+size/2,cy=y+17;auto *r=static_cast<SDL_Renderer*>(renderer.sdlRenderer());
        SDL_SetRenderDrawColor(r,225,234,237,255);
        auto triangle=[&](int left,bool reverse){for(int i=0;i<12;++i){int xx=reverse?left+11-i:left+i;SDL_RenderDrawLine(r,xx,cy-i/2-1,xx,cy+i/2+1);}};
        if(a==Action::Pause||a==Action::Slide) {
            bool playing=a==Action::Slide?slideshow:!paused&&!ended;
            if(playing){renderer.fillRect(cx-6,cy-7,4,14,0xe1eaedff);renderer.fillRect(cx+2,cy-7,4,14,0xe1eaedff);}else triangle(cx-5,true);
        } else if(a==Action::Previous||a==Action::Next){triangle(cx-6,a==Action::Next);renderer.fillRect(a==Action::Next?cx+8:cx-9,cy-7,2,14,0xe1eaedff);}
        else if(a==Action::Stop)renderer.fillRect(cx-6,cy-6,12,12,0xe1eaedff);
        else if(a==Action::ZoomIn||a==Action::ZoomOut){renderer.drawRect(cx-7,cy-7,14,14,0xe1eaedff);renderer.fillRect(cx-4,cy,9,2,0xe1eaedff);if(a==Action::ZoomIn)renderer.fillRect(cx,cy-4,2,9,0xe1eaedff);}
        else if(a==Action::Playlist||a==Action::Options){for(int i=-1;i<=1;++i){renderer.fillRect(cx-8,cy+i*5,16,2,0xe1eaedff);if(a==Action::Options)renderer.fillRect(cx+(i==0?-4:3),cy+i*5-2,3,6,0xe1eaedff);}}
        else if(a==Action::Shuffle){SDL_RenderDrawLine(r,cx-8,cy-5,cx+8,cy+5);SDL_RenderDrawLine(r,cx-8,cy+5,cx+8,cy-5);triangle(cx+4,true);}
        else {renderer.drawRect(cx-8,cy-6,16,12,0xe1eaedff);renderer.fillRect(cx-3,cy+5,7,3,0x15191cff);triangle(cx+3,true);}
    }
    void render() {
        const int w=renderer.width(),h=renderer.height();renderer.clear(hardware?0:0x111416ff);
        bool controls=type==Kind::Audio||type==Kind::Photo||paused||ended||panel||now()<controlsUntil||!error.empty();
        if(texture&&type!=Kind::Audio) {
            int top=controls?44:0,bottom=controls?94:0,available=h-top-bottom;
            double pw=picture.width,ph=picture.height;if(rotation%180)std::swap(pw,ph);
            double fit=std::min((w-12)/pw,available/ph),factor=fit*(type==Kind::Photo?zoom:1);
            int dw=int(picture.width*factor),dh=int(picture.height*factor);
            panX=std::clamp(panX,-std::max(0.0,(pw*factor-w)/2),std::max(0.0,(pw*factor-w)/2));
            panY=std::clamp(panY,-std::max(0.0,(ph*factor-available)/2),std::max(0.0,(ph*factor-available)/2));
            int x=(w-dw)/2+int(panX),y=top+(available-dh)/2+int(panY);
            renderer.setClipRect(0,top,w,available);
            if(stretch&&type!=Kind::Photo)renderer.drawTexture(texture,0,top,w,available);
            else renderer.drawTextureEx(texture,x,y,dw,dh,rotation,dw/2,dh/2);
            renderer.clearClipRect();
        } else if(type==Kind::Audio) {
            int center=44+(h-134)/2;
            int left=20,textWidth=w-40;
            if(texture&&w>=480) {int size=std::min(160,h-160);renderer.drawTexture(texture,20,center-size/2,size,size);left=size+36;textWidth=w-left-20;}
            font.renderStyled(renderer,decoder.title,left,center-50,textWidth,48,0xf5f7f8ff,"",h<300?17:23,"center","middle",true);
            font.renderStyled(renderer,decoder.artist,left,center+4,textWidth,23,0x86d5d1ff,"",h<300?13:17,"center");
            font.renderStyled(renderer,decoder.album,left,center+30,textWidth,20,0xa5b2b9ff,"",13,"center");
        }
        if(!error.empty())font.renderStyled(renderer,error,18,70,w-36,h-160,0xff9c86ff,"",16,"center","middle",true);
        if(subtitles&&!captions.empty()&&type!=Kind::Photo)for(const auto &c:captions)if(position()>=c.begin&&position()<=c.end){font.renderStyled(renderer,c.text,18,h-(controls?142:65),w-36,52,0xffffffff,"",17,"center","bottom",true);break;}
        if(controls) {
            renderer.fillRect(0,0,w,40,0x22282cff);
            font.renderStyled(renderer,std::filesystem::path(file).filename().string(),12,10,w-90,22,0xe3e9edff,"",15);
            font.renderStyled(renderer,std::to_string(list.index+1)+" / "+std::to_string(list.files.size()),w-75,10,63,22,0x86d5d1ff,"",13,"right");
            renderer.fillRect(0,h-90,w,90,0x20262aff);
            if(type!=Kind::Photo) {
                renderer.fillRect(14,h-73,w-28,5,0x444d53ff);
                if(decoder.duration>0)renderer.fillRect(14,h-73,int((w-28)*std::clamp(position()/decoder.duration,0.0,1.0)),5,0x63d8c6ff);
                font.renderStyled(renderer,clockText(position())+" / "+clockText(decoder.duration),14,h-62,w-28,15,0xc4cdd3ff,"",11,"center");
            } else font.renderStyled(renderer,std::to_string(decoder.sourceWidth?decoder.sourceWidth:picture.width)+" x "+std::to_string(decoder.sourceHeight?decoder.sourceHeight:picture.height)+"   "+std::to_string(int(zoom*100))+"%",14,h-75,w-28,20,0xc4cdd3ff,"",12,"center");
            int step=(w-24)/8;auto bar=toolbar();
            for(int i=0;i<8;++i){bool active=(bar[i]==Action::Shuffle&&list.shuffle)||(bar[i]==Action::Repeat&&list.repeat)||(bar[i]==Action::Slide&&slideshow);renderer.fillRect(12+i*step,h-45,step-5,34,active?0x266d66ff:0x303a40ff);icon(bar[i],12+i*step,h-45,step-5);}
            if(!tooltip.empty())font.renderStyled(renderer,tooltip,12,h-117,w-24,20,0xe7ebedff,"",12,"center");
        }
        if(panel) {
            renderer.fillRect(0,40,w,h-40,0x151a1eff);
            font.render(renderer,panel==1?"Playback options":panel==2?"Playlist":"File information",14,46,0x83d8cbff);
            if(panel==3) {
                std::string info=file+"\n\n"+decoder.codec+"\n"+std::to_string(decoder.sourceWidth)+" x "+std::to_string(decoder.sourceHeight)+"\n"+clockText(decoder.duration)+"\nAudio tracks: "+std::to_string(decoder.audioTracks);
                font.renderStyled(renderer,info,14,80,w-28,h-100,0xd7dfe4ff,"",15,"left","top",true);
            } else {
                auto opts=options();int count=panel==1?int(opts.size()):int(list.files.size()),visible=std::max(1,(h-78)/26);
                if(row<playlistScroll)playlistScroll=row;if(row>=playlistScroll+visible)playlistScroll=row-visible+1;
                for(int i=playlistScroll;i<std::min(count,playlistScroll+visible);++i) {
                    int y=72+(i-playlistScroll)*26;
                    if(i==row)renderer.fillRect(8,y-2,w-16,25,0x275b55ff);
                    std::string text=panel==1?opts[i].first:std::filesystem::path(list.files[i]).filename().string();
                    font.renderStyled(renderer,text,15,y,w-30,23,0xe0e6eaff,"",14);
                }
            }
        }
    }
};
}
int main(int argc,char **argv) {
    if(argc<2)return 2;
    const bool qa=std::getenv("LILSMOKE_MEDIA_QA")&&std::string(std::getenv("LILSMOKE_MEDIA_QA"))=="1";
    std::vector<TestAction> actions;double qaSeconds=0;std::string captures;
    for(int i=2;i<argc;++i) {
        if(!qa)return 2;
        if(std::string(argv[i])=="--qa-seconds"&&i+1<argc)qaSeconds=std::clamp(std::atof(argv[++i]),1.0,120.0);
        else if(std::string(argv[i])=="--qa-actions"&&i+1<argc) {
            std::ifstream script(argv[++i]);double at;std::string action;
            while(script>>at>>action&&actions.size()<256)for(const auto &entry:actionNames)if(entry.first==action)actions.push_back({at,entry.second});
        } else if(std::string(argv[i])=="--qa-captures"&&i+1<argc)captures=argv[++i];else return 2;
    }
    if(kind(argv[1])==Kind::Unknown||!localFile(argv[1]))return 2;
    std::signal(SIGINT,stop);std::signal(SIGTERM,stop);
    setenv("SDL_VIDEODRIVER","directfb",0);setenv("SDL_RENDER_DRIVER","software",1);
    int width=environmentInt("LILSMOKE_SCREEN_WIDTH",640,320,854),height=environmentInt("LILSMOKE_SCREEN_HEIGHT",480,240,480);
    if(const char *native=std::getenv("LILSMOKE_HCFB");native&&std::string(native)=="1") {
        // hcfb rounds 32-bit modes to four pixels. The boot logo can still
        // occupy a larger mode, so normalize the fallback request as well.
        width=(width+3)&~3;
        const int fd=open("/dev/fb0",O_RDONLY|O_CLOEXEC);
        fb_var_screeninfo mode{};fb_fix_screeninfo memory{};
        if(fd>=0&&ioctl(fd,FBIOGET_VSCREENINFO,&mode)==0&&ioctl(fd,FBIOGET_FSCREENINFO,&memory)==0&&
           std::memcmp(memory.id,"hcfb",4)==0&&mode.xres>=320&&mode.xres<=856&&mode.yres>=240&&mode.yres<=480) {
            width=int(mode.xres);height=int(mode.yres);
            std::fprintf(stderr,"MEDIA_NATIVE_CANVAS %dx%d\n",width,height);
        }
        if(fd>=0)close(fd);
    }
    const bool useNative=kind(argv[1])==Kind::Video&&NativeVideo::available();
    NativeVideo native;
    Renderer renderer;if(!(useNative?renderer.initOffscreen(width,height):renderer.init(width,height)))return 3;
    if(useNative){
        if(!native.openDisplay(width,height)){std::fprintf(stderr,"MEDIA_NATIVE_ERROR %s\n",native.error().c_str());return 3;}
    }
    Font font;if(!font.load(std::getenv("LILSMOKE_MEDIA_FONT")?std::getenv("LILSMOKE_MEDIA_FONT"):"/usr/share/fonts/dejavu/DejaVuSans.ttf",16))return 3;
    Player player(renderer,font,useNative?&native:nullptr);player.list.files=collection(argv[1]);
    auto selected=std::find(player.list.files.begin(),player.list.files.end(),std::filesystem::absolute(argv[1]).string());
    if(selected!=player.list.files.end())player.list.index=size_t(selected-player.list.files.begin());
    player.nativeInput=player.input.init(width,height);player.input.setKeyboardNavigation(true);
    player.cursors.load(renderer,"/usr/share/lilsmoke/cursors/lilsmoke-cursors.webp");
    player.input.setPointerOptions(true,"normal");SDL_ShowCursor(SDL_DISABLE);
    player.volume=environmentInt("LILSMOKE_MEDIA_VOLUME",80,0,100);player.load();
    const double begin=now();size_t actionIndex=0;unsigned frames=0,capture=0;
    double nextCapture=.5,lastRender=-1,nextProfile=5,updateMs=0,renderMs=0,presentMs=0;
    auto profile=[&] {
        const auto &p=player.decoder.profile;
        std::fprintf(stderr,"MEDIA_PERF wall_ms=%.0f update_ms=%.3f demux_ms=%.3f decode_ms=%.3f convert_ms=%.3f render_ms=%.3f present_ms=%.3f ref_frames=%llu converted=%llu pixels=%llu shown=%u drop_convert=%llu drop_present=%u audio_clock=%.3f picture_pts=%.3f sync_est_ms=%.3f queued_pcm=%zu\n",
            (now()-begin)*1000,updateMs,p.demuxUs/1000.0,p.decodeUs/1000.0,p.convertUs/1000.0,renderMs,presentMs,
            (unsigned long long)p.received,(unsigned long long)p.converted,(unsigned long long)p.pixels,player.decoded,
            (unsigned long long)player.decoder.discardedVideoFrames,player.dropped,player.position(),player.picture.pts,
            player.texture?(player.position()-player.picture.pts)*1000:0,player.decoder.audioFrames());
    };
    while(!stopping&&!player.quit) {
        Uint32 frameBegin=SDL_GetTicks();
        double elapsed=now()-begin;
        if(qa&&qaSeconds&&elapsed>=qaSeconds)break;
        while(actionIndex<actions.size()&&elapsed>=actions[actionIndex].at)player.action(actions[actionIndex++].action);
        SDL_Event event;
        while(SDL_PollEvent(&event)) {
            player.input.handleSDLEvent(event);
            if(event.type==SDL_QUIT)player.quit=true;
            if(!player.nativeInput&&event.type==SDL_KEYDOWN) {
                InputButton b=InputButton::None;
                switch(event.key.keysym.sym){case SDLK_ESCAPE:b=InputButton::B;break;case SDLK_SPACE:case SDLK_RETURN:b=InputButton::A;break;case SDLK_LEFT:b=InputButton::Left;break;case SDLK_RIGHT:b=InputButton::Right;break;case SDLK_UP:b=InputButton::Up;break;case SDLK_DOWN:b=InputButton::Down;break;case SDLK_x:b=InputButton::X;break;case SDLK_y:b=InputButton::Y;break;case SDLK_PAGEUP:b=InputButton::L1;break;case SDLK_PAGEDOWN:b=InputButton::R1;break;default:break;}
                player.button(b,event.key.repeat?InputEvent::Repeat:InputEvent::Pressed);
            }
            if(!player.nativeInput&&event.type==SDL_MOUSEBUTTONDOWN)player.pointer({PointerEventType::Pressed,event.button.button==SDL_BUTTON_LEFT?PointerButton::Primary:PointerButton::Secondary,event.button.x,event.button.y});
            if(!player.nativeInput&&event.type==SDL_MOUSEMOTION)player.pointer({PointerEventType::Moved,PointerButton::None,event.motion.x,event.motion.y,event.motion.xrel,event.motion.yrel});
        }
        InputButton button;InputEvent action;PointerEvent pointer;
        for(unsigned n=0;n<128&&player.input.poll(button,action,pointer);++n){player.button(button,action);player.pointer(pointer);}
        if(player.type==Kind::Photo&&!player.panel&&player.zoom>1) {
            const auto sticks=player.input.analogState();
            if(std::abs(int(sticks.leftX))>6000){player.panX+=sticks.leftX*12.0/32767;player.dirty=true;}
            if(std::abs(int(sticks.leftY))>6000){player.panY+=sticks.leftY*12.0/32767;player.dirty=true;}
        }
        const double updateBegin=now();player.update();updateMs+=(now()-updateBegin)*1000;
        const bool render=player.dirty||elapsed-lastRender>=.25;
        if(render){const double renderBegin=now();player.render();renderMs+=(now()-renderBegin)*1000;player.dirty=false;lastRender=elapsed;}
        if(!captures.empty()&&elapsed>=nextCapture&&capture<6) {
            auto *surface=SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_ARGB8888);
            if(surface){SDL_RenderReadPixels(static_cast<SDL_Renderer*>(renderer.sdlRenderer()),nullptr,SDL_PIXELFORMAT_ARGB8888,surface->pixels,surface->pitch);std::string file=captures+"/frame-"+std::to_string(capture++)+".bmp";SDL_SaveBMP(surface,file.c_str());SDL_FreeSurface(surface);}nextCapture+=1;
        }
        if(render) {
            const double presentBegin=now();
            if(player.pointerVisible)player.cursors.draw(renderer,player.type==Kind::Photo&&player.zoom>1?CursorKind::Move:CursorKind::Normal,player.pointerX,player.pointerY,24);
            if(useNative){if(!native.present(static_cast<SDL_Renderer*>(renderer.sdlRenderer())))player.error=native.error();}
            else renderer.present();++frames;
            presentMs+=(now()-presentBegin)*1000;
        }
        if(player.type==Kind::Video&&elapsed>=nextProfile){profile();nextProfile=elapsed+5;}
        Uint32 work=SDL_GetTicks()-frameBegin;if(work<8)SDL_Delay(8-work);
    }
    if(player.type==Kind::Video)profile();
    std::fprintf(stderr,"MEDIA_DONE frames=%u decoded=%u dropped=%u changes=%u audio_frames=%llu position=%.3f error=%s\n",frames,player.decoded,player.dropped,player.changes,(unsigned long long)player.sound.submitted(),player.position(),player.error.c_str());
    return player.power?76:player.error.empty()?0:1;
}
