#include "RecentGamesView.h"
#include "Log.h"

#include <algorithm>
#include <sys/stat.h>

namespace lilsmoke {
namespace {
bool present(const std::string &path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
void cell(Renderer &r, Font &font, std::string text, int x, int y, int width,
          uint32_t color, int size = 16) {
    int w = 0, h = 0;
    font.textSizeStyled(text, "", size, w, h);
    if (w > width) {
        do {
            size_t last = text.size() - 1;
            while (last && (static_cast<unsigned char>(text[last]) & 0xC0) == 0x80) --last;
            text.resize(last);
            font.textSizeStyled(text + "...", "", size, w, h);
        } while (!text.empty() && w > width);
        text += "...";
    }
    font.renderStyled(r, text, x, y, width, 20, color, "", size);
}
}

RecentGamesView::RecentGamesView(std::function<View *(const ROM &)> launch, GameHistory history)
    : history_(std::move(history)), launch_(std::move(launch)) {}

void RecentGamesView::onEnter() {
    error_ = history_.load() ? "" : "Recent games unavailable";
    missing_.clear();
    for (const auto &game : history_.entries()) missing_.push_back(!present(game.path));
    selected_ = std::clamp(selected_, 0, std::max(0, static_cast<int>(missing_.size()) - 1));
    LOG_INFO("RecentGames: loaded %zu entries", history_.entries().size());
}

View *RecentGamesView::handleInput(InputButton button, InputEvent event) {
    if (event != InputEvent::Pressed && event != InputEvent::Repeat) return nullptr;
    const int count = static_cast<int>(history_.entries().size());
    if (count && event == InputEvent::Pressed &&
        (button == InputButton::X || button == InputButton::Home || button == InputButton::Select)) {
        const auto &game = history_.entries()[selected_];
        ROM rom; rom.system = game.system; rom.path = game.path; rom.name = game.name;
        return options_ ? options_(rom) : nullptr;
    }
    if (button == InputButton::Up) selected_ = std::max(0, selected_ - 1);
    if (button == InputButton::Down) selected_ = std::min(std::max(0, count - 1), selected_ + 1);
    if ((button == InputButton::A || button == InputButton::Start) && event == InputEvent::Pressed && count) {
        const auto &game = history_.entries()[selected_];
        missing_[selected_] = !present(game.path);
        if (missing_[selected_]) { error_ = "Game file is missing"; return nullptr; }
        error_.clear();
        ROM rom;
        rom.system = game.system;
        rom.path = game.path;
        rom.name = game.name;
        return launch_ ? launch_(rom) : nullptr;
    }
    return nullptr;
}

int RecentGamesView::hit(int x, int y) const {
    if (x < 12 || x >= width_ - 12 || y < 44 || y >= hitBottom_) return -1;
    const int index = offset_ + (y - 44) / 52;
    return index < static_cast<int>(history_.entries().size()) ? index : -1;
}
CursorKind RecentGamesView::cursorAt(int x, int y) const {
    const int index = hit(x, y);
    if (index < 0) return CursorKind::Normal;
    return missing_[index] ? CursorKind::NotAllowed : CursorKind::Hand;
}
View *RecentGamesView::handlePointer(const PointerEvent &event) {
    if (event.type == PointerEventType::Moved || event.type == PointerEventType::Pressed) {
        const int index = hit(event.x, event.y);
        if (index >= 0) selected_ = index;
        if (event.type == PointerEventType::Pressed && event.button == PointerButton::Primary && index < 0)
            return nullptr;
    }
    return View::handlePointer(event);
}

void RecentGamesView::render(Renderer &r, Font &font, const Theme &) {
    width_ = r.width();
    r.clear(0x2B2D31FF);
    r.fillRect(0, 0, r.width(), 36, 0x1E2A47FF);
    cell(r, font, "Recent Games", 12, 8, r.width() - 100, 0xD9C9A3FF);
    const auto &entries = history_.entries();
    cell(r, font, std::to_string(entries.size()) + "/32", r.width() - 72, 8, 60, 0xD9C9A399, 14);
    visible_ = std::max(1, (r.height() - 76) / 52);
    if (selected_ < offset_) offset_ = selected_;
    if (selected_ >= offset_ + visible_) offset_ = selected_ - visible_ + 1;
    const int end = std::min(static_cast<int>(entries.size()), offset_ + visible_);
    hitBottom_ = 44 + (end - offset_) * 52;
    if (entries.empty()) cell(r, font, error_.empty() ? "No recent games" : error_, 20, 64, r.width() - 40, 0xD9C9A3FF);
    for (int i = offset_, y = 44; i < end; ++i, y += 52) {
        const auto &game = entries[i];
        if (i == selected_) r.fillRect(12, y, r.width() - 24, 50, 0x1F3D2BFF);
        const auto color = missing_[i] ? 0xFF9999FFu : 0xD9C9A3FFu;
        cell(r, font, game.name, 20, y + 3, r.width() - 40, color);
        const std::string details = (missing_[i] ? "Missing | " : game.system + " | ") +
            "Last " + sessionTime(game.lastMs) + " | Total " + sessionTime(game.totalMs);
        cell(r, font, details, 20, y + 27, r.width() - 40, color, 13);
    }
    r.fillRect(0, r.height() - 28, r.width(), 28, 0x1E2A47FF);
    cell(r, font, error_.empty() ? (options_ ? "X: Game Options" : "") : error_, 12, r.height() - 22, r.width() - 24, 0xFF9999FF, 13);
}
} // namespace lilsmoke
