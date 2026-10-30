#pragma once
#include "Font.h"
#include "Renderer.h"

namespace lilsmoke {
inline void collectionText(Renderer &r, Font &font, std::string text, int x, int y,
                           int width, uint32_t color, int size = 16) {
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
} // namespace lilsmoke
