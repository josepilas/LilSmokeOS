// SPDX-License-Identifier: MPL-2.0
#include "Keyboard.h"
#include <algorithm>
#include <cstring>

namespace lilsmoke {
void Keyboard::event(const SDL_Event &event, Terminal &terminal) {
    if (event.type == SDL_TEXTINPUT) {
        if (suppressText_) return;
        const auto *s = reinterpret_cast<const unsigned char *>(event.text.text);
        const size_t length = strnlen(event.text.text, sizeof(event.text.text));
        for (size_t i = 0; i < length;) {
            uint32_t cp = s[i++];
            int extra = 0;
            if (cp >= 0xf0) { cp &= 7; extra = 3; }
            else if (cp >= 0xe0) { cp &= 15; extra = 2; }
            else if (cp >= 0xc0) { cp &= 31; extra = 1; }
            else if (cp >= 0x80) continue;
            if (i + extra > length) break;
            bool valid = true;
            for (int n = 0; n < extra; ++n) {
                if ((s[i] & 0xc0) != 0x80) { valid = false; break; }
                cp = (cp << 6) | (s[i++] & 0x3f);
            }
            // Some DirectFB versions also emit text for Enter/Tab/Ctrl keys.
            if (valid && cp >= 32 && cp != 127 && cp <= 0x10ffff)
                terminal.character(cp, textModifiers_);
        }
        return;
    }
    if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
        suppressText_ = false;
        textModifiers_ = VTERM_MOD_NONE;
    }
    if (event.type != SDL_KEYDOWN) return;
    const auto key = event.key.keysym.sym;
    const auto mod = event.key.keysym.mod;
    const bool altGr = (mod & KMOD_MODE) || ((mod & KMOD_RALT) && (mod & KMOD_CTRL));
    int modifiers = VTERM_MOD_NONE;
    if (mod & KMOD_SHIFT) modifiers |= VTERM_MOD_SHIFT;
    if (!altGr && (mod & KMOD_ALT)) modifiers |= VTERM_MOD_ALT;
    if (!altGr && (mod & KMOD_CTRL)) modifiers |= VTERM_MOD_CTRL;
    const auto vtmod = static_cast<VTermModifier>(modifiers);
    textModifiers_ = static_cast<VTermModifier>(modifiers & VTERM_MOD_ALT);
    suppressText_ = false;
    if ((mod & KMOD_SHIFT) && (key == SDLK_PAGEUP || key == SDLK_PAGEDOWN)) {
        terminal.scroll((key == SDLK_PAGEUP ? 1 : -1) * std::max(1, terminal.rows() - 1));
        suppressText_ = true;
        return;
    }
    if ((mod & KMOD_SHIFT) && key == SDLK_END) {
        terminal.live(); suppressText_ = true; return;
    }
    VTermKey special = VTERM_KEY_NONE;
    switch (key) {
        case SDLK_RETURN: special = VTERM_KEY_ENTER; break;
        case SDLK_KP_ENTER: special = VTERM_KEY_KP_ENTER; break;
        case SDLK_TAB: special = VTERM_KEY_TAB; break;
        case SDLK_BACKSPACE: special = VTERM_KEY_BACKSPACE; break;
        case SDLK_ESCAPE: special = VTERM_KEY_ESCAPE; break;
        case SDLK_UP: special = VTERM_KEY_UP; break;
        case SDLK_DOWN: special = VTERM_KEY_DOWN; break;
        case SDLK_LEFT: special = VTERM_KEY_LEFT; break;
        case SDLK_RIGHT: special = VTERM_KEY_RIGHT; break;
        case SDLK_HOME: special = VTERM_KEY_HOME; break;
        case SDLK_END: special = VTERM_KEY_END; break;
        case SDLK_INSERT: special = VTERM_KEY_INS; break;
        case SDLK_DELETE: special = VTERM_KEY_DEL; break;
        case SDLK_PAGEUP: special = VTERM_KEY_PAGEUP; break;
        case SDLK_PAGEDOWN: special = VTERM_KEY_PAGEDOWN; break;
        default:
            if (key >= SDLK_F1 && key <= SDLK_F12)
                special = static_cast<VTermKey>(VTERM_KEY_FUNCTION(key - SDLK_F1 + 1));
            break;
    }
    if (special != VTERM_KEY_NONE) {
        terminal.key(special, vtmod);
        suppressText_ = true;
    } else if ((modifiers & VTERM_MOD_CTRL) && key >= 32 && key <= 126) {
        uint32_t cp = key;
        if (mod & KMOD_SHIFT) {
            if (key == SDLK_2) cp = '@';
            if (key == SDLK_6) cp = '^';
            if (key == SDLK_MINUS) cp = '_';
            if (key == SDLK_SLASH) cp = '?';
        }
        terminal.character(cp, vtmod);
        suppressText_ = true;
    }
}
} // namespace lilsmoke
