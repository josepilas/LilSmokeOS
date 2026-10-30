// LilSmokeUI - ListView.cpp

#include "ListView.h"
#include "Log.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <sstream>
#include <utility>

namespace lilsmoke {

namespace {

struct Rect {
    int x, y, w, h;
};

static Rect elementRect(const ThemeElement &elem,
                        Renderer &r,
                        float defX,
                        float defY,
                        float defW,
                        float defH) {
    float x, y, w, h, ox, oy;
    elem.getPos(x, y, defX, defY);
    elem.getSize(w, h, defW, defH);
    elem.getOrigin(ox, oy, 0.0f, 0.0f);
    int pxW = std::max(1, (int)(w * r.width()));
    int pxH = std::max(1, (int)(h * r.height()));
    return {
        (int)(x * r.width()) - (int)(ox * pxW),
        (int)(y * r.height()) - (int)(oy * pxH),
        pxW,
        pxH,
    };
}

static Rect imageElementRect(const ThemeElement &elem,
                             Renderer &r,
                             SDL_Texture *texture,
                             float defX,
                             float defY,
                             float defW,
                             float defH) {
    float x, y, w, h, ox, oy;
    elem.getPos(x, y, defX, defY);
    elem.getOrigin(ox, oy, 0.0f, 0.0f);
    elem.getSize(w, h, defW, defH);

    int textureW = 1;
    int textureH = 1;
    SDL_QueryTexture(texture, nullptr, nullptr, &textureW, &textureH);
    float normalizedAspect =
        ((float)textureW / (float)std::max(1, textureH)) *
        ((float)r.height() / (float)r.width());

    std::string size = elem.get("size");
    if (!size.empty()) {
        if (w <= 0.0f && h > 0.0f) w = h * normalizedAspect;
        if (h <= 0.0f && w > 0.0f) h = w / std::max(0.001f, normalizedAspect);
    }

    std::string maxSize = elem.get("maxsize");
    if (!maxSize.empty()) {
        float maxW = 0.0f, maxH = 0.0f;
        std::istringstream ss(maxSize);
        ss >> maxW;
        if (!(ss >> maxH)) maxH = maxW;
        if (size.empty()) {
            w = maxW;
            h = w / std::max(0.001f, normalizedAspect);
            if (h > maxH) {
                h = maxH;
                w = h * normalizedAspect;
            }
        } else if (w > 0.0f && h > 0.0f) {
            float scale = std::min(maxW > 0.0f ? maxW / w : 1.0f,
                                   maxH > 0.0f ? maxH / h : 1.0f);
            if (scale < 1.0f) {
                w *= scale;
                h *= scale;
            }
        }
    }

    int pxW = std::max(1, (int)(w * r.width()));
    int pxH = std::max(1, (int)(h * r.height()));
    return {(int)(x * r.width()) - (int)(ox * pxW),
            (int)(y * r.height()) - (int)(oy * pxH), pxW, pxH};
}

static void drawImageElement(Renderer &r,
                             const ThemeElement &elem,
                             float defX,
                             float defY,
                             float defW,
                             float defH) {
    std::string path = elem.get("path");
    if (path.empty()) {
        if (elem.get("size").empty() && elem.get("maxsize").empty()) return;
        Rect rc = elementRect(elem, r, defX, defY, defW, defH);
        uint32_t color = elem.getColor("color", 0);
        if (color != 0) r.fillRect(rc.x, rc.y, rc.w, rc.h, color);
        return;
    }

    SDL_Texture *tex = r.loadImage(path);
    if (!tex) return;
    Rect rc = imageElementRect(elem, r, tex, defX, defY, defW, defH);
    if (!elem.get("color").empty()) {
        uint32_t color = elem.getColor("color", 0xFFFFFFFF);
        SDL_SetTextureColorMod(tex, (color >> 24) & 0xFF,
                              (color >> 16) & 0xFF, (color >> 8) & 0xFF);
        SDL_SetTextureAlphaMod(tex, color & 0xFF);
    }
    r.drawTexture(tex, rc.x, rc.y, rc.w, rc.h);
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, 255);
}

static const ThemeView *listViewTheme(const Theme &theme) {
    if (const ThemeView *v = theme.findView("system")) return v;
    if (const ThemeView *v = theme.findView("basic")) return v;
    if (const ThemeView *v = theme.findView("detailed")) return v;
    return theme.findView("grid");
}

static void drawThemeBackdrop(Renderer &r, const ThemeView *view) {
    if (!view) return;

    if (const ThemeElement *bg = view->find("image", "background"))
        drawImageElement(r, *bg, 0.0f, 0.0f, 1.0f, 1.0f);

    for (const auto &elem : view->elements) {
        if (elem.type != "image") continue;
        if (elem.name == "background" || elem.name == "logo") continue;
        if (!elem.getBool("extra", false)) continue;
        drawImageElement(r, elem, 0.0f, 0.0f, 1.0f, 1.0f);
    }
}

static bool drawLogo(Renderer &r,
                     Font &font,
                     const ThemeView *view,
                     const std::string &title) {
    if (!view) return false;
    if (const ThemeElement *logo = view->find("image", "logo")) {
        if (!logo->get("path").empty()) {
            ThemeElement adjusted = *logo;
            if (adjusted.get("size").empty() &&
                adjusted.get("maxsize").empty()) {
                std::string logoSize;
                if (const ThemeElement *carousel =
                        view->find("carousel", "systemcarousel")) {
                    logoSize = carousel->get("logosize");
                }
                adjusted.props["maxsize"] =
                    logoSize.empty() ? "0.45 0.72" : logoSize;
                if (adjusted.get("pos").empty())
                    adjusted.props["pos"] = "0.72 0.50";
                if (adjusted.get("origin").empty())
                    adjusted.props["origin"] = "0.5 0.5";
            }
            drawImageElement(r, adjusted, 0.72f, 0.50f, 0.45f, 0.72f);
            return true;
        }
    }

    if (const ThemeElement *logoText = view->find("text", "logoText")) {
        float x, y, ox, oy;
        logoText->getPos(x, y, 0.02f, 0.02f);
        logoText->getOrigin(ox, oy, 0.0f, 0.0f);
        uint32_t color = logoText->getColor("color", 0xD9C9A3FF);
        int tw = 0, th = 0;
        font.textSize(title, tw, th);
        font.render(r, title,
                    (int)(x * r.width()) - (int)(ox * tw),
                    (int)(y * r.height()) - (int)(oy * th),
                    color);
        return true;
    }

    return false;
}

static void drawThemeTextExtras(Renderer &r,
                                Font &font,
                                const ThemeView *view) {
    if (!view) return;
    for (const auto &elem : view->elements) {
        if (elem.type != "text" || !elem.getBool("extra", false) ||
            !elem.getBool("visible", true)) {
            continue;
        }
        std::string text = elem.get("text");
        if (text.empty()) continue;

        float x, y, ox, oy;
        elem.getPos(x, y, 0.02f, 0.02f);
        elem.getOrigin(ox, oy, 0.0f, 0.0f);
        int tw = 0, th = 0;
        font.textSize(text, tw, th);
        font.render(r, text,
                    (int)(x * r.width()) - (int)(ox * tw),
                    (int)(y * r.height()) - (int)(oy * th),
                    elem.getColor("color", 0xD9C9A3FF));
    }
}

}  // namespace

ListView::ListView(std::vector<std::string> items,
                   std::function<View *(int, const std::string &)> onSelect,
                   std::string title, bool useTheme)
    : items_(std::move(items)),
      onSelect_(std::move(onSelect)),
      title_(std::move(title)), useTheme_(useTheme) {}

void ListView::onEnter() {
    selected_ = 0;
    scrollOffset_ = 0;
}

void ListView::setSelectedIndex(int i) {
    if (items_.empty()) return;
    if (i < 0) i = 0;
    if (i >= (int)items_.size()) i = items_.size() - 1;
    selected_ = i;

    if (selected_ < scrollOffset_) scrollOffset_ = selected_;
    if (selected_ >= scrollOffset_ + visibleCount_)
        scrollOffset_ = selected_ - visibleCount_ + 1;
}

View *ListView::handleInput(InputButton btn, InputEvent evt) {
    if (evt != InputEvent::Pressed && evt != InputEvent::Repeat) return nullptr;

    switch (btn) {
        case InputButton::Up:
            if (selected_ > 0) setSelectedIndex(selected_ - 1);
            return nullptr;
        case InputButton::Down:
            if (selected_ < (int)items_.size() - 1)
                setSelectedIndex(selected_ + 1);
            return nullptr;
        case InputButton::A:
        case InputButton::Start:
            if (evt != InputEvent::Pressed) return nullptr;
            if (onSelect_ && !items_.empty()) return onSelect_(selected_, items_[selected_]);
            return nullptr;
        case InputButton::B:
            return nullptr;
        default:
            return nullptr;
    }
}

View *ListView::handlePointer(const PointerEvent &event) {
    if (event.type == PointerEventType::Pressed && event.button == PointerButton::Primary &&
        cursorAt(event.x, event.y) != CursorKind::Hand) return nullptr;
    if ((event.type == PointerEventType::Moved ||
         event.type == PointerEventType::Pressed) &&
        event.x >= hitX_ && event.x < hitX_ + hitW_ &&
        event.y >= hitY_ && event.y < hitY_ + hitH_ && hitRowH_ > 0) {
        int index = scrollOffset_ + (event.y - hitY_) / hitRowH_;
        if (index >= 0 && index < static_cast<int>(items_.size()))
            setSelectedIndex(index);
    }
    return View::handlePointer(event);
}

CursorKind ListView::cursorAt(int x, int y) const {
    if (x >= hitX_ && x < hitX_ + hitW_ &&
        y >= hitY_ && y < hitY_ + hitH_) {
        int index = scrollOffset_ + (y - hitY_) / std::max(1, hitRowH_);
        return index < static_cast<int>(items_.size())
            ? CursorKind::Hand : CursorKind::NotAllowed;
    }
    return CursorKind::Normal;
}

void ListView::render(Renderer &r, Font &font, const Theme &theme) {
    const ThemeView *view = useTheme_ && theme.loaded() ? listViewTheme(theme) : nullptr;
    const ThemeElement *list = view ? view->find("textlist", "gamelist") : nullptr;
    if (!list && view) list = view->find("textlist", "systemlist");

    uint32_t clearColor =
        list ? list->getColor("backgroundcolor", 0x2B2D31FF) : 0x2B2D31FF;
    r.clear(clearColor);
    drawThemeBackdrop(r, view);

    if (!drawLogo(r, font, view, title_)) {
        r.fillRect(0, 0, r.width(), 36, 0x1E2A47FF);
        font.render(r, title_, 12, 8, 0xD9C9A3FF);
    }
    drawThemeTextExtras(r, font, view);

    float lx, ly, lw, lh;
    if (list) {
        list->getPos(lx, ly, 0.05f, 0.12f);
        list->getSize(lw, lh, 0.90f, 0.76f);
    } else {
        lx = 16.0f / (float)r.width();
        ly = 50.0f / (float)r.height();
        lw = (float)(r.width() - 32) / (float)r.width();
        lh = (float)(r.height() - 84) / (float)r.height();
    }

    int listX = (int)(lx * r.width());
    int listY = (int)(ly * r.height());
    int listW = std::max(1, (int)(lw * r.width()));
    int listH = std::max(1, (int)(lh * r.height()));
    int itemH = 24;
    if (list) {
        float fontSize = list->getFloat("fontsize", 0.045f);
        itemH = std::max(18, (int)(fontSize * r.height() * 1.35f));
    }
    visibleCount_ = std::max(1, listH / itemH);
    hitX_ = listX;
    hitY_ = listY;
    hitW_ = listW;
    hitH_ = listH;
    hitRowH_ = itemH;
    if (selected_ >= scrollOffset_ + visibleCount_)
        scrollOffset_ = selected_ - visibleCount_ + 1;

    uint32_t selectorColor =
        list ? list->getColor("selectorcolor", 0x1F3D2BFF) : 0x1F3D2BFF;
    uint32_t selectedColor =
        list ? list->getColor("selectedcolor", 0xD9C9A3FF) : 0xD9C9A3FF;
    uint32_t primaryColor =
        list ? list->getColor("primarycolor", 0xD9C9A3AA) : 0xD9C9A3AA;

    int end = std::min((int)items_.size(), scrollOffset_ + visibleCount_);
    int y = listY;
    for (int i = scrollOffset_; i < end; i++) {
        bool selected = (i == selected_);
        if (selected) r.fillRect(listX, y, listW, itemH - 2, selectorColor);

        std::string label = items_[i];
        int textW = 0, textH = 0;
        font.textSize(label, textW, textH);
        int textX = listX + 8;
        std::string alignment = list ? list->get("alignment", "left") : "left";
        if (alignment == "center")
            textX = listX + (listW - textW) / 2;
        else if (alignment == "right")
            textX = listX + listW - textW - 8;

        font.render(r, label, textX, y + (itemH - textH) / 2,
                    selected ? selectedColor : primaryColor);
        y += itemH;
    }

    int fy = r.height() - 20;
    r.fillRect(0, fy - 4, r.width(), 24, 0x1E2A47CC);
    font.render(r, "Up/Down: navigate   A: select   B: back", 12, fy, 0xD9C9A399);
}

}  // namespace lilsmoke
