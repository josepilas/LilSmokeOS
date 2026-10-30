#include "EmulatorSettingsView.h"
#include "Renderer.h"
#include "Font.h"

namespace lilsmoke {
EmulatorSettingsView::EmulatorSettingsView(Config &config)
    : config_(config), providers_(emulatorProviders()), systems_(configurableEmulatorSystems(providers_)) {}

std::string EmulatorSettingsView::selectedName(const std::string &system) const {
    std::map<std::string, std::string> values;
    values["EMULATOR_BACKEND"] = config_.get("EMULATOR_BACKEND");
    values["EMULATOR_" + system] = config_.get("EMULATOR_" + system);
    values["SNES_EMULATOR"] = config_.get("SNES_EMULATOR");
    const auto id = selectedEmulator(system, values, providers_);
    for (const auto &p : providers_) if (p.id == id)
        return p.name + (p.available() ? "" : " (unavailable)");
    return "No compatible engine installed";
}

void EmulatorSettingsView::openChoices() {
    choices_ = {""};
    const auto &system = systems_[system_];
    for (const auto &p : providers_)
        if (p.available() && p.needsRom && p.supports(system)) choices_.push_back(p.id);
    choice_ = 0;
    const auto current = config_.get("EMULATOR_" + system);
    for (size_t i = 0; i < choices_.size(); ++i) if (choices_[i] == current) choice_ = static_cast<int>(i);
    choosing_ = true; scroll_ = 0;
}

View *EmulatorSettingsView::handleInput(InputButton button, InputEvent event) {
    if (event != InputEvent::Pressed && event != InputEvent::Repeat) return nullptr;
    int &selected = choosing_ ? choice_ : system_;
    const int count = choosing_ ? static_cast<int>(choices_.size()) : static_cast<int>(systems_.size());
    if (button == InputButton::Up) selected = std::max(0, selected - 1);
    else if (button == InputButton::Down) selected = std::min(count - 1, selected + 1);
    else if (button == InputButton::B && choosing_) { choosing_ = false; scroll_ = 0; }
    else if (event == InputEvent::Pressed && (button == InputButton::A || button == InputButton::Start)) {
        if (!choosing_) openChoices();
        else {
            const auto key = "EMULATOR_" + systems_[system_];
            const auto previous = config_.get(key);
            config_.set(key, choices_[choice_]);
            if (!config_.save()) config_.set(key, previous);
            else { choosing_ = false; scroll_ = 0; }
        }
    }
    return nullptr;
}

View *EmulatorSettingsView::handlePointer(const PointerEvent &event) {
    if (event.type == PointerEventType::Moved || event.type == PointerEventType::Pressed) {
        if (event.x >= 12 && event.x < width_ - 12 && event.y >= 50 && event.y < 50 + rows_ * rowH_) {
            const int index = scroll_ + (event.y - 50) / rowH_;
            const int count = choosing_ ? static_cast<int>(choices_.size()) : static_cast<int>(systems_.size());
            if (index < count) {
                (choosing_ ? choice_ : system_) = index;
                if (event.type == PointerEventType::Pressed && event.button == PointerButton::Primary)
                    return handleInput(InputButton::A, InputEvent::Pressed);
            }
        }
    }
    return View::handlePointer(event);
}

CursorKind EmulatorSettingsView::cursorAt(int x, int y) const {
    return x >= 12 && x < width_ - 12 && y >= 50 && y < 50 + rows_ * rowH_ ? CursorKind::Hand : CursorKind::Normal;
}

void EmulatorSettingsView::render(Renderer &r, Font &font, const Theme &) {
    r.clear(0x24282DFF);
    width_ = r.width();
    font.renderStyled(r, choosing_ ? emulatorSystemName(systems_[system_]) : "Emulator Core Settings",
                      12, 10, width_ - 24, 28, 0xFFFFFFFF, "", 18);
    rows_ = std::max(1, (r.height() - 72) / rowH_);
    const int selected = choosing_ ? choice_ : system_;
    const int count = choosing_ ? static_cast<int>(choices_.size()) : static_cast<int>(systems_.size());
    if (selected < scroll_) scroll_ = selected;
    if (selected >= scroll_ + rows_) scroll_ = selected - rows_ + 1;
    for (int i = scroll_; i < std::min(count, scroll_ + rows_); ++i) {
        const int y = 50 + (i - scroll_) * rowH_;
        if (i == selected) r.fillRect(12, y, width_ - 24, rowH_ - 2, 0x35694FFF);
        if (choosing_) {
            std::string label = "Follow global setting";
            for (const auto &p : providers_) if (p.id == choices_[i])
                label = (p.backend == "retroarch" ? "RetroArch: " : p.backend == "applications" ? "LilSmoke App: " : "OpenDingux: ") + p.name;
            font.renderStyled(r, label, 20, y + 5, width_ - 40, 23, 0xFFFFFFFF, "", 16);
        } else {
            font.renderStyled(r, emulatorSystemName(systems_[i]), 20, y + 5,
                              width_ / 3 - 24, 23, 0xFFFFFFFF, "", 16);
            font.renderStyled(r, selectedName(systems_[i]), width_ / 3, y + 5,
                              width_ * 2 / 3 - 24, 23, 0xD3E2D8FF, "", 16);
        }
    }
}
}
