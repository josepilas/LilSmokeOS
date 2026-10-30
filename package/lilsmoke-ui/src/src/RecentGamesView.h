#pragma once

#include "GameHistory.h"
#include "ROMScanner.h"
#include "View.h"
#include <functional>

namespace lilsmoke {
class RecentGamesView final : public View {
public:
    explicit RecentGamesView(std::function<View *(const ROM &)> launch,
                             GameHistory history = GameHistory());
    void onEnter() override;
    View *handleInput(InputButton button, InputEvent event) override;
    View *handlePointer(const PointerEvent &event) override;
    CursorKind cursorAt(int x, int y) const override;
    void render(Renderer &renderer, Font &font, const Theme &theme) override;
    int selectedIndex() const { return selected_; }
    void setOnOptions(std::function<View *(const ROM &)> options) { options_ = std::move(options); }

private:
    GameHistory history_;
    std::function<View *(const ROM &)> launch_;
    std::function<View *(const ROM &)> options_;
    std::vector<bool> missing_;
    int selected_ = 0, offset_ = 0, visible_ = 1;
    int width_ = 0, hitBottom_ = 0;
    std::string error_;
    int hit(int x, int y) const;
};
} // namespace lilsmoke
