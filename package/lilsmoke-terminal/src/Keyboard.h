// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "Terminal.h"
#include <SDL.h>

namespace lilsmoke {
class Keyboard {
public:
    void event(const SDL_Event &event, Terminal &terminal);
private:
    VTermModifier textModifiers_ = VTERM_MOD_NONE;
    bool suppressText_ = false;
};
}
