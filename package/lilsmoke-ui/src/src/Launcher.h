// LilSmokeUI — Launcher.h
//
// When the user picks a game, this view appears briefly, then calls
// execve("/usr/lib/lilsmoke/lilsmoke-launch", ...). The kernel reclaims
// LilSmokeUI's memory while the game runs.
//
// BusyBox inittab respawns LilSmokeUI when the game exits. If execve itself
// fails, this view remains alive and displays the launch error.

#pragma once

#include "View.h"
#include "ROMScanner.h"
#include "Input.h"

#include <string>

namespace lilsmoke {

class Launcher : public View {
public:
    explicit Launcher(const ROM &rom);
    void onEnter() override;
    View *handleInput(InputButton btn, InputEvent evt) override;
    CursorKind cursorAt(int, int) const override {
        return errorMsg_.empty() ? CursorKind::Busy : CursorKind::NotAllowed;
    }
    void render(Renderer &r, Font &font, const Theme &theme) override;

private:
    ROM rom_;
    bool launched_ = false;
    std::string errorMsg_;
};

}  // namespace lilsmoke
