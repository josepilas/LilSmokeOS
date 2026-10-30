// SPDX-License-Identifier: MPL-2.0
#include "Terminal.h"
#include <algorithm>
#include <cstring>
#include <new>

namespace lilsmoke {

bool ByteQueue::append(const char *data, size_t length) {
    if (length > Capacity - size_) return false;
    const size_t tail = (head_ + size_) % Capacity;
    const size_t first = std::min(length, Capacity - tail);
    std::memcpy(bytes_.data() + tail, data, first);
    std::memcpy(bytes_.data(), data + first, length - first);
    size_ += length;
    return true;
}

size_t ByteQueue::contiguous() const { return std::min(size_, Capacity - head_); }

void ByteQueue::consume(size_t length) {
    length = std::min(length, size_);
    head_ = (head_ + length) % Capacity;
    size_ -= length;
}

Terminal::Terminal(int rows, int cols)
    : rows_(std::clamp(rows, 1, MaxRows)), cols_(std::clamp(cols, 1, MaxCols)),
      history_(HistoryLines * MaxCols) {
    vt_ = vterm_new(rows_, cols_);
    if (!vt_) throw std::bad_alloc();
    vterm_set_utf8(vt_, 1);
    screen_ = vterm_obtain_screen(vt_);
    if (!screen_) { vterm_free(vt_); throw std::bad_alloc(); }
    static const VTermScreenCallbacks callbacks = [] {
        VTermScreenCallbacks cb{};
        cb.damage = damage;
        cb.movecursor = moveCursor;
        cb.settermprop = property;
        cb.bell = bell;
        cb.sb_pushline = pushLine;
        cb.sb_popline = popLine;
        cb.sb_clear = clearHistory;
        return cb;
    }();
    vterm_screen_set_callbacks(screen_, &callbacks, this);
    vterm_output_set_callback(vt_, outputBytes, this);
    vterm_screen_enable_altscreen(screen_, 1);
    vterm_screen_set_damage_merge(screen_, VTERM_DAMAGE_SCROLL);
    VTermColor fg{}, bg{};
    vterm_color_rgb(&fg, 226, 232, 226);
    vterm_color_rgb(&bg, 18, 21, 20);
    vterm_state_set_default_colors(vterm_obtain_state(vt_), &fg, &bg);
    vterm_screen_reset(screen_, 1);
}

Terminal::~Terminal() { vterm_free(vt_); }

void Terminal::feed(const char *data, size_t length) {
    vterm_input_write(vt_, data, length);
    vterm_screen_flush_damage(screen_);
}

void Terminal::resize(int rows, int cols) {
    rows_ = std::clamp(rows, 1, MaxRows);
    cols_ = std::clamp(cols, 1, MaxCols);
    vterm_set_size(vt_, rows_, cols_);
    vterm_screen_flush_damage(screen_);
    dirty_ = true;
}

void Terminal::key(VTermKey key, VTermModifier modifiers) {
    live();
    vterm_keyboard_key(vt_, key, modifiers);
}

void Terminal::character(uint32_t codepoint, VTermModifier modifiers) {
    live();
    // xterm's legacy control bytes work with BusyBox/readline, which do not
    // negotiate CSI-u. libvterm otherwise disambiguates Ctrl-I/M/[ as CSI-u.
    if ((modifiers & VTERM_MOD_CTRL) && codepoint < 128) {
        int control = -1;
        if ((codepoint >= '@' && codepoint <= '_') || (codepoint >= 'a' && codepoint <= 'z'))
            control = codepoint & 31;
        else if (codepoint == ' ' || codepoint == '2') control = 0;
        else if (codepoint >= '3' && codepoint <= '7') control = codepoint - '3' + 27;
        else if (codepoint == '8' || codepoint == '?') control = 127;
        if (control >= 0) {
            const char bytes[] = {'\033', static_cast<char>(control)};
            const bool alt = modifiers & VTERM_MOD_ALT;
            if (!output_.append(bytes + (alt ? 0 : 1), alt ? 2 : 1)) overflow_ = true;
            return;
        }
    }
    vterm_keyboard_unichar(vt_, codepoint, modifiers);
}

void Terminal::scroll(int lines) {
    if (!alternate_) scrollOffset_ = std::clamp(scrollOffset_ + lines, 0, historySize_);
    dirty_ = true;
}

void Terminal::live() { scrollOffset_ = 0; dirty_ = true; }

bool Terminal::cell(int row, int col, VTermScreenCell &result) const {
    if (row < 0 || row >= rows_ || col < 0 || col >= cols_) return false;
    if (row < scrollOffset_) {
        const int line = (historyHead_ + historySize_ - scrollOffset_ + row) % HistoryLines;
        if (col < historyWidths_[line]) result = history_[line * MaxCols + col];
        else {
            result = {};
            result.width = 1;
            vterm_state_get_default_colors(vterm_obtain_state(vt_), &result.fg, &result.bg);
        }
        return true;
    }
    return vterm_screen_get_cell(screen_, {row - scrollOffset_, col}, &result) != 0;
}

VTermColor Terminal::rgb(VTermColor color) const {
    vterm_screen_convert_color_to_rgb(screen_, &color);
    return color;
}

bool Terminal::takeOverflow() { bool value = overflow_; overflow_ = false; return value; }

int Terminal::damage(VTermRect, void *user) {
    static_cast<Terminal *>(user)->dirty_ = true;
    return 1;
}

int Terminal::moveCursor(VTermPos pos, VTermPos, int visible, void *user) {
    auto &self = *static_cast<Terminal *>(user);
    self.cursor_ = pos;
    self.cursorVisible_ = visible != 0;
    self.dirty_ = true;
    return 1;
}

int Terminal::property(VTermProp prop, VTermValue *value, void *user) {
    auto &self = *static_cast<Terminal *>(user);
    if (prop == VTERM_PROP_CURSORVISIBLE) self.cursorVisible_ = value->boolean;
    if (prop == VTERM_PROP_ALTSCREEN) {
        self.alternate_ = value->boolean;
        self.scrollOffset_ = 0;
    }
    self.dirty_ = true;
    return 1;
}

int Terminal::bell(void *user) { return damage({}, user); }

int Terminal::pushLine(int cols, const VTermScreenCell *cells, void *user) {
    auto &self = *static_cast<Terminal *>(user);
    if (self.alternate_) return 1;
    int line = (self.historyHead_ + self.historySize_) % HistoryLines;
    if (self.historySize_ == HistoryLines) self.historyHead_ = (self.historyHead_ + 1) % HistoryLines;
    else ++self.historySize_;
    self.historyWidths_[line] = std::min(cols, MaxCols);
    std::copy_n(cells, self.historyWidths_[line], self.history_.data() + line * MaxCols);
    if (self.scrollOffset_ > 0) self.scrollOffset_ = std::min(self.scrollOffset_ + 1, self.historySize_);
    self.dirty_ = true;
    return 1;
}

void Terminal::outputBytes(const char *data, size_t length, void *user) {
    auto &self = *static_cast<Terminal *>(user);
    if (!self.output_.append(data, length)) self.overflow_ = true;
}

int Terminal::clearHistory(void *user) {
    auto &self = *static_cast<Terminal *>(user);
    self.historyHead_ = self.historySize_ = self.scrollOffset_ = 0;
    self.dirty_ = true;
    return 1;
}

} // namespace lilsmoke
