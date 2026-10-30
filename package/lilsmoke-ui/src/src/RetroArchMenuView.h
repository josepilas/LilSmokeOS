// LilSmokeUI - RetroArchMenuView.h
//
// Lightweight handoff screen for the RetroArch app menu.

#pragma once

#include "Input.h"
#include "View.h"

#include <string>

namespace lilsmoke {

class RetroArchMenuView : public View {
public:
    RetroArchMenuView();

    void onEnter() override;
    View *handleInput(InputButton btn, InputEvent evt) override;
    void render(Renderer &r, Font &font, const Theme &theme) override;

private:
    bool launched_ = false;
    std::string message_ = "Starting RetroArch...";

    void run();
};

}  // namespace lilsmoke
