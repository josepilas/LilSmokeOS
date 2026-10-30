#pragma once
#include "FavoritesStore.h"
#include "View.h"
#include <functional>

namespace lilsmoke {
class GameOptionsView final : public View {
public:
    GameOptionsView(ROM rom, std::function<View *(const ROM &)> launch,
                    std::function<void()> back, FavoritesStore store = FavoritesStore());
    void onEnter() override;
    View *handleInput(InputButton button, InputEvent event) override;
    View *handlePointer(const PointerEvent &event) override;
    CursorKind cursorAt(int x, int y) const override;
    void render(Renderer &r, Font &font, const Theme &) override;
    bool allowAutoBackPop() const override { return false; }
private:
    ROM rom_;
    FavoritesStore store_;
    std::function<View *(const ROM &)> launch_;
    std::function<void()> back_;
    int selected_ = 0, width_ = 0;
    std::string status_;
    int hit(int x, int y) const;
};
} // namespace lilsmoke
