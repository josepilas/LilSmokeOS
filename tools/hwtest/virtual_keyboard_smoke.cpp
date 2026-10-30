#define SDL_MAIN_HANDLED
#include "VirtualKeyboard.h"
#include "FileExplorerView.h"
#include "BluetoothView.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace lilsmoke;
namespace lilsmoke {
struct BluetoothViewTestAccess {
    static void prompt(BluetoothView &view,const std::string &value) { view.line(value+"\t"); }
};
}
namespace {
int checks = 0;
void check(bool b, const char *message) {
    if (!b) throw std::runtime_error(message);
    ++checks; std::cout << "PASS " << message << '\n';
}
KeyboardResult press(VirtualKeyboard &k, InputButton b) { return k.input(b, InputEvent::Pressed); }
}
int main(int argc, char **argv) {
    try {
        VirtualKeyboard k; k.reset();
        check(k.selectedLabel()=="1", "initial focus is stable");
        check(press(k,InputButton::A).text=="1", "confirm types selected character");
        press(k,InputButton::Down); check(press(k,InputButton::A).text=="q", "directional selection");
        press(k,InputButton::Y); check(press(k,InputButton::A).text=="Q", "shift");
        press(k,InputButton::L1); check(press(k,InputButton::A).text=="!", "symbol page");
        check(press(k,InputButton::X).text=="\b", "delete command");
        check(press(k,InputButton::R1).text==" ", "space command");
        check(press(k,InputButton::Start).action==KeyboardAction::Accept, "Start accepts");
        check(press(k,InputButton::B).action==KeyboardAction::Cancel, "back cancels");
        check(k.input(InputButton::A,InputEvent::Repeat).action==KeyboardAction::None, "held confirm cannot duplicate text");
        check(k.input(InputButton::A,InputEvent::Released).action==KeyboardAction::None, "release cannot type");
        k.reset(true); press(k,InputButton::Y); press(k,InputButton::L1);
        check(press(k,InputButton::A).text=="1", "numeric mode ignores letter page changes");
        press(k,InputButton::Up); check(k.selectedLabel()=="Back", "vertical wrap clamps focus");
        press(k,InputButton::Left); check(k.selectedLabel()=="OK", "horizontal wrap");
        check(press(k,InputButton::A).action==KeyboardAction::Accept, "OK key accepts");
        for (int i=0;i<10000;++i) { press(k,InputButton::Right); press(k,InputButton::Down); }
        check(!k.selectedLabel().empty(), "focus remains valid after repeated navigation");
        auto base=std::filesystem::temp_directory_path()/"lilsmoke-keyboard-smoke";
        std::filesystem::create_directories(base);
        FileExplorerView explorer(base.string()); explorer.onEnter();
        explorer.handleInput(InputButton::Select,InputEvent::Pressed);
        check(explorer.acceptsText() && explorer.textTitle()=="Find", "Explorer declares a text request");
        k.reset(); explorer.handleText(press(k,InputButton::A).text);
        press(k,InputButton::Down); explorer.handleText(press(k,InputButton::A).text);
        check(explorer.textValue()=="1q", "gamepad characters reach Explorer");
        explorer.handleText(press(k,InputButton::X).text);
        check(explorer.textValue()=="1", "gamepad backspace edits Explorer");
        explorer.handleText(std::string(400,'a'));
        check(explorer.textValue().size()==255, "Explorer text remains bounded");
        explorer.handleInput(InputButton::B,InputEvent::Pressed);
        check(!explorer.acceptsText(), "cancel closes field without a shell pop");
        Config config;
        BluetoothView pairing(config);
        BluetoothViewTestAccess::prompt(pairing,"PIN");
        check(pairing.acceptsText() && !pairing.textNumeric(), "legacy Bluetooth PIN requests the full keyboard");
        k.reset(pairing.textNumeric()); pairing.handleText(press(k,InputButton::A).text);
        press(k,InputButton::Down); pairing.handleText(press(k,InputButton::A).text);
        check(pairing.textValue()=="1q", "alphanumeric gamepad PIN replaces the default");
        pairing.handleText(std::string(40,'a'));
        check(pairing.textValue().size()==16, "legacy PIN is bounded to 16 bytes");
        pairing.handleText("\b"); check(pairing.textValue().size()==15, "PIN backspace removes a character");
        BluetoothViewTestAccess::prompt(pairing,"PASSKEY");
        check(pairing.textNumeric(), "Bluetooth passkey requests the numeric keyboard");
        pairing.handleText("1a2b3456789");
        check(pairing.textValue()=="123456", "passkey accepts only six digits");
        for(int i=0;i<6;++i) pairing.handleText("\b");
        check(pairing.textValue().empty(), "passkey can be cleared");
        pairing.handleInput(InputButton::Start,InputEvent::Pressed);
        check(pairing.acceptsText(), "empty passkey cannot submit");
        if (argc==2) {
            std::filesystem::create_directories(argv[1]);
            for (const auto &size : {std::pair<int,int>{320,240},{640,480},{856,480}}) {
                Renderer r; check(r.init(size.first,size.second), "software display opens");
                Font font; check(font.load("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",16), "font opens");
                k.reset(); k.render(r,font,"New folder",std::string(255,'W'));
                SDL_Surface *surface=SDL_CreateRGBSurfaceWithFormat(0,size.first,size.second,32,SDL_PIXELFORMAT_ARGB8888);
                check(surface!=nullptr,"capture surface");
                check(SDL_RenderReadPixels(static_cast<SDL_Renderer *>(r.sdlRenderer()),nullptr,surface->format->format,surface->pixels,surface->pitch)==0,"pixels rendered");
                std::string path=std::string(argv[1])+"/keyboard-"+std::to_string(size.first)+".png";
                check(IMG_SavePNG(surface,path.c_str())==0,"keyboard screenshot saved");
                SDL_FreeSurface(surface);
                k.reset(true); k.render(r,font,"Pairing code","123456",true); r.present();
            }
        }
        std::cout << "KEYBOARD_OK checks=" << checks << '\n';
    } catch (const std::exception &e) { std::cerr<<"FAIL "<<e.what()<<'\n'; return 1; }
}
