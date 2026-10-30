#pragma once
#include "View.h"
#include "Config.h"
#include "EmulatorRegistry.h"

namespace lilsmoke {
class EmulatorSettingsView : public View {
public:
    explicit EmulatorSettingsView(Config &config);
    View *handleInput(InputButton button, InputEvent event) override;
    View *handlePointer(const PointerEvent &event) override;
    bool allowAutoBackPop() const override { return !choosing_; }
    CursorKind cursorAt(int x, int y) const override;
    void render(Renderer &r, Font &font, const Theme &theme) override;
private:
    Config &config_;
    std::vector<EmulatorProvider> providers_;
    std::vector<std::string> systems_;
    std::vector<std::string> choices_;
    bool choosing_ = false;
    int system_ = 0, choice_ = 0, scroll_ = 0, rows_ = 1, width_ = 0, rowH_ = 30;
    void openChoices();
    std::string selectedName(const std::string &system) const;
};
}
