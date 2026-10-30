// LilSmokeUI — InputSettingsView.h
//
// Lets the user remap gamepad buttons. Flow:
//   1. User selects a button to remap (e.g., "A")
//   2. UI shows "Press a key for A..."
//   3. Next evdev event is captured and bound
//   4. Repeat for other buttons
//   5. "Save" writes to /persist/config/input-maps/user.map

#pragma once

#include "View.h"
#include "Config.h"
#include "Input.h"
#include "Keymap.h"
#include "Renderer.h"
#include "Theme.h"
#include "Font.h"

#include <vector>
#include <string>
#include <functional>

namespace lilsmoke {

class InputSettingsView : public View {
public:
    InputSettingsView(Input &input);

    void onEnter() override;
    void onLeave() override { input_.setBindingCapture(false); }
    bool allowAutoBackPop() const override { return false; }
    View *handleInput(InputButton btn, InputEvent evt) override;
    void render(Renderer &r, Font &font, const Theme &theme) override;

    void setOnBack(std::function<View *()> cb) { onBack_ = std::move(cb); }

private:
    Input &input_;
    std::function<View *()> onBack_;

    // The list of buttons the user can remap
    struct RemappableButton {
        InputButton button;
        const char *label;
    };
    std::vector<RemappableButton> buttons_;

    int selected_ = 0;       // index into buttons_
    bool waiting_ = false;   // true when waiting for the user to press a key
    std::string waitingLabel_;

    // Cached display: each button's current evdev code
    int currentCode(int idx) const;
};

}  // namespace lilsmoke
