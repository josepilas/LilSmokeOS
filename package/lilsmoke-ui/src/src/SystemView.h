#pragma once

#include "Theme.h"
#include "ThemeMotion.h"
#include "ThemeRender.h"
#include "View.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace lilsmoke {

struct SystemEntry {
    std::string id;
    std::string label;
    int gameCount = 0;
};

struct SystemPointerHit {
    themeui::PaintedRegion region;
    int index = -1;
};

class SystemView final : public View {
public:
    SystemView(std::vector<SystemEntry> entries,
               std::function<View *(const SystemEntry &)> onSelect,
               const std::string *themeSetDir);

    View *handleInput(InputButton button, InputEvent event) override;
    void onEnter() override;
    View *handlePointer(const PointerEvent &event) override;
    CursorKind cursorAt(int x, int y) const override;
    void render(Renderer &renderer, Font &font, const Theme &theme) override;
    ViewActivity frameActivity(uint32_t now, const Theme &theme) const override;

    int selectedIndex() const { return selected_; }
    void setSelectedIndex(int index);

private:
    std::vector<SystemEntry> entries_;
    std::function<View *(const SystemEntry &)> onSelect_;
    const std::string *themeSetDir_ = nullptr;
    std::string cachedThemeSetDir_;
    std::unordered_map<std::string, Theme> themeCache_;
    int selected_ = 0;
    int previousSelected_ = 0;
    uint32_t enteredAt_ = 0, selectedAt_ = 0;
    CarouselMotion carouselMotion_;
    bool firstFrameComplete_ = false;
    std::vector<SystemPointerHit> pointerHits_;
    const ThemeView *sceneryView_ = nullptr;
    std::array<int, 3> sceneryIndices_{{-1, -1, -1}};
    std::vector<const ThemeElement *> stationaryScenery_;

    struct CarouselLayout {
        const ThemeElement *element = nullptr;
        int width = 0, height = 0, x = 0, y = 0, w = 0, h = 0;
        int count = 3;
        float logoW = 0.25f, logoH = 0.155f, scale = 1.2f;
        float centerX = 0, centerY = 0, spacingX = 0, spacingY = 0;
        float originX = 0.5f, originY = 0.5f;
        float rotation = 7.5f, rotationX = -5, rotationY = 0.5f;
        bool valid = false, vertical = false, wheel = false;
        uint32_t color = 0xFFFFFFD8;
    } carouselLayout_;

    const CarouselLayout &carouselLayout(const ThemeElement *element, int width, int height);

    Theme *themeFor(const SystemEntry &entry);
    std::string resolveThemeAlias(const std::string &system) const;
};

}  // namespace lilsmoke
