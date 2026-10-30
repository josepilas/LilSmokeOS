#include "FavoritesView.h"
#include "CollectionText.h"
#include "Log.h"
#include <algorithm>

namespace lilsmoke {
FavoritesView::FavoritesView(std::function<View *(const ROM &)> launch, std::function<View *(const ROM &)> options,
                             FavoritesStore store)
    : store_(std::move(store)), launch_(std::move(launch)), options_(std::move(options)) {}
void FavoritesView::onEnter() {
    status_ = store_.load() ? "" : store_.error(); missing_.clear();
    for (const auto &rom : store_.entries()) missing_.push_back(!FavoritesStore::present(rom));
    selected_ = std::clamp(selected_, 0, std::max(0, static_cast<int>(missing_.size()) - 1));
    LOG_INFO("Favorites: loaded %zu entries", store_.entries().size());
}
View *FavoritesView::handleInput(InputButton button, InputEvent event) {
    if (event != InputEvent::Pressed && event != InputEvent::Repeat) return nullptr;
    const int count = static_cast<int>(store_.entries().size());
    if (button == InputButton::Up) { selected_ = std::max(0, selected_ - 1); status_.clear(); }
    if (button == InputButton::Down) { selected_ = std::min(std::max(0, count - 1), selected_ + 1); status_.clear(); }
    if (!count || event != InputEvent::Pressed) return nullptr;
    const ROM &rom = store_.entries()[selected_];
    if (button == InputButton::X || button == InputButton::Home || button == InputButton::Select)
        return options_ ? options_(rom) : nullptr;
    if (button == InputButton::A || button == InputButton::Start) {
        missing_[selected_] = !FavoritesStore::present(rom);
        if (missing_[selected_]) { status_ = "Game file is missing"; return nullptr; }
        return launch_ ? launch_(rom) : nullptr;
    }
    return nullptr;
}
int FavoritesView::hit(int x, int y) const {
    if (x < 12 || x >= width_ - 12 || y < 44 || y >= bottom_) return -1;
    const int index = offset_ + (y - 44) / 48;
    return index < static_cast<int>(store_.entries().size()) ? index : -1;
}
CursorKind FavoritesView::cursorAt(int x, int y) const {
    const int index = hit(x, y);
    return index < 0 ? CursorKind::Normal : missing_[index] ? CursorKind::NotAllowed : CursorKind::Hand;
}
View *FavoritesView::handlePointer(const PointerEvent &event) {
    if (event.type == PointerEventType::Moved || event.type == PointerEventType::Pressed) {
        const int index = hit(event.x, event.y);
        if (index >= 0) selected_ = index;
        if (event.type == PointerEventType::Pressed && event.button == PointerButton::Primary && index < 0) return nullptr;
    }
    return View::handlePointer(event);
}
void FavoritesView::render(Renderer &r, Font &font, const Theme &) {
    width_ = r.width(); r.clear(0x2B2D31FF); r.fillRect(0, 0, width_, 36, 0x1E2A47FF);
    collectionText(r, font, "Favorites", 12, 8, width_ - 110, 0xD9C9A3FF);
    collectionText(r, font, std::to_string(store_.entries().size()) + "/128", width_ - 88, 8, 76, 0xD9C9A399, 14);
    const int visible = std::max(1, (r.height() - 76) / 48);
    if (selected_ < offset_) offset_ = selected_;
    if (selected_ >= offset_ + visible) offset_ = selected_ - visible + 1;
    const int end = std::min(static_cast<int>(store_.entries().size()), offset_ + visible);
    bottom_ = 44 + (end - offset_) * 48;
    if (store_.entries().empty()) collectionText(r, font, status_.empty() ? "No favorites" : status_, 20, 64, width_ - 40, 0xD9C9A3FF);
    for (int i = offset_, y = 44; i < end; ++i, y += 48) {
        const auto &rom = store_.entries()[i];
        if (i == selected_) r.fillRect(12, y, width_ - 24, 46, 0x1F3D2BFF);
        const auto color = missing_[i] ? 0xFF9999FFu : 0xD9C9A3FFu;
        collectionText(r, font, rom.name, 20, y + 3, width_ - 40, color);
        collectionText(r, font, missing_[i] ? "Missing | " + rom.system : rom.system, 20, y + 25, width_ - 40, color, 13);
    }
    r.fillRect(0, r.height() - 28, width_, 28, 0x1E2A47FF);
    collectionText(r, font, status_.empty() ? "X: Game Options" : status_, 12, r.height() - 22, width_ - 24, 0xD9C9A3FF, 13);
}
} // namespace lilsmoke
