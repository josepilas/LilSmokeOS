#include "GameOptionsView.h"
#include "CollectionText.h"
#include "Log.h"
#include <algorithm>

namespace lilsmoke {
GameOptionsView::GameOptionsView(ROM rom, std::function<View *(const ROM &)> launch,
                               std::function<void()> back, FavoritesStore store)
    : rom_(std::move(rom)), store_(std::move(store)), launch_(std::move(launch)), back_(std::move(back)) {}
void GameOptionsView::onEnter() {
    status_ = store_.load() ? "" : store_.error();
    LOG_INFO("GameOptions: ready for %s favorite=%d", rom_.path.c_str(), store_.contains(rom_));
}
View *GameOptionsView::handleInput(InputButton button, InputEvent event) {
    if (event != InputEvent::Pressed && event != InputEvent::Repeat) return nullptr;
    if (button == InputButton::Up) selected_ = std::max(0, selected_ - 1);
    if (button == InputButton::Down) selected_ = std::min(2, selected_ + 1);
    if (event != InputEvent::Pressed) return nullptr;
    if (button == InputButton::B) { if (back_) back_(); return nullptr; }
    if (button == InputButton::A || button == InputButton::Start) {
        if (selected_ == 0) {
            if (!FavoritesStore::present(rom_)) status_ = "Game file is missing or invalid";
            else return launch_ ? launch_(rom_) : nullptr;
        } else if (selected_ == 1) {
            const bool add = !store_.contains(rom_);
            status_ = store_.setFavorite(rom_, add) ? (add ? "Added to Favorites" : "Removed from Favorites") : store_.error();
            LOG_INFO("GameOptions: %s", status_.c_str());
        } else if (back_) back_();
    }
    return nullptr;
}
int GameOptionsView::hit(int x, int y) const {
    return x >= 12 && x < width_ - 12 && y >= 82 && y < 190 ? (y - 82) / 36 : -1;
}
CursorKind GameOptionsView::cursorAt(int x, int y) const { return hit(x, y) < 0 ? CursorKind::Normal : CursorKind::Hand; }
View *GameOptionsView::handlePointer(const PointerEvent &event) {
    if (event.type == PointerEventType::Moved || event.type == PointerEventType::Pressed) {
        const int index = hit(event.x, event.y);
        if (index >= 0) selected_ = index;
        if (event.type == PointerEventType::Pressed && event.button == PointerButton::Primary && index < 0) return nullptr;
    }
    return View::handlePointer(event);
}
void GameOptionsView::render(Renderer &r, Font &font, const Theme &) {
    width_ = r.width();
    r.clear(0x2B2D31FF); r.fillRect(0, 0, width_, 36, 0x1E2A47FF);
    collectionText(r, font, "Game Options", 12, 8, width_ - 24, 0xD9C9A3FF);
    collectionText(r, font, rom_.name, 20, 48, width_ - 40, 0xD9C9A3FF);
    const std::string labels[] = {"Play", store_.contains(rom_) ? "Remove from Favorites" : "Add to Favorites", "Cancel"};
    for (int i = 0; i < 3; ++i) {
        if (selected_ == i) r.fillRect(12, 82 + i * 36, width_ - 24, 34, 0x1F3D2BFF);
        collectionText(r, font, labels[i], 20, 90 + i * 36, width_ - 40, 0xD9C9A3FF);
    }
    collectionText(r, font, status_, 12, r.height() - 26, width_ - 24, 0xFFCC99FF, 13);
}
} // namespace lilsmoke
