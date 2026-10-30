#define main player_application_main
#include "../../package/lilsmoke-media/src/Player.cpp"
#undef main
#include <cassert>
int main() {
    Renderer renderer;Font font;Player p(renderer,font);
    p.ended=true;p.paused=false;p.action(Action::Pause);
    assert(!p.paused);
    p.ended=false;p.paused=false;p.action(Action::Pause);assert(p.paused);
    p.action(Action::Pause);assert(!p.paused);
    p.button(InputButton::A,InputEvent::Repeat);assert(!p.paused);
    puts("PASS production end-of-media restart, normal pause/resume and repeat rejection");
}
