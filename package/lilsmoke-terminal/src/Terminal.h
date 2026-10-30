// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <vterm.h>
#include <array>
#include <cstddef>
#include <vector>

namespace lilsmoke {

class ByteQueue {
public:
    static constexpr size_t Capacity = 16384;
    bool append(const char *data, size_t length);
    const char *data() const { return bytes_.data() + head_; }
    size_t contiguous() const;
    size_t size() const { return size_; }
    void consume(size_t length);
private:
    std::array<char, Capacity> bytes_{};
    size_t head_ = 0, size_ = 0;
};

class Terminal {
public:
    static constexpr int MaxRows = 100, MaxCols = 240, HistoryLines = 128;
    Terminal(int rows, int cols);
    ~Terminal();
    Terminal(const Terminal &) = delete;
    Terminal &operator=(const Terminal &) = delete;
    void feed(const char *data, size_t length);
    void resize(int rows, int cols);
    void key(VTermKey key, VTermModifier modifiers);
    void character(uint32_t codepoint, VTermModifier modifiers);
    void scroll(int lines);
    void live();
    bool cell(int row, int col, VTermScreenCell &result) const;
    VTermColor rgb(VTermColor color) const;
    int rows() const { return rows_; }
    int cols() const { return cols_; }
    int historySize() const { return historySize_; }
    int scrollOffset() const { return scrollOffset_; }
    bool alternate() const { return alternate_; }
    bool dirty() const { return dirty_; }
    void invalidate() { dirty_ = true; }
    void painted() { dirty_ = false; }
    bool cursorVisible() const { return cursorVisible_ && scrollOffset_ == 0; }
    VTermPos cursor() const { return cursor_; }
    bool takeOverflow();
    ByteQueue &output() { return output_; }
private:
    static int damage(VTermRect, void *user);
    static int moveCursor(VTermPos, VTermPos, int visible, void *user);
    static int property(VTermProp, VTermValue *, void *user);
    static int bell(void *user);
    static int pushLine(int cols, const VTermScreenCell *, void *user);
    static int popLine(int, VTermScreenCell *, void *) { return 0; }
    static int clearHistory(void *user);
    static void outputBytes(const char *, size_t, void *user);
    VTerm *vt_ = nullptr;
    VTermScreen *screen_ = nullptr;
    int rows_, cols_;
    std::vector<VTermScreenCell> history_;
    std::array<int, HistoryLines> historyWidths_{};
    int historyHead_ = 0, historySize_ = 0, scrollOffset_ = 0;
    VTermPos cursor_{};
    bool dirty_ = true, cursorVisible_ = true, alternate_ = false, overflow_ = false;
    ByteQueue output_;
};

} // namespace lilsmoke
