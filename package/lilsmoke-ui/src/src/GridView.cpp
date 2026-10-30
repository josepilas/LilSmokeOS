#include "GridView.h"

#include "Font.h"
#include "Renderer.h"
#include "ThemeRender.h"
#include "ThemeMotion.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <utility>

namespace lilsmoke {

namespace {

enum class NodeKind {
    ExtraImage,
    ExtraText,
    GameList,
    Media,
    Marquee,
    MetadataText,
    Rating,
    LogoImage,
    LogoText,
    ImageGrid,
};

struct RenderNode {
    float z = 0.0f;
    size_t order = 0;
    NodeKind kind = NodeKind::ExtraImage;
    const ThemeElement *element = nullptr;
};

static bool isExtra(const ThemeElement &element) {
    std::string value = element.get("extra");
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value == "true" || value == "1" || value == "yes" ||
           value == "static";
}

static const ThemeView *chooseGameView(const Theme &theme) {
    const char *requested = std::getenv("LILSMOKE_THEME_GAME_VIEW");
    if (requested && *requested) {
        if (const ThemeView *view = theme.findView(requested)) return view;
    }

    // ArkOS and EmuELEC use the video view as a detailed list with a still
    // snapshot until playback begins. It is the richest compatible fallback
    // on this CPU-only frontend.
    if (const ThemeView *view = theme.findView("video")) return view;
    if (const ThemeView *view = theme.findView("detailed")) return view;
    if (const ThemeView *view = theme.findView("basic")) return view;
    return theme.findView("grid");
}

static bool parsePair(const std::string &value, float &x, float &y) {
    if (value.empty()) return false;
    std::istringstream input(value);
    input >> x;
    if (!(input >> y)) y = x;
    return true;
}

static std::string metadataValue(const ROM &rom,
                                 const std::string &name,
                                 bool label) {
    if (label) {
        if (name == "md_lbl_rating") return "Rating:";
        if (name == "md_lbl_releasedate") return "Released:";
        if (name == "md_lbl_developer") return "Developer:";
        if (name == "md_lbl_publisher") return "Publisher:";
        if (name == "md_lbl_genre") return "Genre:";
        if (name == "md_lbl_players") return "Players:";
        if (name == "md_lbl_lastplayed") return "Last played:";
        if (name == "md_lbl_playcount") return "Times played:";
        if (name == "md_lbl_gametime") return "Game time:";
    }
    if (name == "md_name") return rom.name;
    if (name == "md_description") return rom.description;
    if (name == "md_releasedate") return rom.releaseDate;
    if (name == "md_developer") return rom.developer;
    if (name == "md_publisher" || name == "md_publsher") return rom.publisher;
    if (name == "md_genre") return rom.genre;
    if (name == "md_players") return rom.players;
    if (name == "md_rating") return rom.rating;
    return "";
}

static int listFontPixels(const ThemeElement &list, Renderer &renderer) {
    return std::max(7, static_cast<int>(std::round(
                           list.getFloat("fontsize", 0.045f) * renderer.height())));
}

static void drawGameList(Renderer &renderer,
                         Font &font,
                         const ThemeElement &list,
                         const std::vector<const ROM *> &roms,
                         int selected,
                         int &scrollOffset,
                         std::vector<GridPointerHit> &pointerHits) {
    if (!themeui::visible(list) || roms.empty()) return;
    // BasicGameListView starts full width, then Detailed/VideoGameListView
    // moves the list to the right half before applying theme properties.
    // Themes such as Switch deliberately omit <pos> and rely on that native
    // default together with an origin greater than 1 to place the list left.
    themeui::Rect area = themeui::elementRect(list, renderer, 0.51f, 0.2f,
                                               0.49f, 0.8f);
    if (area.w <= 0 || area.h <= 0) return;

    const int fontPixels = listFontPixels(list, renderer);
    const float lineSpacing = list.getFloat("linespacing", 1.5f);
    const int rowHeight = std::max(12, static_cast<int>(
        std::round(fontPixels * std::max(1.0f, lineSpacing))));
    const int visibleRows = std::max(1, area.h / rowHeight);
    if (selected < scrollOffset) scrollOffset = selected;
    if (selected >= scrollOffset + visibleRows)
        scrollOffset = selected - visibleRows + 1;
    int end = std::min(static_cast<int>(roms.size()), scrollOffset + visibleRows);

    uint32_t selectorColor = list.getColor("selectorcolor", 0x00000000);
    uint32_t selectedColor = list.getColor("selectedcolor", 0xFFFFFFFF);
    uint32_t primaryColor = list.getColor("primarycolor", 0x777777FF);
    float selectorHeightValue = list.getFloat("selectorheight", 0.0f);
    int selectorHeight = selectorHeightValue > 0.0f
        ? std::max(1, static_cast<int>(selectorHeightValue * renderer.height()))
        : rowHeight;
    int selectorOffset = static_cast<int>(
        list.getFloat("selectoroffsety", 0.0f) * renderer.height());
    int margin = static_cast<int>(
        list.getFloat("horizontalmargin", 0.01f) * renderer.width());
    const std::string alignment = list.get("alignment", "left");
    const std::string fontPath = list.get("fontpath");
    const bool uppercase = list.getBool("forceuppercase", false);

    renderer.setClipRect(area.x, area.y, area.w, area.h);
    for (int index = scrollOffset; index < end; ++index) {
        int rowY = area.y + (index - scrollOffset) * rowHeight;
        pointerHits.push_back({area.x, rowY, area.w, rowHeight, index});
        bool isSelected = index == selected;
        if (isSelected && (selectorColor & 0xFF) != 0) {
            renderer.fillRect(area.x, rowY + selectorOffset, area.w,
                              selectorHeight, selectorColor);
        }

        std::string label = roms[index]->name;
        if (uppercase) label = themeui::upper(label);
        font.renderStyled(renderer, label, area.x + margin, rowY,
                          std::max(1, area.w - margin * 2), rowHeight,
                          isSelected ? selectedColor : primaryColor,
                          fontPath, fontPixels, alignment, "center", false,
                          1.0f);
    }
    renderer.clearClipRect();
}

static void drawRating(Renderer &renderer,
                       Font &font,
                       const ThemeElement &element,
                       const std::string &value) {
    if (!themeui::visible(element) || value.empty()) return;
    float rating = std::strtof(value.c_str(), nullptr);
    if (rating > 1.0f) rating /= 5.0f;
    rating = std::max(0.0f, std::min(1.0f, rating));

    float x = 0.0f, y = 0.0f, originX = 0.0f, originY = 0.0f;
    float width = 0.0f, height = 0.04f;
    element.getPos(x, y, x, y);
    element.getOrigin(originX, originY, originX, originY);
    element.getSize(width, height, width, height);
    if (height <= 0.0f) height = 0.04f;
    if (width <= 0.0f)
        width = height * 5.0f * renderer.height() / renderer.width();
    const float starWidth = width / 5.0f;
    const std::string filled = element.get("filledpath");
    const std::string unfilled = element.get("unfilledpath");

    if (!filled.empty() || !unfilled.empty()) {
        for (int star = 0; star < 5; ++star) {
            bool on = rating * 5.0f >= star + 0.5f;
            std::string path = on ? filled : unfilled;
            if (path.empty()) path = on ? unfilled : filled;
            ThemeElement image = element;
            image.type = "image";
            image.props.erase("size");
            image.props["maxsize"] = std::to_string(starWidth) + " " +
                                     std::to_string(height);
            image.props["origin"] = "0 0.5";
            float left = x - originX * width + star * starWidth;
            float top = y + (0.5f - originY) * height;
            image.props["pos"] = std::to_string(left) + " " +
                                  std::to_string(top);
            themeui::drawImage(renderer, image, path);
        }
    } else {
        ThemeElement text = element;
        text.type = "text";
        text.props["size"] = std::to_string(width) + " " +
                             std::to_string(height);
        themeui::drawText(renderer, font, text, "*****",
                          element.getColor("color", 0xFFFFFFFF));
    }
}

static void drawImageGrid(Renderer &renderer,
                          const ThemeElement &grid,
                          const std::vector<const ROM *> &roms,
                          int selected,
                          int &cols,
                          int &rows,
                          int &tileW,
                          int &tileH,
                          std::vector<GridPointerHit> &pointerHits) {
    themeui::Rect area = themeui::elementRect(grid, renderer, 0.02f, 0.14f,
                                               0.96f, 0.76f);
    float autoCols = 4.0f, autoRows = 2.0f;
    parsePair(grid.get("autolayout"), autoCols, autoRows);
    cols = std::max(1, static_cast<int>(std::round(autoCols)));
    rows = std::max(1, static_cast<int>(std::round(autoRows)));
    float marginX = 0.01f, marginY = 0.01f;
    parsePair(grid.get("margin"), marginX, marginY);
    int gapX = std::max(2, static_cast<int>(marginX * renderer.width()));
    int gapY = std::max(2, static_cast<int>(marginY * renderer.height()));
    tileW = std::max(24, (area.w - gapX * (cols - 1)) / cols);
    tileH = std::max(24, (area.h - gapY * (rows - 1)) / rows);
    int page = std::max(1, cols * rows);
    int first = selected / page * page;
    int visible = std::min(page, static_cast<int>(roms.size()) - first);

    renderer.setClipRect(area.x, area.y, area.w, area.h);
    for (int item = 0; item < visible; ++item) {
        int index = first + item;
        int x = area.x + (item % cols) * (tileW + gapX);
        int y = area.y + (item / cols) * (tileH + gapY);
        pointerHits.push_back({x, y, tileW, tileH, index});
        bool active = index == selected;
        if (active) renderer.drawRect(x, y, tileW, tileH, 0xFFFFFFFF);
        ThemeElement image;
        image.type = "image";
        image.props["pos"] = std::to_string(x / static_cast<float>(renderer.width())) +
                             " " + std::to_string(y / static_cast<float>(renderer.height()));
        image.props["origin"] = "0 0";
        image.props["maxsize"] =
            std::to_string(tileW / static_cast<float>(renderer.width())) + " " +
            std::to_string(tileH / static_cast<float>(renderer.height()));
        themeui::drawImage(renderer, image, roms[index]->coverPath);
    }
    renderer.clearClipRect();
}

}  // namespace

GridView::GridView(std::vector<const ROM *> roms,
                   std::function<View *(const ROM *)> onSelect,
                   std::string title,
                   Theme theme)
    : allRoms_(std::move(roms)), roms_(allRoms_),
      onSelect_(std::move(onSelect)),
      title_(std::move(title)),
      theme_(std::move(theme)) {}

void GridView::onEnter() {
    // Keep the current game and filter when returning from a child view.
    enteredAt_ = selectedAt_ = SDL_GetTicks();
}

void GridView::onLeave() {
    scrolling_ = false;
    waitForRelease_ = true;
}

void GridView::handleText(const std::string &text) {
    if (!editing_) return;
    if (text == "\b" || text == "\x7f") {
        if (draft_.empty()) return;
        size_t last = draft_.size() - 1;
        while (last > 0 && (static_cast<unsigned char>(draft_[last]) & 0xC0) == 0x80) --last;
        draft_.erase(last);
    } else if (draft_.size() + text.size() <= 96 &&
               std::none_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; })) {
        draft_ += text;
    }
}

void GridView::applySearch() {
    if (draft_ == query_) return;
    if (query_.empty()) { fullSelected_ = selected_; fullScroll_ = scrollOffset_; }
    query_ = draft_;
    // Reuse the pointer buffer. Filter once on acceptance without cloning ROM
    // names, metadata, themes, or artwork, and never rescan on keyboard frames.
    roms_.clear();
    auto fold = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
    for (const ROM *rom : allRoms_) {
        if (rom && (query_.empty() || std::search(rom->name.begin(), rom->name.end(), query_.begin(), query_.end(),
            [&](unsigned char a, unsigned char b) { return fold(a) == fold(b); }) != rom->name.end()))
            roms_.push_back(rom);
    }
    selected_ = query_.empty() ? fullSelected_ : 0;
    scrollOffset_ = query_.empty() ? fullScroll_ : 0;
    selectedAt_ = SDL_GetTicks();
    pointerHits_.clear();
}

void GridView::updateFastScroll(uint32_t now, bool held) {
    if (!held) { scrolling_ = false; waitForRelease_ = false; return; }
    if (editing_ || waitForRelease_) return;
    if (scrolling_ && now - lastScroll_ < 50) return;
    lastScroll_ = now;
    scrolling_ = true;
    // Bound each frame to one three-row step; never catch up after a stall.
    for (int i = 0; i < 3; ++i) handleInput(InputButton::Down, InputEvent::Repeat);
}

void GridView::setSelectedIndex(int index) {
    if (roms_.empty()) {
        selected_ = 0;
        return;
    }
    index = std::max(0, std::min(index, static_cast<int>(roms_.size()) - 1));
    if (index != selected_) selectedAt_ = SDL_GetTicks();
    selected_ = index;
}

View *GridView::handleInput(InputButton button, InputEvent event) {
    if (editing_) {
        if (event == InputEvent::Pressed && (button == InputButton::Start || button == InputButton::B)) {
            if (button == InputButton::Start) applySearch();
            editing_ = false;
            waitForRelease_ = true;
        }
        return nullptr;
    }
    if (event == InputEvent::Pressed && button == InputButton::Y) {
        draft_ = query_;
        editing_ = true;
        scrolling_ = false;
        waitForRelease_ = true;
        return nullptr;
    }
    if (event == InputEvent::Pressed && button == InputButton::B && !query_.empty()) {
        draft_.clear();
        applySearch();
        scrolling_ = false;
        waitForRelease_ = true;
        return nullptr;
    }
    if ((event != InputEvent::Pressed && event != InputEvent::Repeat) ||
        roms_.empty()) {
        return nullptr;
    }
    int total = static_cast<int>(roms_.size());
    if (event == InputEvent::Pressed && (button == InputButton::X || button == InputButton::Home || button == InputButton::Select))
        return onOptions_ ? onOptions_(*roms_[selected_]) : nullptr;
    switch (button) {
        case InputButton::Left:
            setSelectedIndex(selected_ - 1);
            return nullptr;
        case InputButton::Right:
            setSelectedIndex(selected_ + 1);
            return nullptr;
        case InputButton::Up:
            setSelectedIndex(selected_ - std::max(1, cols_));
            return nullptr;
        case InputButton::Down:
            setSelectedIndex(std::min(total - 1, selected_ + std::max(1, cols_)));
            return nullptr;
        case InputButton::A:
        case InputButton::Start:
            if (event != InputEvent::Pressed) return nullptr;
            return onSelect_ ? onSelect_(roms_[selected_]) : nullptr;
        default:
            return nullptr;
    }
}

View *GridView::handlePointer(const PointerEvent &event) {
    if (editing_) return nullptr;
    if (event.type == PointerEventType::Pressed && event.button == PointerButton::Primary &&
        cursorAt(event.x, event.y) != CursorKind::Hand) return nullptr;
    if (event.type == PointerEventType::Moved ||
        event.type == PointerEventType::Pressed) {
        for (const GridPointerHit &hit : pointerHits_) {
            if (event.x >= hit.x && event.x < hit.x + hit.w &&
                event.y >= hit.y && event.y < hit.y + hit.h) {
                setSelectedIndex(hit.index);
                break;
            }
        }
    }
    return View::handlePointer(event);
}

CursorKind GridView::cursorAt(int x, int y) const {
    if (editing_) return CursorKind::Normal;
    for (const GridPointerHit &hit : pointerHits_) {
        if (x >= hit.x && x < hit.x + hit.w &&
            y >= hit.y && y < hit.y + hit.h) return CursorKind::Hand;
    }
    return roms_.empty() ? CursorKind::NotAllowed : CursorKind::Normal;
}

void GridView::render(Renderer &renderer, Font &font, const Theme &fallbackTheme) {
    if (editing_) return; // The app draws the shared keyboard without hidden artwork work.
    pointerHits_.clear();
    const Theme &activeTheme = theme_.loaded() ? theme_ : fallbackTheme;
    const uint32_t now = SDL_GetTicks();
    themeui::MotionScope viewMotion(now - enteredAt_);
    const ThemeView *view = activeTheme.loaded() ? chooseGameView(activeTheme) : nullptr;
    if (!view) {
        renderer.clear(0x2B2D31FF);
        font.render(renderer, title_, 12, 8, 0xFFFFFFFF);
        if (roms_.empty()) font.render(renderer, "No games found", 12, 40, 0xFFFFFFFF);
        return;
    }

    const ROM *selectedRom = roms_.empty() ? nullptr : roms_[selected_];
    const ThemeElement *background = view->find("image", "background");
    renderer.clear(background ? background->getColor("color", 0x000000FF)
                              : 0x000000FF);

    std::vector<RenderNode> nodes;
    nodes.reserve(view->elements.size() + 8);
    size_t order = 0;
    for (const ThemeElement &element : view->elements) {
        if (!themeui::visible(element)) continue;
        if (isExtra(element) &&
            (element.type == "image" || element.type == "ninepatch" ||
             element.type == "text")) {
            nodes.push_back({themeui::zIndex(element, 10.0f), order++,
                             element.type == "text" ? NodeKind::ExtraText
                                                    : NodeKind::ExtraImage,
                             &element});
        }
    }

    if (const ThemeElement *list = view->find("textlist", "gamelist"))
        nodes.push_back({themeui::zIndex(*list, 20.0f), order++,
                         NodeKind::GameList, list});
    else if (const ThemeElement *grid = view->find("imagegrid", "gamegrid"))
        nodes.push_back({themeui::zIndex(*grid, 20.0f), order++,
                         NodeKind::ImageGrid, grid});

    const ThemeElement *video = view->find("video", "md_video");
    const ThemeElement *image = view->find("image", "md_image");
    if (video)
        nodes.push_back({themeui::zIndex(*video, 30.0f), order++,
                         NodeKind::Media, video});
    else if (image)
        nodes.push_back({themeui::zIndex(*image, 30.0f), order++,
                         NodeKind::Media, image});
    if (const ThemeElement *marquee = view->find("image", "md_marquee"))
        nodes.push_back({themeui::zIndex(*marquee, 35.0f), order++,
                         NodeKind::Marquee, marquee});

    for (const ThemeElement &element : view->elements) {
        if (isExtra(element) || !themeui::visible(element)) continue;
        if ((element.type == "text" || element.type == "datetime") &&
            element.name.rfind("md_", 0) == 0) {
            const bool dateField = element.name == "md_releasedate" ||
                                   element.name == "md_lastplayed";
            if ((dateField && element.type != "datetime") ||
                (!dateField && element.type == "datetime") ||
                element.name == "md_rating") {
                continue;
            }
            nodes.push_back({themeui::zIndex(element, 40.0f), order++,
                             NodeKind::MetadataText, &element});
        } else if (element.type == "rating" && element.name == "md_rating") {
            nodes.push_back({themeui::zIndex(element, 40.0f), order++,
                             NodeKind::Rating, &element});
        }
    }

    if (const ThemeElement *logo = view->find("image", "logo"))
        nodes.push_back({themeui::zIndex(*logo, 50.0f), order++,
                         NodeKind::LogoImage, logo});
    else if (const ThemeElement *logoText = view->find("text", "logoText"))
        nodes.push_back({themeui::zIndex(*logoText, 50.0f), order++,
                         NodeKind::LogoText, logoText});

    std::stable_sort(nodes.begin(), nodes.end(),
                     [](const RenderNode &left, const RenderNode &right) {
                         if (left.z != right.z) return left.z < right.z;
                         return left.order < right.order;
                     });

    cols_ = 1;
    for (const RenderNode &node : nodes) {
        const ThemeElement &element = *node.element;
        const bool metadata = node.kind == NodeKind::Media || node.kind == NodeKind::Marquee ||
            node.kind == NodeKind::MetadataText || node.kind == NodeKind::Rating;
        themeui::MotionScope nodeMotion(metadata ? now - selectedAt_ : now - enteredAt_, 0, 0,
            metadata ? static_cast<uint8_t>(scrolling_ ? 0 : 255 * themeFade(now - selectedAt_)) : 255);
        switch (node.kind) {
            case NodeKind::ExtraImage:
                themeui::drawImage(renderer, element);
                break;
            case NodeKind::ExtraText:
                themeui::drawText(renderer, font, element);
                break;
            case NodeKind::GameList:
                drawGameList(renderer, font, element, roms_, selected_,
                             scrollOffset_, pointerHits_);
                break;
            case NodeKind::ImageGrid:
                drawImageGrid(renderer, element, roms_, selected_, cols_,
                              rows_, tileW_, tileH_, pointerHits_);
                break;
            case NodeKind::Media:
                if (selectedRom)
                    themeui::drawImage(renderer, element,
                                       selectedRom->coverPath);
                break;
            case NodeKind::Marquee:
                if (!element.get("path").empty())
                    themeui::drawImage(renderer, element);
                break;
            case NodeKind::MetadataText:
                if (selectedRom) {
                    bool label = element.name.rfind("md_lbl_", 0) == 0;
                    std::string value = metadataValue(*selectedRom,
                                                      element.name, label);
                    themeui::drawText(renderer, font, element, value);
                }
                break;
            case NodeKind::Rating:
                if (selectedRom)
                    drawRating(renderer, font, element, selectedRom->rating);
                break;
            case NodeKind::LogoImage:
                themeui::drawImage(renderer, element, "", 0.5f, 0.1f,
                                   0.35f, 0.12f);
                break;
            case NodeKind::LogoText:
                themeui::drawText(renderer, font, element, title_,
                                  0xFFFFFFFF, 0.5f, 0.1f, 0.35f, 0.12f);
                break;
        }
    }

    if (roms_.empty())
        font.renderStyled(renderer, "No games found", 12, renderer.height() / 2,
                          renderer.width() - 24, 24, 0xFFFFFFFF, "", 16, "center");
    const std::string status = std::to_string(roms_.size()) +
        (query_.empty() ? (onOptions_ ? " games | X: Game Options" : " games") : " results: " + query_);
    font.renderStyled(renderer, status, 12, renderer.height() - 22,
                      renderer.width() - 24, 20, 0xD9C9A3AA, "", 12);
    themeui::drawScreenOverlay(renderer, font, activeTheme);
}

}  // namespace lilsmoke
