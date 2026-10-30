// LilSmokeUI - RetroArch app handoff.

#include "RetroArchMenuView.h"
#include "Log.h"
#include "ProcessLaunch.h"

namespace lilsmoke {

RetroArchMenuView::RetroArchMenuView() = default;

void RetroArchMenuView::onEnter() {
    if (launched_) return;
    launched_ = true;
    run();
}

void RetroArchMenuView::run() {
    LOG_INFO("RetroArch app: opening menu");

    requestProcessLaunch({"/usr/lib/lilsmoke/lilsmoke-retroarch", {"lilsmoke-retroarch", "menu"}});
}

View *RetroArchMenuView::handleInput(InputButton btn, InputEvent evt) {
    if (evt == InputEvent::Pressed && btn == InputButton::B) return nullptr;
    return nullptr;
}

void RetroArchMenuView::render(Renderer &r, Font &font, const Theme &) {
    r.clear(0x2B2D31FF);
    font.render(r, "RetroArch", 16, r.height() / 2 - 32, 0xD9C9A3FF);
    font.render(r, message_, 16, r.height() / 2 - 8, 0xFF9090FF);
    font.render(r, "Press B to go back", 16, r.height() - 32, 0xD9C9A399);
}

}  // namespace lilsmoke
