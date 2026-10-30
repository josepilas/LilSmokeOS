#pragma once
#include "FavoritesStore.h"
#include "View.h"
#include <functional>

namespace lilsmoke {
class FavoritesView final : public View {
public:
    FavoritesView(std::function<View *(const ROM &)> launch, std::function<View *(const ROM &)> options,
                  FavoritesStore store = FavoritesStore());
    void onEnter() override;
    View *handleInput(InputButton button, InputEvent event) override;
    View *handlePointer(const PointerEvent &event) override;
    CursorKind cursorAt(int x, int y) const override;
    void render(Renderer &r, Font &font, const Theme &) override;
    int selectedIndex() const { return selected_; }
private:
    FavoritesStore store_;
    std::function<View *(const ROM &)> launch_, options_;
    std::vector<bool> missing_;
    int selected_ = 0, offset_ = 0, width_ = 0, bottom_ = 44;
    std::string status_;
    int hit(int x, int y) const;
};
} // namespace lilsmoke
