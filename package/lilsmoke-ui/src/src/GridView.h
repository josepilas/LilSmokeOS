// LilSmokeUI — GridView.h
//
// Grid view for game selection. Shows cover art thumbnails in a grid.
// Falls back to text labels when cover art is missing.

#pragma once

#include "View.h"
#include "ROMScanner.h"
#include "Theme.h"

#include <vector>
#include <functional>

namespace lilsmoke {

struct GridPointerHit {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int index = -1;
};

class GridView : public View {
public:
    GridView(std::vector<const ROM *> roms,
             std::function<View *(const ROM *)> onSelect,
             std::string title,
             Theme theme = Theme());

    void onEnter() override;
    void onLeave() override;
    View *handleInput(InputButton btn, InputEvent evt) override;
    View *handlePointer(const PointerEvent &event) override;
    CursorKind cursorAt(int x, int y) const override;
    void render(Renderer &r, Font &font, const Theme &theme) override;
    void setSelectedIndex(int index);
    void setOnOptions(std::function<View *(const ROM &)> options) { onOptions_ = std::move(options); }
    bool acceptsText() const override { return editing_; }
    void handleText(const std::string &text) override;
    std::string textTitle() const override { return "Search games"; }
    std::string textValue() const override { return draft_; }
    bool allowAutoBackPop() const override { return !editing_ && query_.empty(); }
    // Y edits the filter; B cancels editing or clears an accepted filter.
    // R2 uses canonical held state, not kernel/SDL key-repeat events.
    void updateFastScroll(uint32_t now, bool held);

private:
    void applySearch();
    std::vector<const ROM *> allRoms_;
    std::vector<const ROM *> roms_;
    std::function<View *(const ROM *)> onSelect_;
    std::function<View *(const ROM &)> onOptions_;
    std::string title_;
    Theme theme_;
    int selected_ = 0;
    int scrollOffset_ = 0;
    int fullSelected_ = 0, fullScroll_ = 0;
    std::string query_, draft_;
    bool editing_ = false, scrolling_ = false, waitForRelease_ = false;
    uint32_t lastScroll_ = 0;
    uint32_t enteredAt_ = 0, selectedAt_ = 0;

    // Grid layout (4:3 friendly: 4 columns x 3 rows = 12 visible)
    int cols_ = 4;
    int rows_ = 3;
    int tileW_ = 140;
    int tileH_ = 140;
    int marginX_ = 16;
    int marginY_ = 50;
    std::vector<GridPointerHit> pointerHits_;
};

}  // namespace lilsmoke
