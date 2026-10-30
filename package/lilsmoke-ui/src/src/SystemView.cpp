#include "SystemView.h"

#include "Font.h"
#include "Log.h"
#include "Renderer.h"
#include "ThemeRender.h"
#include "ThemeActivity.h"
#include <SDL2/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <sys/stat.h>

namespace lilsmoke {

namespace {

static bool fileExists(const std::string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static std::string joinPath(const std::string &left, const std::string &right) {
    if (left.empty()) return right;
    return left.back() == '/' ? left + right : left + "/" + right;
}

static std::vector<std::string> aliasesFor(const std::string &system) {
    if (system == "md") return {"megadrive", "genesis", "md"};
    if (system == "pce") return {"pcengine", "tg16", "pce"};
    if (system == "sms") return {"mastersystem", "sms"};
    if (system == "gg") return {"gamegear", "gg"};
    if (system == "psx") return {"psx", "ps1", "playstation"};
    if (system == "arcade") return {"arcade", "fbneo", "fba", "mame"};
    if (system == "portsmoke") return {"ports", "portsmoke", "pc"};
    if (system == "snes") return {"snes", "sfc"};
    if (system == "gb") return {"gb", "gameboy"};
    if (system == "gbc") return {"gbc", "gbcolor"};
    if (system == "gba") return {"gba", "gbadvance"};
    if (system == "__settings") return {"options", "settings", "tools"};
    if (system == "__explorer") return {"tools", "options", "ports"};
    if (system == "__retroarch") return {"retroarch", "options", "tools"};
    if (system == "__opendingux") return {"opendingux", "ports", "tools", "options"};
    if (system == "__exit") return {"options", "tools"};
    return {system};
}

static bool verticalCarousel(const ThemeElement *carousel) {
    if (!carousel) return false;
    std::string type = carousel->get("type", "horizontal");
    return type == "vertical" || type == "vertical_wheel";
}

static bool backgroundScenery(const ThemeElement &element) {
    if (element.type != "image" && element.type != "ninepatch") return false;
    const auto &name = element.name;
    return name == "background" || name == "bg" ||
        name.compare(0, 11, "background_") == 0 ||
        name.compare(0, 11, "background-") == 0 ||
        name.compare(0, 3, "bg_") == 0 || name.compare(0, 3, "bg-") == 0;
}

static bool sameScenery(const ThemeElement &a, const ThemeElement &b) {
    if (a.type != b.type || a.name != b.name || a.props.size() != b.props.size() ||
        !a.animations.empty() || !b.animations.empty()) return false;
    for (const auto &property : a.props) {
        const auto other = b.props.find(property.first);
        if (other == b.props.end()) return false;
        if (property.second == other->second) continue;
        if (property.first != "path" && property.first != "default" &&
            property.first != "defaultpath") return false;
        if (property.second.empty() || other->second.empty()) return false;
        std::error_code leftError, rightError;
        const auto left = std::filesystem::canonical(property.second, leftError);
        const auto right = std::filesystem::canonical(other->second, rightError);
        if (leftError || rightError || left != right) return false;
    }
    return true;
}

}  // namespace

void SystemView::onEnter() {
    invalidateVisual();
    enteredAt_ = selectedAt_ = SDL_GetTicks();
    previousSelected_ = selected_;
    carouselMotion_.reset(selected_);
}

SystemView::SystemView(std::vector<SystemEntry> entries,
                       std::function<View *(const SystemEntry &)> onSelect,
                       const std::string *themeSetDir)
    : entries_(std::move(entries)),
      onSelect_(std::move(onSelect)),
      themeSetDir_(themeSetDir) {}

void SystemView::setSelectedIndex(int index) {
    if (entries_.empty()) {
        selected_ = 0;
        return;
    }
    index = std::max(0, std::min(index, static_cast<int>(entries_.size()) - 1));
    if (index == selected_) return;
    invalidateVisual();
    const uint32_t now = SDL_GetTicks();
    previousSelected_ = selected_;
    selected_ = index;
    selectedAt_ = now;
    carouselMotion_.select(index, entries_.size(), now);
}

std::string SystemView::resolveThemeAlias(const std::string &system) const {
    std::string root = themeSetDir_ ? *themeSetDir_ : "";
    std::vector<std::string> aliases = aliasesFor(system);
    for (const std::string &alias : aliases) {
        if (fileExists(joinPath(joinPath(root, alias), "theme.xml"))) return alias;
    }
    return aliases.empty() ? system : aliases.front();
}

Theme *SystemView::themeFor(const SystemEntry &entry) {
    const std::string root = themeSetDir_ ? *themeSetDir_ : "";
    const char *subsets = std::getenv("LILSMOKE_THEME_SUBSETS");
    const std::string cacheKey = root + "|" + (subsets ? subsets : "");
    if (cacheKey != cachedThemeSetDir_) {
        cachedThemeSetDir_ = cacheKey;
        themeCache_.clear();
        carouselLayout_.valid = false;
        sceneryView_ = nullptr;
        stationaryScenery_.clear();
    }
    if (root.empty()) return nullptr;

    auto found = themeCache_.find(entry.id);
    if (found != themeCache_.end()) return &found->second;

    Theme loaded;
    const std::string alias = resolveThemeAlias(entry.id);
    LOG_INFO("SystemView: loading theme for %s as %s",
             entry.id.c_str(), alias.c_str());
    loaded.loadSet(root, alias, entry.label);
    LOG_INFO("SystemView: theme ready for %s", entry.id.c_str());
    auto inserted = themeCache_.emplace(entry.id, std::move(loaded));
    return &inserted.first->second;
}

const SystemView::CarouselLayout &SystemView::carouselLayout(
    const ThemeElement *element, int width, int height) {
    auto &layout = carouselLayout_;
    if (layout.valid && layout.element == element && layout.width == width && layout.height == height)
        return layout;
    layout = CarouselLayout{};
    layout.element = element; layout.width = width; layout.height = height;
    float posX = 0, posY = 0.38375f, sizeX = 1, sizeY = 0.2325f;
    float originX = 0, originY = 0, logoPosX = -1, logoPosY = -1;
    std::string alignment = "center";
    if (element) {
        themeui::propertyPair(*element, "pos", posX, posY, posX, posY);
        themeui::propertyPair(*element, "size", sizeX, sizeY, sizeX, sizeY);
        themeui::propertyPair(*element, "origin", originX, originY, originX, originY);
        themeui::propertyPair(*element, "logosize", layout.logoW, layout.logoH, layout.logoW, layout.logoH);
        themeui::propertyPair(*element, "logopos", logoPosX, logoPosY, -1, -1);
        themeui::propertyPair(*element, "logorotationorigin", layout.rotationX, layout.rotationY, -5, 0.5f);
        layout.scale = element->getFloat("logoscale", layout.scale);
        layout.rotation = element->getFloat("logorotation", layout.rotation);
        layout.count = std::clamp(static_cast<int>(std::lround(element->getFloat("maxlogocount", 3))), 1, 11);
        alignment = element->get("logoalignment", alignment);
        const std::string type = element->get("type", "horizontal");
        layout.vertical = type == "vertical" || type == "vertical_wheel";
        layout.wheel = type == "vertical_wheel" || type == "horizontal_wheel";
        layout.color = element->getColor("color", layout.color);
    }
    layout.x = static_cast<int>((posX - originX * sizeX) * width);
    layout.y = static_cast<int>((posY - originY * sizeY) * height);
    layout.w = std::max(1, static_cast<int>(sizeX * width));
    layout.h = std::max(1, static_cast<int>(sizeY * height));
    layout.spacingX = layout.vertical || layout.wheel ? 0
        : ((sizeX - layout.logoW * layout.count) / layout.count) + layout.logoW;
    layout.spacingY = layout.vertical && !layout.wheel
        ? ((sizeY - layout.logoH * layout.count) / layout.count) + layout.logoH : 0;
    layout.centerX = layout.x / static_cast<float>(width) + (logoPosX >= 0 ? logoPosX : sizeX / 2);
    layout.centerY = layout.y / static_cast<float>(height) + (logoPosY >= 0 ? logoPosY : sizeY / 2);
    if (layout.vertical && alignment == "left") {
        layout.originX = 0;
        layout.centerX = layout.x / static_cast<float>(width) + layout.logoW / 10;
    } else if (layout.vertical && alignment == "right") {
        layout.originX = 1;
        layout.centerX = (layout.x + layout.w) / static_cast<float>(width) - layout.logoW * 1.1f;
    }
    if (!layout.vertical && alignment == "top") {
        layout.originY = 0;
        layout.centerY = layout.y / static_cast<float>(height) + layout.logoH / 10;
    } else if (!layout.vertical && alignment == "bottom") {
        layout.originY = 1;
        layout.centerY = (layout.y + layout.h) / static_cast<float>(height) - layout.logoH * 1.1f;
    }
    layout.valid = true;
    return layout;
}

View *SystemView::handleInput(InputButton button, InputEvent event) {
    if ((event != InputEvent::Pressed && event != InputEvent::Repeat) ||
        entries_.empty()) {
        return nullptr;
    }
    switch (button) {
        case InputButton::Left:
        case InputButton::Up:
            setSelectedIndex(wrapThemeIndex(selected_ - 1, entries_.size()));
            return nullptr;
        case InputButton::Right:
        case InputButton::Down:
            setSelectedIndex(wrapThemeIndex(selected_ + 1, entries_.size()));
            return nullptr;
        case InputButton::A:
        case InputButton::Start:
            if (event != InputEvent::Pressed) return nullptr;
            return onSelect_ ? onSelect_(entries_[selected_]) : nullptr;
        default:
            return nullptr;
    }
}

View *SystemView::handlePointer(const PointerEvent &event) {
    if (event.type == PointerEventType::Pressed && event.button == PointerButton::Primary) {
        for (auto hit = pointerHits_.rbegin(); hit != pointerHits_.rend(); ++hit) {
            if (hit->region.contains(event.x, event.y)) {
                if (selected_ != hit->index) {
                    setSelectedIndex(hit->index);
                    return nullptr;
                }
                return handleInput(InputButton::A, InputEvent::Pressed);
            }
        }
        return nullptr;
    }
    return View::handlePointer(event);
}

CursorKind SystemView::cursorAt(int x, int y) const {
    for (auto hit = pointerHits_.rbegin(); hit != pointerHits_.rend(); ++hit) {
        if (hit->region.contains(x, y)) return CursorKind::Hand;
    }
    return entries_.empty() ? CursorKind::NotAllowed : CursorKind::Normal;
}

ViewActivity SystemView::frameActivity(uint32_t now, const Theme &fallback) const {
    const auto active = ViewActivity{ViewActivity::State::Active, visualRevision(), selectedAt_ + 500, true};
    if (!firstFrameComplete_) return active;
    const std::string root = themeSetDir_ ? *themeSetDir_ : "";
    const char *subsets = std::getenv("LILSMOKE_THEME_SUBSETS");
    if (!entries_.empty() && root + "|" + (subsets ? subsets : "") != cachedThemeSetDir_)
        return active;
    if (now - selectedAt_ < 500 || carouselMotion_.moving(now)) return active;
    if (themeui::systemThemeMayChange(fallback)) return active;
    for (const auto &entry : themeCache_)
        if (themeui::systemThemeMayChange(entry.second)) return active;
    return {ViewActivity::State::Settled, visualRevision(), 0, false};
}

void SystemView::render(Renderer &renderer, Font &font, const Theme &fallbackTheme) {
    pointerHits_.clear();
    const bool firstFrame = !firstFrameComplete_;
    if (firstFrame && !entries_.empty()) {
        LOG_INFO("SystemView: rendering first frame for %s",
                 entries_[selected_].id.c_str());
    }
    if (entries_.empty()) {
        renderer.clear(0x111111FF);
        font.render(renderer, "No systems found", 18, 18, 0xFFFFFFFF);
        firstFrameComplete_ = true;
        return;
    }

    Theme *active = themeFor(entries_[selected_]);
    const Theme &theme = active && active->loaded() ? *active : fallbackTheme;
    const uint32_t now = SDL_GetTicks();
    if (firstFrame) {
        enteredAt_ = selectedAt_ = now;
        carouselMotion_.reset(selected_);
    }
    const char *overrideStyle = std::getenv("LILSMOKE_THEME_TRANSITION");
    std::string style = overrideStyle ? overrideStyle : "auto";
    if (style == "auto") style = theme.defaultTransition();
    if (style != "instant" && style != "fade") style = "slide";
    if (style == "instant") carouselMotion_.reset(selected_);
    const float camera = carouselMotion_.position(now);
    const float transition = themeFade(now - selectedAt_, 500);
    themeui::MotionScope viewMotion(now - selectedAt_);
    const ThemeView *view = theme.findView("system");
    if (!view) {
        renderer.clear(0x202020FF);
        font.render(renderer, entries_[selected_].label, 24, 24, 0xFFFFFFFF);
        pointerHits_.push_back({{{16, 16, renderer.width() - 32, 48},
                                {0, 0, renderer.width(), renderer.height()}}, selected_});
        if (firstFrame) LOG_INFO("SystemView: first frame complete (fallback)");
        firstFrameComplete_ = true;
        return;
    }

    const ThemeElement *background = view->find("image", "background");
    renderer.clear(background ? background->getColor("color", 0x000000FF)
                              : 0x000000FF);

    const ThemeElement *carousel = view->find("carousel", "systemcarousel");
    const ThemeElement *systemInfo = view->find("text", "systemInfo");
    const float carouselZ = carousel ? themeui::zIndex(*carousel, 40.0f) : 40.0f;
    const float infoZ = systemInfo ? themeui::zIndex(*systemInfo, 50.0f) : 50.0f;

    const themeui::Rect viewport{0, 0, renderer.width(), renderer.height()};
    std::array<themeui::ExtraLayer, 3> extraLayers{};
    extraLayers[0].view = view;
    extraLayers[0].clip = viewport;
    extraLayers[0].elapsed = now - enteredAt_;
    extraLayers[0].fixedBackgrounds = true;
    extraLayers[0].stationary = &stationaryScenery_;
    size_t extraLayerCount = 1;
    std::array<int, 2> pageIndices{{selected_, selected_}};
    auto addPage = [&](int index, int x, int y, uint8_t opacity) {
        Theme *pageTheme = themeFor(entries_[index]);
        auto &layer = extraLayers[extraLayerCount++];
        layer.view = pageTheme && pageTheme->loaded() ? pageTheme->findView("system") : view;
        layer.offsetX = x; layer.offsetY = y;
        const int left = std::max(0, x), top = std::max(0, y);
        layer.clip = {left, top, std::max(0, std::min(viewport.w, x + viewport.w) - left),
                               std::max(0, std::min(viewport.h, y + viewport.h) - top)};
        layer.elapsed = now - selectedAt_;
        layer.opacity = opacity;
        layer.event = index == selected_ ? "activate" : "deactivate";
        layer.stationary = &stationaryScenery_;
    };
    if (style == "slide" && carouselMotion_.moving(now)) {
        const int base = static_cast<int>(std::floor(camera));
        const bool vertical = verticalCarousel(carousel);
        const int extent = vertical ? viewport.h : viewport.w;
        const int origin = static_cast<int>(std::lround((base - camera) * extent));
        for (int n = 0; n < 2; ++n) {
            pageIndices[n] = wrapThemeIndex(base + n, entries_.size());
            const int offset = origin + n * extent;
            addPage(pageIndices[n], vertical ? 0 : offset, vertical ? offset : 0, 255);
        }
    } else if (style == "fade" && transition < 1.0f) {
        pageIndices = {{previousSelected_, selected_}};
        addPage(transition < 0.5f ? previousSelected_ : selected_, 0, 0,
                static_cast<uint8_t>(255 * std::fabs(2 * transition - 1)));
    } else addPage(selected_, 0, 0, 255);

    const std::array<int, 3> sceneryIndices{{selected_, pageIndices[0], pageIndices[1]}};
    if (view != sceneryView_ || sceneryIndices != sceneryIndices_) {
        sceneryView_ = view;
        sceneryIndices_ = sceneryIndices;
        stationaryScenery_.clear();
        std::array<const ThemeView *, 2> peers{};
        for (size_t n = 0; n < peers.size(); ++n) {
            Theme *peer = themeFor(entries_[pageIndices[n]]);
            peers[n] = peer && peer->loaded() ? peer->findView("system") : view;
        }
        // Resolve shared scenery once per page pair, never on every animation frame.
        for (const auto &element : view->elements) {
            if (!backgroundScenery(element) || !element.animations.empty()) continue;
            const ThemeElement *matches[2] = {};
            bool common = true;
            for (size_t n = 0; n < peers.size(); ++n) {
                matches[n] = peers[n] ? peers[n]->find(element.type, element.name) : nullptr;
                if (!matches[n] || !sameScenery(element, *matches[n])) common = false;
            }
            if (common) {
                stationaryScenery_.push_back(&element);
                for (const auto *match : matches)
                    if (std::find(stationaryScenery_.begin(), stationaryScenery_.end(), match) == stationaryScenery_.end())
                        stationaryScenery_.push_back(match);
            }
        }
    }

    auto drawCarousel = [&]() {
        const auto &layout = carouselLayout(carousel, renderer.width(), renderer.height());
        const int boxX = layout.x, boxY = layout.y, boxW = layout.w, boxH = layout.h;
        const float logoW = layout.logoW, logoH = layout.logoH, logoScale = layout.scale;
        const int maxLogoCount = layout.count;
        if (boxX >= renderer.width() || boxY >= renderer.height() ||
            boxX + boxW <= 0 || boxY + boxH <= 0) return;
        if ((layout.color & 0xFF) != 0)
            renderer.fillRect(boxX, boxY, boxW, boxH, layout.color);

        renderer.setClipRect(boxX, boxY, boxW, boxH);
        int visibleCount = std::min(maxLogoCount, static_cast<int>(entries_.size()));
        const int half = visibleCount / 2;
        const int center = visibleCount == 1 ? selected_ : static_cast<int>(std::floor(camera));
        const bool moving = carouselMotion_.moving(now);
        const int buffer = moving && visibleCount > 1 ? 1 : 0;
        struct Item { int index; float distance; };
        std::array<Item, 13> items{};
        size_t itemCount = 0;
        const int count = static_cast<int>(entries_.size());
        const float startCamera = carouselMotion_.position(selectedAt_);
        const float targetCamera = carouselMotion_.position(selectedAt_ + 500);
        for (int offset = -half - buffer; offset <= half + buffer; ++offset) {
            const int page = center + offset;
            const int index = wrapThemeIndex(page, count);
            if (visibleCount > 1) {
                const int lower = index + static_cast<int>(std::floor((camera - index) / count)) * count;
                const int upper = lower + count;
                const float lowDistance = std::fabs(camera - lower);
                const float highDistance = std::fabs(camera - upper);
                // At a half-turn tie, retain the incoming/outgoing copy's side.
                const float reference = index == selected_ ? targetCamera : startCamera;
                const int nearest = std::fabs(lowDistance - highDistance) < 0.00001f
                    ? (std::fabs(lower - reference) <= std::fabs(upper - reference) ? lower : upper)
                    : (lowDistance < highDistance ? lower : upper);
                if (page != nearest) continue;
            }
            items[itemCount++] = {index, visibleCount == 1 ? 0.0f : page - camera};
        }
        std::sort(items.begin(), items.begin() + itemCount, [](const Item &a, const Item &b) {
            if (std::fabs(a.distance) != std::fabs(b.distance))
                return std::fabs(a.distance) < std::fabs(b.distance);
            return a.distance < b.distance;
        });
        itemCount = std::min(itemCount, static_cast<size_t>(visibleCount + 2 * buffer));
        std::reverse(items.begin(), items.begin() + itemCount);
        for (size_t item = 0; item < itemCount; ++item) {
            const int index = items[item].index;
            const float distance = items[item].distance;

            Theme *entryTheme = themeFor(entries_[index]);
            const ThemeView *entryView = entryTheme ? entryTheme->findView("system") : nullptr;
            const ThemeElement *logo = entryView ? entryView->find("image", "logo") : nullptr;
            const ThemeElement *logoText = entryView ? entryView->find("text", "logoText") : nullptr;
            const float focus = std::max(0.0f, 1.0f - std::fabs(distance));
            const float scale = 1.0f + (logoScale - 1.0f) * focus;
            const uint8_t opacity = static_cast<uint8_t>(128 + std::lround(127 * focus));
            const float itemX = layout.centerX + distance * layout.spacingX;
            const float itemY = layout.centerY + distance * layout.spacingY;
            themeui::ElementLayout itemLayout;
            itemLayout.pos = {{itemX, itemY}};
            itemLayout.origin = {{layout.originX, layout.originY}};

            themeui::PaintedRegion painted;
            if (logo && !logo->get("path").empty()) {
                itemLayout.ignoreSize = true;
                itemLayout.maxSize = {{logoW * scale, logoH * scale}};
                if (layout.wheel) {
                    itemLayout.rotation = layout.rotation * distance;
                    itemLayout.rotationOrigin = {{layout.rotationX, layout.rotationY}};
                }
                themeui::MotionScope logoMotion(now - selectedAt_, 0, 0, 255,
                                               index == selected_ ? "activate" : "deactivate");
                themeui::drawImage(renderer, *logo, "", itemX, itemY,
                                   logoW * scale, logoH * scale, opacity, &painted, &itemLayout);
            } else {
                static const ThemeElement fallbackText;
                const ThemeElement &text = logoText ? *logoText : fallbackText;
                itemLayout.size = {{logoW * scale, logoH * scale}};
                itemLayout.fontSize = text.getFloat("fontsize", 0.035f) * scale;
                itemLayout.ignoreColor = true;
                const uint32_t color = text.getColor("color", 0xFFFFFFFF);
                themeui::drawText(renderer, font, text, entries_[index].label,
                                  (color & 0xFFFFFF00U) | opacity,
                                  0, 0, 0, 0, &painted, &itemLayout);
            }
            if (painted.rect.w > 0 && painted.rect.h > 0)
                pointerHits_.push_back({painted, index});
        }

        // Some ArkOS multi-artwork themes successively assign a system logo
        // and a tall artwork strip to the same ES element name. The artwork
        // is the carousel item, while the earlier logo remains the selected
        // system overlay in the reference layout.
        const ThemeElement *selectedLogo = view->find("image", "logo");
        const std::string overlayPath = selectedLogo
            ? selectedLogo->get("__previouspath") : "";
        if (!overlayPath.empty() && maxLogoCount > 1 &&
            (logoW >= 0.75f || logoH >= 0.75f)) {
            themeui::ElementLayout overlay;
            overlay.ignoreSize = true;
            overlay.maxSize = {{0.62f, 0.30f}};
            overlay.pos = {{0.5f, 0.5f}};
            overlay.origin = {{0.5f, 0.5f}};
            themeui::PaintedRegion painted;
            themeui::drawImage(renderer, *selectedLogo, overlayPath, 0, 0, 1, 1, 255, &painted, &overlay);
            if (painted.rect.w > 0 && painted.rect.h > 0)
                pointerHits_.push_back({painted, selected_});
        }
        renderer.clearClipRect();
    };

    auto drawInfo = [&]() {
        if (!systemInfo || !themeui::visible(*systemInfo)) return;
        themeui::MotionScope infoMotion(now - selectedAt_, 0, 0,
            static_cast<uint8_t>(255 * themeFade(now - selectedAt_, 300)));
        std::string label = std::to_string(entries_[selected_].gameCount) +
                            (entries_[selected_].gameCount == 1
                                 ? " GAME AVAILABLE"
                                 : " GAMES AVAILABLE");
        themeui::drawText(renderer, font, *systemInfo, label, 0x000000FF,
                          0.0f, 0.62f, 1.0f, 0.08f);
    };

    const float firstZ = std::min(carouselZ, infoZ);
    const float secondZ = std::max(carouselZ, infoZ);
    auto drawBackgrounds = [&](float lower, float upper) {
        themeui::drawSystemExtras(renderer, font, extraLayers.data(), extraLayerCount,
                                  lower, upper, 10.0f);
    };
    drawBackgrounds(-100000.0f, firstZ);
    if (carouselZ <= infoZ) drawCarousel(); else drawInfo();
    drawBackgrounds(firstZ, secondZ);
    if (carouselZ <= infoZ) drawInfo(); else drawCarousel();
    drawBackgrounds(secondZ, 100000.0f);

    const bool vertical = verticalCarousel(carousel);
    themeui::drawHelp(renderer, font, view,
                      vertical ? "UP/DOWN  CHOOSE    A  SELECT    START  MENU"
                               : "LEFT/RIGHT  CHOOSE    A  SELECT    START  MENU");
    themeui::drawScreenOverlay(renderer, font, theme);
    if (firstFrame) LOG_INFO("SystemView: first frame complete");
    firstFrameComplete_ = true;
}

}  // namespace lilsmoke
