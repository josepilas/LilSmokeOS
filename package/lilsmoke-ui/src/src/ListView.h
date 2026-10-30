// LilSmokeUI — ListView.h
//
// Shows a vertical list of strings. Used for:
//   - System selection (each row = a system name)
//   - Game selection within a system (each row = a game name)

#pragma once

#include "View.h"
#include "ROMScanner.h"

#include <vector>
#include <string>
#include <functional>

namespace lilsmoke {

class ListView : public View {
public:
    // `items`: the strings to display.
    // `onSelect`: called when user presses A/Start on an item. Returns the next view.
    ListView(std::vector<std::string> items,
             std::function<View *(int index, const std::string &)> onSelect,
             std::string title, bool useTheme = true);

    void onEnter() override;
    View *handleInput(InputButton btn, InputEvent evt) override;
    View *handlePointer(const PointerEvent &event) override;
    CursorKind cursorAt(int x, int y) const override;
    void render(Renderer &r, Font &font, const Theme &theme) override;

    int selectedIndex() const { return selected_; }
    void setSelectedIndex(int i);

private:
    std::vector<std::string> items_;
    std::function<View *(int, const std::string &)> onSelect_;
    std::string title_;
    bool useTheme_ = true;
    int selected_ = 0;
    int scrollOffset_ = 0;
    int visibleCount_ = 16;
    int hitX_ = 0;
    int hitY_ = 0;
    int hitW_ = 0;
    int hitH_ = 0;
    int hitRowH_ = 24;
};

}  // namespace lilsmoke
