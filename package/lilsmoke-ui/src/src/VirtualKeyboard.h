#pragma once
#include "Input.h"
#include <string>
#include <vector>

namespace lilsmoke {
class Renderer;
class Font;
enum class KeyboardAction { None, Text, Accept, Cancel };
struct KeyboardResult { KeyboardAction action = KeyboardAction::None; std::string text; };

// A bounded, allocation-light keyboard shared by UI fields and native apps.
class VirtualKeyboard {
public:
    void reset(bool numeric = false);
    KeyboardResult input(InputButton button, InputEvent event);
    KeyboardResult pointer(const PointerEvent &event);
    void render(Renderer &, Font &, const std::string &title,
                const std::string &value, bool secret = false);
    std::string selectedLabel() const;
private:
    std::vector<std::vector<std::string>> rows() const;
    KeyboardResult activate();
    int row_ = 0, column_ = 0, width_ = 320, keyTop_ = 68, keyHeight_ = 32;
    bool numeric_ = false, uppercase_ = false, symbols_ = false;
};
}
