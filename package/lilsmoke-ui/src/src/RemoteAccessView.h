#pragma once

#include "Config.h"
#include "View.h"

#include <functional>
#include <string>
#include <vector>

namespace lilsmoke {

class RemoteAccessView : public View {
public:
    explicit RemoteAccessView(Config &config) : config_(config) {}

    void onEnter() override;
    View *handleInput(InputButton button, InputEvent event) override;
    CursorKind cursorAt(int, int) const override { return CursorKind::Help; }
    void render(Renderer &renderer, Font &font, const Theme &theme) override;

    void setOnBack(std::function<View *()> callback) {
        onBack_ = std::move(callback);
    }

private:
    Config &config_;
    std::function<View *()> onBack_;
    std::vector<std::string> lines_;
    int scroll_ = 0;

    void refresh();
};

}  // namespace lilsmoke
