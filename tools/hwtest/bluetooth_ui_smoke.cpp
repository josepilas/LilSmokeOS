#define SDL_MAIN_HANDLED
#include "BluetoothView.h"
#include "SettingsView.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>
using namespace lilsmoke;
namespace lilsmoke {
struct BluetoothViewTestAccess {
    static void line(BluetoothView &v,const std::string &s) { v.line(s); }
    static int pending(BluetoothView &v) {
        int f[2]; if(pipe2(f,O_CLOEXEC|O_NONBLOCK))throw std::runtime_error("pipe");
        v.input_=f[1]; return f[0];
    }
    static std::string pin(BluetoothView &v) { return v.pin_; }
    static size_t devices(BluetoothView &v) { return v.devices_.size(); }
    static bool adapter(BluetoothView &v) { return v.adapter_; }
};
}
int checks=0;
void check(bool ok,const char *name) {
    if(!ok)throw std::runtime_error(name);
    std::cout<<"PASS "<<name<<'\n'; ++checks;
}
void press(View &v,InputButton b) { v.handleInput(b,InputEvent::Pressed); }
std::string reply(int fd) {
    char b[128]; ssize_t n=read(fd,b,sizeof b); return n>0?std::string(b,n):"";
}
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    try {
        std::filesystem::path out=argv[1]; std::filesystem::create_directories(out);
        std::ofstream(out/"settings.conf")<<"BLUETOOTH_ENABLED=off\n";
        Config config; config.load((out/"settings.conf").string());
        SettingsView settings(config); bool opened=false;
        settings.setOnOpenBluetooth([&]()->View*{opened=true;return nullptr;});
        settings.onEnter();
        for(int i=0;i<25;++i)press(settings,InputButton::Down);
        press(settings,InputButton::A);
        check(opened,"Bluetooth submenu is reachable from Settings");
        BluetoothView view(config);
        BluetoothViewTestAccess::line(view,"ADAPTER\t/org/bluez/hci0\t1");
        check(BluetoothViewTestAccess::adapter(view),"adapter status parsed");
        BluetoothViewTestAccess::line(view,"DEVICE\t12:34:56:78:9A:BC\tTest pad\t1\t1\t0");
        check(BluetoothViewTestAccess::devices(view)==1,"device row parsed");
        BluetoothViewTestAccess::line(view,"DEVICE\tshort");
        check(BluetoothViewTestAccess::devices(view)==1,"malformed row ignored");
        int fd=BluetoothViewTestAccess::pending(view);
        BluetoothViewTestAccess::line(view,"CONFIRM\t123456");
        check(!view.allowAutoBackPop() && reply(fd).empty(),"pairing never auto-confirms");
        press(view,InputButton::A);
        check(reply(fd)=="yes\n","explicit confirmation reaches pairing agent");
        BluetoothViewTestAccess::line(view,"PIN\t");
        check(view.acceptsText() && BluetoothViewTestAccess::pin(view)=="0000","legacy PIN widget has four digits");
        view.handleText("1234"); press(view,InputButton::A);
        check(reply(fd)=="1234\n","keyboard PIN reaches agent");
        BluetoothViewTestAccess::line(view,"PASSKEY\t");
        press(view,InputButton::Up); press(view,InputButton::Right); press(view,InputButton::Down);
        press(view,InputButton::A);
        check(reply(fd)=="190000\n","gamepad digit controls reach agent");
        close(fd);
        int cancelled=BluetoothViewTestAccess::pending(view);
        BluetoothViewTestAccess::line(view,"CONFIRM\t654321");
        press(view,InputButton::B);
        check(reply(cancelled)=="no\n","pairing cancellation rejects confirmation");
        close(cancelled);
        bool backed=false; view.setOnBack([&]()->View*{backed=true;return nullptr;});
        press(view,InputButton::B); check(backed,"Bluetooth view can return to settings");
        SDL_SetMainReady(); setenv("SDL_VIDEODRIVER","dummy",1);
        setenv("LILSMOKE_UI_WINDOWED","1",1); setenv("LILSMOKE_UI_HIDDEN","1",1);
        Theme theme;
        for(auto size: {std::pair<int,int>{640,480},{320,240}}) {
            Renderer renderer; Font font;
            check(renderer.init(size.first,size.second),"software renderer starts");
            check(font.load("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",16),"font loads");
            BluetoothViewTestAccess::line(view,"DISPLAY\t123456");
            view.render(renderer,font,theme);
            auto *surface=SDL_CreateRGBSurfaceWithFormat(0,size.first,size.second,32,SDL_PIXELFORMAT_RGBA32);
            check(surface!=nullptr,"capture allocated");
            check(SDL_RenderReadPixels(static_cast<SDL_Renderer *>(renderer.sdlRenderer()),nullptr,
                  surface->format->format,surface->pixels,surface->pitch)==0,"pairing screen rendered");
            check(IMG_SavePNG(surface,(out/("bluetooth-"+std::to_string(size.first)+".png")).c_str())==0,"pairing image saved");
            SDL_FreeSurface(surface);
        }
        std::cout<<"BLUETOOTH_UI_OK "<<checks<<'\n';
    } catch(const std::exception &e) { std::cerr<<"FAIL "<<e.what()<<'\n';return 1; }
}
