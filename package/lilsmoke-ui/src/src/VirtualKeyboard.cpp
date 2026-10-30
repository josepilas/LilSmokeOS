#include "VirtualKeyboard.h"
#include "Renderer.h"
#include "Font.h"
#include <algorithm>
#include <cctype>

namespace lilsmoke {
void VirtualKeyboard::reset(bool numeric) {
    numeric_ = numeric; uppercase_ = symbols_ = false; row_ = column_ = 0;
}
std::vector<std::vector<std::string>> VirtualKeyboard::rows() const {
    std::vector<std::vector<std::string>> result;
    const std::vector<std::string> lines = numeric_ ?
        std::vector<std::string>{"1234567890"} : symbols_ ?
        std::vector<std::string>{"1234567890", "!@#$%^&*()", "-_=+[]{}\\|", ";:'\",.<>/?"} :
        std::vector<std::string>{"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm_./"};
    for (const auto &line : lines) {
        std::vector<std::string> keys;
        for (unsigned char c : line) keys.emplace_back(1, uppercase_ ? char(std::toupper(c)) : char(c));
        result.push_back(std::move(keys));
    }
    result.push_back(numeric_ ? std::vector<std::string>{"Back", "Cancel", "OK"} :
                              std::vector<std::string>{"Shift", "#+=", "Space", "Back", "Cancel", "OK"});
    return result;
}
std::string VirtualKeyboard::selectedLabel() const { return rows()[row_][column_]; }
KeyboardResult VirtualKeyboard::activate() {
    const auto key = selectedLabel();
    if (key == "OK") return {KeyboardAction::Accept, {}};
    if (key == "Cancel") return {KeyboardAction::Cancel, {}};
    if (key == "Shift") { uppercase_ = !uppercase_; return {}; }
    if (key == "#+=") { symbols_ = !symbols_; return {}; }
    return {KeyboardAction::Text, key == "Space" ? " " : key == "Back" ? "\b" : key};
}
KeyboardResult VirtualKeyboard::input(InputButton b, InputEvent e) {
    if (e != InputEvent::Pressed && e != InputEvent::Repeat) return {};
    const auto keys = rows();
    if (b == InputButton::Up || b == InputButton::Down) {
        row_ = (row_ + int(keys.size()) + (b == InputButton::Up ? -1 : 1)) % int(keys.size());
        column_ = std::min(column_, int(keys[row_].size()) - 1);
    } else if (b == InputButton::Left || b == InputButton::Right) {
        column_ = (column_ + int(keys[row_].size()) + (b == InputButton::Left ? -1 : 1)) % int(keys[row_].size());
    } else if (e == InputEvent::Pressed) {
        if (b == InputButton::A) return activate();
        if (b == InputButton::B) return {KeyboardAction::Cancel, {}};
        if (b == InputButton::Start) return {KeyboardAction::Accept, {}};
        if (b == InputButton::X) return {KeyboardAction::Text, "\b"};
        if (!numeric_ && b == InputButton::Y) uppercase_ = !uppercase_;
        if (!numeric_ && b == InputButton::L1) symbols_ = !symbols_;
        if (!numeric_ && b == InputButton::R1) return {KeyboardAction::Text, " "};
    }
    return {};
}
KeyboardResult VirtualKeyboard::pointer(const PointerEvent &e) {
    if (e.type == PointerEventType::Pressed && e.button == PointerButton::Secondary)
        return {KeyboardAction::Cancel, {}};
    if (e.type != PointerEventType::Moved && e.type != PointerEventType::Pressed) return {};
    const auto keys = rows();
    if (e.x < 8 || e.x >= width_ - 8 || e.y < keyTop_ || e.y >= keyTop_ + int(keys.size()) * keyHeight_)
        return {};
    row_ = (e.y - keyTop_) / keyHeight_;
    column_ = std::min(int(keys[row_].size()) - 1, (e.x - 8) * int(keys[row_].size()) / (width_ - 16));
    return e.type == PointerEventType::Pressed && e.button == PointerButton::Primary ? activate() : KeyboardResult{};
}
void VirtualKeyboard::render(Renderer &r, Font &font, const std::string &title,
                             const std::string &value, bool secret) {
    width_ = r.width(); keyTop_ = 68;
    const auto keys = rows();
    keyHeight_ = std::max(18, std::min(42, (r.height() - keyTop_ - 8) / int(keys.size())));
    r.clear(0x20242AFF);
    font.renderStyled(r, title, 12, 8, width_ - 24, 22, 0xFFFFFFFF, "", 16);
    r.fillRect(8, 34, width_ - 16, 26, 0x30383FFF);
    // Find a fitting UTF-8 suffix without measuring every possible substring.
    auto shown = secret ? std::string(value.size(), '*') : value;
    int textWidth = 0, textHeight = 0;
    font.textSizeStyled(shown, "", 14, textWidth, textHeight);
    if (textWidth > width_ - 24) {
        std::vector<size_t> boundaries;
        for (size_t i = 0; i < shown.size(); ++i)
            if ((static_cast<unsigned char>(shown[i]) & 0xC0) != 0x80)
                boundaries.push_back(i);
        boundaries.push_back(shown.size());
        size_t low = 0, high = boundaries.size() - 1;
        while (low < high) {
            const size_t middle = low + (high - low) / 2;
            font.textSizeStyled(shown.substr(boundaries[middle]), "", 14, textWidth, textHeight);
            if (textWidth <= width_ - 24) high = middle;
            else low = middle + 1;
        }
        shown.erase(0, boundaries[low]);
    }
    r.setClipRect(12, 36, width_ - 24, 22);
    font.renderStyled(r, shown,
                      12, 38, width_ - 24, 22, 0xE8C46AFF, "", 14);
    r.clearClipRect();
    for (int y = 0; y < int(keys.size()); ++y) {
        const int count = int(keys[y].size());
        for (int x = 0; x < count; ++x) {
            const int left = 8 + x * (width_ - 16) / count;
            const int right = 8 + (x + 1) * (width_ - 16) / count;
            r.fillRect(left + 1, keyTop_ + y * keyHeight_ + 1, right - left - 2, keyHeight_ - 2,
                       y == row_ && x == column_ ? 0x315944FF : 0x30383FFF);
            font.renderStyled(r, keys[y][x], left + 2, keyTop_ + y * keyHeight_ + 4,
                              right - left - 4, keyHeight_ - 6, 0xFFFFFFFF, "", 12, "center", "center");
        }
    }
}
}
