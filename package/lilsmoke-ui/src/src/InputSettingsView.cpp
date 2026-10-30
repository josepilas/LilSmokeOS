// LilSmokeUI — InputSettingsView.cpp

#include "InputSettingsView.h"
#include "Log.h"
#include <algorithm>

namespace lilsmoke {

InputSettingsView::InputSettingsView(Input &input) : input_(input) {
    buttons_ = {
        {InputButton::Up,     "D-pad Up"},
        {InputButton::Down,   "D-pad Down"},
        {InputButton::Left,   "D-pad Left"},
        {InputButton::Right,  "D-pad Right"},
        {InputButton::A,      "A button"},
        {InputButton::B,      "B button"},
        {InputButton::X,      "X button"},
        {InputButton::Y,      "Y button"},
        {InputButton::L1,     "L1 (left shoulder)"},
        {InputButton::R1,     "R1 (right shoulder)"},
        {InputButton::L2,     "L2 (left trigger)"},
        {InputButton::R2,     "R2 (right trigger)"},
        {InputButton::L3,     "L3 (left stick)"},
        {InputButton::R3,     "R3 (right stick)"},
        {InputButton::Select, "Select"},
        {InputButton::Start,  "Start"},
        {InputButton::Home,   "Home / Menu"},
    };
}

void InputSettingsView::onEnter() {
    selected_ = 0;
    waiting_ = false;
    input_.setBindingCapture(false);
}

int InputSettingsView::currentCode(int idx) const {
    if (idx < 0 || idx >= (int)buttons_.size()) return -1;
    InputButton target = buttons_[idx].button;
    for (const auto &b : input_.keymap().bindings()) {
        if (b.action == target) return b.code;
    }
    return -1;
}

View *InputSettingsView::handleInput(InputButton btn, InputEvent evt) {
    if (evt != InputEvent::Pressed) return nullptr;

    if (waiting_) {
        const RawBinding &raw = input_.rawBinding();
        if (raw.cancelled || (btn == InputButton::B && !raw.ready)) {
            waiting_ = false;
            input_.setBindingCapture(false);
            waitingLabel_ = "Cancelled";
            return nullptr;
        }
        if (!raw.ready) return nullptr;
        bool saved = input_.bindCaptured(buttons_[selected_].button);
        waitingLabel_ = input_.bindingStatus();
        if (saved) {
            waiting_ = false;
            input_.setBindingCapture(false);
        }
        return nullptr;
    }

    switch (btn) {
        case InputButton::Up:
            if (selected_ > 0) selected_--;
            return nullptr;
        case InputButton::Down:
            if (selected_ < (int)buttons_.size() - 1) selected_++;
            return nullptr;
        case InputButton::A:
        case InputButton::Start:
            // Start waiting for a key press
            waiting_ = true;
            input_.setBindingCapture(true);
            waitingLabel_ = std::string("Press a key for ") + buttons_[selected_].label + "...";
            return nullptr;
        case InputButton::B:
            if (onBack_) return onBack_();
            return nullptr;
        default:
            return nullptr;
    }
}

void InputSettingsView::render(Renderer &r, Font &font, const Theme &theme) {
    r.clear(0x2B2D31FF);

    // Title
    r.fillRect(0, 0, r.width(), 36, 0x1E2A47FF);
    font.render(r, "Gamepad mapping", 12, 8, 0xD9C9A3FF);

    int y = 46;
    const int rowH = 24;
    const int rows = std::max(1, (r.height() - 100) / rowH);
    const int first = std::max(0, selected_ - rows + 1);
    int labelX = 16;
    int valueX = std::max(130, r.width() - 140);
    int rowW = r.width() - 32;

    for (size_t i = first; i < buttons_.size() && i < static_cast<size_t>(first + rows); i++) {
        bool selected = ((int)i == selected_);
        uint32_t bg = selected ? 0x1F3D2BFF : 0x1E2A4799;

        r.fillRect(labelX, y, rowW, rowH - 2, bg);

        // Label
        font.renderStyled(r, buttons_[i].label, labelX + 8, y + 3,
                          valueX - labelX - 16, rowH - 6, 0xD9C9A3FF, "", 14);

        // Current code
        std::string codeStr = input_.bindingLabel(buttons_[i].button);
        font.renderStyled(r, codeStr, valueX, y + 3, r.width() - valueX - 20,
                          rowH - 6, selected ? 0xD9C9A3FF : 0xD9C9A3AA, "", 12);

        y += rowH;
    }

    // Footer
    int fy = r.height() - 36;
    r.fillRect(0, fy, r.width(), 36, 0x1E2A47FF);
    if (waiting_) {
        font.renderStyled(r, waitingLabel_, 12, fy + 8, r.width() - 24, 24, 0xFFAA66FF, "", 12);

    } else {
        font.renderStyled(r, waitingLabel_, 12, fy + 8, r.width() - 24, 24, 0xD9C9A399, "", 12);
    }
}

}  // namespace lilsmoke
