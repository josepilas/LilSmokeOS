#define SDL_MAIN_HANDLED
#include "Config.h"
#include "Cursor.h"
#include "FileExplorerView.h"
#include "Font.h"
#include "GridView.h"
#include "ListView.h"
#include "Launcher.h"
#include "ProcessLaunch.h"
#include "RetroArchMenuView.h"
#include "RemoteAccessView.h"
#include "Renderer.h"
#include "SettingsView.h"
#include "SystemView.h"
#include "Theme.h"
#include "InputSettingsView.h"
#include "ROMScanner.h"
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace lilsmoke;
namespace fs = std::filesystem;
namespace lilsmoke {
struct InputTestAccess {
    static int attach(Input &input,const fs::path &out) {
        int fds[2]; if(pipe2(fds,O_NONBLOCK|O_CLOEXEC)) throw std::runtime_error("pipe");
        input.inputDir_="/nonexistent-ui-gamepad-test";
        input.gamepadDir_=(out/"gamepad-maps").string();
        Input::Device device;
        device.fd=fds[0]; device.path="qa-gamepad"; device.id="qa-gamepad";
        device.name="QA gamepad"; device.gamepad=true;
        input.devices_.push_back(device);
        return fds[1];
    }
};
}
int checks=0;
void check(bool ok,const char *label) {
    if (!ok) throw std::runtime_error(label);
    ++checks; std::cout<<"PASS "<<label<<"\n";
}
void snapshot(Renderer &r,const fs::path &path) {
    int width=0,height=0;
    check(SDL_GetRendererOutputSize(static_cast<SDL_Renderer*>(r.sdlRenderer()),&width,&height)==0,
          "framebuffer dimensions");
    auto *surface=SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_RGBA32);
    check(surface!=nullptr,"capture surface");
    check(SDL_RenderReadPixels(static_cast<SDL_Renderer*>(r.sdlRenderer()),nullptr,
          surface->format->format,surface->pixels,surface->pitch)==0,"read software framebuffer");
    check(IMG_SavePNG(surface,path.string().c_str())==0,"PNG screenshot");
    SDL_FreeSurface(surface);
}
PointerEvent click(int x,int y,PointerButton button=PointerButton::Primary) {
    PointerEvent p; p.type=PointerEventType::Pressed; p.button=button; p.x=x; p.y=y;
    return p;
}
void press(View &v,InputButton b) { v.handleInput(b,InputEvent::Pressed); }
int main(int argc,char **argv) {
    if(argc!=3) return 2;
    try {
        fs::path out=argv[1]; fs::create_directories(out/"fixture");
        std::ofstream(out/"fixture"/"example.txt")<<"Line one\nLine two\n";
        SDL_SetMainReady(); SDL_SetHint(SDL_HINT_RENDER_DRIVER,"software");
        setenv("LILSMOKE_UI_WINDOWED","1",1);
        setenv("LILSMOKE_UI_HIDDEN","1",1);
        Renderer r; check(r.init(640,480),"software renderer starts");
        Font font; check(font.load("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",16),"font loads");
        Theme theme;
        {
            Input input;
            int writer=InputTestAccess::attach(input,out);
            InputSettingsView mapping(input); mapping.onEnter();
            press(mapping,InputButton::A);
            mapping.handleInput(InputButton::A,InputEvent::Repeat);
            input_event raw{}; raw.type=EV_KEY; raw.code=BTN_TRIGGER_HAPPY1; raw.value=1;
            check(write(writer,&raw,sizeof raw)==sizeof raw,"raw controller event reaches isolated UI input");
            InputButton button; InputEvent event; PointerEvent pointer;
            check(input.poll(button,event,pointer) && button==InputButton::None,
                  "unknown controller button reaches real mapping view");
            mapping.handleInput(button,event);
            check(fs::exists(out/"gamepad-maps/qa-gamepad.map"),"mapping view persists unknown button");
            raw.value=0;
            if(write(writer,&raw,sizeof raw)!=sizeof raw) throw std::runtime_error("controller release");
            while(input.poll(button,event,pointer)) {}
            raw.value=1;
            if(write(writer,&raw,sizeof raw)!=sizeof raw) throw std::runtime_error("controller re-press");
            check(input.poll(button,event,pointer) && button==InputButton::Up,
                  "fresh remapped button uses captured physical code");
            mapping.render(r,font,theme);
            snapshot(r,out/"07-controller-remapping.png");
            mapping.onLeave(); close(writer);
        }
        CursorAtlas cursors; check(cursors.load(r,argv[2]),"all eleven supplied cursor sprites decode");
        r.clear(0xCCCCCCFF);
        for(int i=0;i<11;++i) {
            int x=30+(i%5)*125,y=60+(i/5)*145;
            cursors.draw(r,static_cast<CursorKind>(i),x,y,64);
            font.renderStyled(r,cursorKindName(static_cast<CursorKind>(i)),x,y+80,120,28,
                              0x111111FF,"",12);
        }
        snapshot(r,out/"01-cursors.png");
        int selected=-1;
        ListView list({"First","Second","Third"},[&](int index,const std::string &)->View*{
            selected=index; return nullptr;
        },"Pointer test");
        list.onEnter(); list.render(r,font,theme);
        list.handlePointer(click(630,470));
        check(selected==-1,"blank list click does not activate");
        list.handlePointer(click(30,90));
        check(selected==1,"list click selects and activates hit row");
        Config config;
        std::ofstream(out/"settings.conf")<<"USB_MODE=off\nSSH_ENABLED=off\nPOINTER_ENABLED=on\n";
        config.load((out/"settings.conf").string());
        SettingsView settings(config); bool remote=false;
        settings.setOnOpenRemote([&]()->View*{remote=true;return nullptr;});
        settings.onEnter(); settings.render(r,font,theme);
        settings.handlePointer(click(30,50+3*28+8));
        check(remote,"Settings remote-access row opens callback");
        cursors.draw(r,settings.cursorAt(30,170),30,170,32);
        snapshot(r,out/"02-settings.png");
        config.set("USB_MODE","drive");
        settings.render(r,font,theme);
        settings.handlePointer(click(30,50+2*28+8));
        check(!settings.allowAutoBackPop() && config.get("USB_MODE")=="drive",
              "USB sharing change requires safe-eject confirmation");
        settings.handlePointer(click(630,470));
        check(!settings.allowAutoBackPop(),"blank click cannot confirm USB disconnect");
        press(settings,InputButton::B);
        check(settings.allowAutoBackPop() && config.get("USB_MODE")=="drive",
              "cancel preserves exported mode and settings view");
        config.set("USB_MODE","off");
        RemoteAccessView info(config); info.onEnter(); info.render(r,font,theme);
        snapshot(r,out/"03-remote-status.png");
        FileExplorerView explorer((out/"fixture").string()); explorer.onEnter();
        explorer.render(r,font,theme);
        explorer.handlePointer(click(50,50,PointerButton::Secondary));
        explorer.render(r,font,theme);
        snapshot(r,out/"04-explorer-menu.png");
        press(explorer,InputButton::B);
        press(explorer,InputButton::Select);
        check(explorer.acceptsText(),"Explorer search accepts physical keyboard text");
        explorer.handleText("examplx\be");
        explorer.render(r,font,theme);
        cursors.draw(r,CursorKind::Text,300,205,32);
        snapshot(r,out/"05-keyboard-search.png");
        press(explorer,InputButton::A);
        check(!explorer.acceptsText(),"Explorer text accept closes prompt");
        explorer.render(r,font,theme);
        explorer.handlePointer(click(80,50));
        explorer.render(r,font,theme);
        snapshot(r,out/"06-explorer-open-text.png");
        std::string chosen;
        SystemView system({{"snes","SNES",1},{"__settings","Settings",0}},
            [&](const SystemEntry &e)->View*{chosen=e.id;return nullptr;},nullptr);
        system.onEnter(); system.render(r,font,theme);
        system.handlePointer(click(620,450)); check(chosen.empty(),"blank system click ignored");
        system.handlePointer(click(40,40)); check(chosen=="snes","fallback system hit opens");
        chosen.clear();
        system.handleInput(InputButton::A,InputEvent::Repeat);
        system.handleInput(InputButton::Start,InputEvent::Repeat);
        check(chosen.empty(),"held confirm cannot reopen a system");
        system.handleInput(InputButton::Down,InputEvent::Repeat);
        press(system,InputButton::A);
        check(chosen=="__settings","system directional repeat and fresh confirm work");
        selected=-1;
        list.handleInput(InputButton::A,InputEvent::Repeat);
        list.handleInput(InputButton::Start,InputEvent::Repeat);
        check(selected==-1,"held confirm cannot launch from list");
        list.handleInput(InputButton::Down,InputEvent::Repeat);
        press(list,InputButton::Start);
        check(selected==2,"list directional repeat and fresh Start work");
        ROM first{},second{}; const ROM *launched=nullptr;
        GridView grid({&first,&second},[&](const ROM *rom)->View*{
            launched=rom;return nullptr;
        },"Input regression");
        grid.handleInput(InputButton::A,InputEvent::Repeat);
        grid.handleInput(InputButton::Start,InputEvent::Repeat);
        check(!launched,"held confirm cannot launch from themed grid");
        grid.handleInput(InputButton::Right,InputEvent::Repeat);
        press(grid,InputButton::A);
        check(launched==&second,"grid directional repeat and fresh confirm work");
        ROM queued{};
        queued.system="snes"; queued.path="/storage/roms/snes/Game with spaces.7z";
        Launcher handoff(queued); handoff.onEnter();
        queued.path="changed after request";
        auto request=takeProcessLaunch();
        check(request && request->executable=="/usr/lib/lilsmoke/lilsmoke-launch" &&
              request->arguments==std::vector<std::string>{"lilsmoke-launch","snes","/storage/roms/snes/Game with spaces.7z"},
              "handoff owns arguments independently of UI destruction");
        check(!processLaunchRequested(),"consumed handoff cannot launch twice");
        fs::create_directories(out/"roms/n64");
        for(const char *extension : {".z64",".v64",".n64",".7z"})
            std::ofstream(out/"roms/n64"/(std::string("N64 fixture")+extension)) << "scanner fixture";
        ROMScanner scanner;
        scanner.scan((out/"roms").string());
        auto n64=scanner.bySystem("n64");
        check(n64.size()==4,"N64 scanner recognizes all three byte orders and 7z");
        Launcher n64Handoff(*n64.front()); n64Handoff.onEnter();
        request=takeProcessLaunch();
        check(request && request->arguments.size()==3 && request->arguments[1]=="n64",
              "N64 UI handoff selects console-specific runtime path");
        RetroArchMenuView appMenu; appMenu.onEnter();
        request=takeProcessLaunch();
        check(request && request->arguments==std::vector<std::string>{"lilsmoke-retroarch","menu"},
              "RetroArch app uses the same deferred handoff");
        std::cout<<"REMOTE_UI_SMOKE_OK checks="<<checks<<"\n";
    } catch(const std::exception &e) {
        std::cerr<<"FAIL "<<e.what()<<"\n";return 1;
    }
}
