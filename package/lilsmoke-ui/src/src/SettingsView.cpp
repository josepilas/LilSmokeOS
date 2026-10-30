// LilSmokeUI — SettingsView.cpp

#include "SettingsView.h"
#include "Log.h"
#include "EmulatorRegistry.h"
#include "ProcessLaunch.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <unistd.h>

namespace lilsmoke {

namespace {

std::vector<std::string> installedThemes(const Config &config) {
    namespace fs = std::filesystem;
    std::set<std::string> names;
    names.insert("default");
    const char *roots[] = {
        "/storage/themes",
        "/storage/.config/emulationstation/themes",
        "/roms/themes",
        "/userdata/themes",
        "/usr/share/lilsmoke/themes",
    };

    for (const char *root : roots) {
        std::error_code ec;
        fs::directory_iterator end;
        for (fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
             !ec && it != end; it.increment(ec)) {
            std::error_code itemEc;
            if (it->is_symlink(itemEc) || !it->is_directory(itemEc)) continue;
            if (!fs::is_regular_file(it->path() / "theme.xml", itemEc)) continue;
            names.insert(it->path().filename().string());
            if (names.size() >= 64) break;
        }
    }

    std::string current = config.get("DEFAULT_THEME", "default");
    if (!current.empty()) names.insert(current);
    return std::vector<std::string>(names.begin(), names.end());
}

}  // namespace

SettingsView::SettingsView(Config &config, FavoritesStore favorites) : config_(config), favorites_(std::move(favorites)) {
    settings_ = {
        {"__POWER_OFF__", "Power Off", {"open"}},
        {"__SCAN_NOW__", "Scan games now", {"open"}},
        {"EMULATOR_BACKEND", "Emulator Core Settings", {"retroarch", "opendingux"}},
        {"WIFI_ENABLED",     "WiFi",                 {"off", "on"}},
        {"SSH_ENABLED",      "SSH (Dropbear)",       {"off", "on"}},
        {"__SHELL_TERMINAL__", "Shell Terminal",       {"open"}},
        {"USB_MODE",         "USB connection",       {"off", "network", "drive", "network+drive"}},
        {"__REMOTE_ACCESS__", "Remote access info",   {"open"}},
        {"DISPLAY_ROTATION", "Display rotation",     {"0", "90", "180", "270"}},
        {"DEVICE_MODE",      "Presentation",         {"handheld", "tv"}},
        {"UI_RENDER_MODE",   "UI rendering",         {"balanced", "native"}},
        {"TV_SAFE_AREA_PERCENT", "TV safe margin (%)", {"0", "2", "3", "5", "7", "10"}},
        {"SCALING_MODE",     "Scaling",              {"integer", "stretch", "aspect"}},
        {"SHOW_CLOCK",       "Clock overlay",         {"off", "on"}},
        {"SHOW_BATTERY",     "Battery overlay",       {"off", "icon", "text"}},
        {"DEFAULT_THEME",    "Theme",                installedThemes(config)},
        {"THEME_SYSTEM_VIEW", "Theme system layout", {"theme-default", "multi", "centered", "centered-one-logo", "small", "rgb30", "no-artwork"}},
        {"THEME_GAMELIST_VIEW", "Theme game list",    {"theme-default", "metadata-on", "metadata-off", "No Metadata", "w/ Description", "w/ Metadata"}},
        {"THEME_TRANSITION", "Theme transition", {"auto", "slide", "fade", "instant"}},
        {"MASTER_VOLUME",    "Master volume",        {"0", "10", "20", "30", "40", "50", "60", "70", "80", "90", "100"}},
        {"POINTER_ENABLED",  "Mouse / touchpad",     {"off", "on"}},
        {"POINTER_SPEED",    "Pointer speed",        {"slow", "normal", "fast"}},
        {"CURSOR_SIZE",      "Cursor size",          {"24", "32", "40", "48"}},
        {"ZRAM_ENABLED",     "ZRAM swap",            {"off", "on"}},
        {"ZRAM_SIZE_MB",     "ZRAM size (MB)",       {"24", "32", "48", "64"}},
        {"CPU_GOVERNOR",     "CPU governor",         {"ondemand", "performance", "powersave"}},
        {"CPU_OVERCLOCK",    "Overclocking",         {"stock", "1300MHz", "1400MHz"}},
        {"SCAN_ROMS_ON_BOOT","Scan ROMs on boot",    {"off", "on"}},
        {"START_IN_RECENTS", "Start in Recents", {"off", "on"}},
        {"HIDE_EMPTY_SYSTEMS", "Hide Empty Systems", {"off", "on"}},
        {"AUTO_SLEEP_MINUTES","Auto-sleep (min)",    {"0", "5", "10", "15", "30"}},
        {"LCD_BRIGHTNESS",   "LCD brightness",       {"20", "40", "60", "80", "100"}},
        {"__INPUT_SETTINGS__","Gamepad mapping", {"open"}},
        {"__OTA_UPDATE__",   "OTA Update",            {"open"}},
        {"GAMEPAD_BACKEND",  "Controller backend",    {"linuxraw", "sdl2"}},
        {"__BLUETOOTH__", "Bluetooth devices", {"open"}},
        {"RETROARCH_AUTO_RESUME", "RetroArch Auto Resume", {"off", "on"}},
    };
}

void SettingsView::onEnter() {
    history_.load();
    favorites_.load();
    selected_ = std::clamp(selected_, 0, static_cast<int>(settings_.size()) - 1);
}

int SettingsView::optionIndex(const std::string &key) const {
    if (key == "HIDE_EMPTY_SYSTEMS") return favorites_.hideEmptySystems() ? 1 : 0;
    if (key == "START_IN_RECENTS") return history_.startInRecents() ? 1 : 0;
    std::string v = config_.get(key);
    if (v.empty()) {
        if (key == "POINTER_ENABLED") v = "on";
        else if (key == "POINTER_SPEED") v = "normal";
        else if (key == "CURSOR_SIZE") v = "32";
    }
    if (v == "0" || v == "false" || v == "no") v = "off";
    if (v == "1" || v == "true" || v == "yes") v = "on";
    for (size_t i = 0; i < settings_.size(); i++) {
        if (settings_[i].key == key) {
            for (size_t j = 0; j < settings_[i].options.size(); j++) {
                if (settings_[i].options[j] == v) return (int)j;
            }
            // Default to first option if current value isn't in the list
            return 0;
        }
    }
    return 0;
}

void SettingsView::setOption(const std::string &key, int idx, bool confirmed) {
    if (unavailable(key)) return;
    if (key == "HIDE_EMPTY_SYSTEMS") {
        if (!favorites_.setHideEmptySystems(idx == 1)) terminalError_ = favorites_.error();
        else {
            terminalError_.clear();
            LOG_INFO("Library: hide empty systems=%d", favorites_.hideEmptySystems());
            if (onLibraryChanged_) onLibraryChanged_(favorites_.hideEmptySystems());
        }
        return;
    }
    if (key == "START_IN_RECENTS") {
        terminalError_ = history_.setStartInRecents(idx == 1) ? "" : "Could not save Recents on LILROMS";
        return;
    }
    for (auto &s : settings_) {
        if (s.key == key && idx >= 0 && idx < (int)s.options.size()) {
            if (key == "USB_MODE" && !confirmed &&
                config_.get(key).find("drive") != std::string::npos &&
                config_.get(key) != s.options[idx]) {
                pendingUsbOption_ = idx;
                return;
            }
            config_.set(key, s.options[idx]);
            if (key == "EMULATOR_BACKEND") {
                for (const auto &system : emulatorSystems()) config_.set("EMULATOR_" + system, "");
            }
            config_.save();
            LOG_INFO("Settings: %s = %s", key.c_str(), s.options[idx].c_str());
            if (key == "UI_RENDER_MODE") {
                requestProcessLaunch({"/usr/bin/lilsmoke-ui", {"lilsmoke-ui"}});
                return;
            }
            if ((key == "DEFAULT_THEME" || key == "THEME_SYSTEM_VIEW" ||
                 key == "THEME_GAMELIST_VIEW" || key == "THEME_TRANSITION") && onThemeChanged_) {
                onThemeChanged_(config_.get("DEFAULT_THEME", "default"));
            }
            if (key == "SHOW_CLOCK") {
                setenv("LILSMOKE_DRAW_CLOCK",
                       config_.getBool("SHOW_CLOCK", false) ? "1" : "0", 1);
            } else if (key == "SHOW_BATTERY") {
                const std::string mode = config_.get("DEVICE_MODE", "handheld") == "tv" ?
                    "off" : config_.get("SHOW_BATTERY", "text");
                setenv("LILSMOKE_SHOW_BATTERY", mode.c_str(), 1);
            }
            if ((key == "DEVICE_MODE" || key == "TV_SAFE_AREA_PERCENT") &&
                onPresentationChanged_) onPresentationChanged_();
            if ((key == "POINTER_ENABLED" || key == "POINTER_SPEED") &&
                onPointerChanged_) {
                onPointerChanged_(config_.getBool("POINTER_ENABLED", true),
                                  config_.get("POINTER_SPEED", "normal"));
            }
            applyRuntimeSetting(key);
            return;
        }
    }
}

void SettingsView::applyRuntimeSetting(const std::string &key) const {
    if (key == "USB_MODE") {
        int rc = std::system(
            "if [ -x /usr/lib/lilsmoke/lilsmoke-usb ]; then "
            "/usr/lib/lilsmoke/lilsmoke-usb apply >>/var/log/lilsmoke-usb.log 2>&1; "
            "if [ -x /etc/init.d/S30dropbear ]; then /etc/init.d/S30dropbear restart; fi; "
            "fi &");
        if (rc != 0) LOG_WARN("Settings: USB apply could not be started");
        return;
    }

    if (key == "WIFI_ENABLED") {
        int rc = std::system(
            "(/etc/init.d/S20network restart; /etc/init.d/S30dropbear restart) "
            ">>/var/log/lilsmoke-network.log 2>&1 &");
        if (rc != 0) LOG_WARN("Settings: network restart could not be started");
        return;
    }

    if (key == "SSH_ENABLED") {
        int rc = std::system(
            "/etc/init.d/S30dropbear restart >>/var/log/dropbear.log 2>&1 &");
        if (rc != 0) LOG_WARN("Settings: SSH restart could not be started");
        return;
    }

    if (key == "ZRAM_ENABLED" || key == "ZRAM_SIZE_MB") {
        int rc = std::system(
            "if [ -x /usr/lib/lilsmoke/lilsmoke-zram ]; then "
            "/usr/lib/lilsmoke/lilsmoke-zram apply >>/var/log/lilsmoke-zram.log 2>&1; "
            "fi");
        if (rc != 0) {
            LOG_WARN("Settings: ZRAM helper exited with status %d", rc);
        }
        return;
    }

    if (key != "CPU_GOVERNOR" && key != "CPU_OVERCLOCK") return;

    int rc = std::system(
        "if [ -x /usr/lib/lilsmoke/lilsmoke-overclock ]; then "
        "/usr/lib/lilsmoke/lilsmoke-overclock apply >>/var/log/lilsmoke-overclock.log 2>&1; "
        "/usr/lib/lilsmoke/lilsmoke-overclock watch >>/var/log/lilsmoke-overclock.log 2>&1 & "
        "fi");
    if (rc != 0) {
        LOG_WARN("Settings: performance apply helper exited with status %d", rc);
    }
}

View *SettingsView::handleInput(InputButton btn, InputEvent evt) {
    if (pendingPowerOff_) {
        if (evt != InputEvent::Pressed) return nullptr;
        if (btn == InputButton::Left || btn == InputButton::Right)
            confirmPowerOff_ = !confirmPowerOff_;
        else if (btn == InputButton::B) pendingPowerOff_ = false;
        else if (btn == InputButton::A || btn == InputButton::Start) {
            pendingPowerOff_ = false;
            if (confirmPowerOff_ && onPowerOff_) onPowerOff_();
        }
        return nullptr;
    }
    if (pendingUsbOption_ >= 0) {
        if (evt == InputEvent::Pressed) {
            if (btn == InputButton::A || btn == InputButton::Start) {
                int option = pendingUsbOption_;
                pendingUsbOption_ = -1;
                setOption("USB_MODE", option, true);
            } else if (btn == InputButton::B) pendingUsbOption_ = -1;
        }
        return nullptr;
    }
    if (evt != InputEvent::Pressed && evt != InputEvent::Repeat) return nullptr;

    // Special "open" entries (submenus)
    if (btn == InputButton::A || btn == InputButton::Start || btn == InputButton::Right) {
        const Setting &s = settings_[selected_];
        if (s.key == "EMULATOR_BACKEND" && btn != InputButton::Right) {
            if (evt == InputEvent::Pressed && onOpenEmulators_) return onOpenEmulators_();
            return nullptr;
        }
        if (s.key == "__POWER_OFF__") {
            pendingPowerOff_ = true;
            confirmPowerOff_ = false;
            return nullptr;
        } else if (s.key == "__SCAN_NOW__") {
            if (evt == InputEvent::Pressed && onScanGames_) onScanGames_();
            return nullptr;
        } else if (s.key == "__BLUETOOTH__") {
            if (onOpenBluetooth_) return onOpenBluetooth_();
        } else if (s.key == "__INPUT_SETTINGS__") {
            if (onOpenInput_) return onOpenInput_();
        } else if (s.key == "__OTA_UPDATE__") {
            if (onOpenOTA_) return onOpenOTA_();
        } else if (s.key == "__REMOTE_ACCESS__") {
            if (onOpenRemote_) return onOpenRemote_();
        } else if (s.key == "__SHELL_TERMINAL__") {
            if (evt == InputEvent::Pressed) {
                const char *launcher = "/usr/lib/lilsmoke/lilsmoke-terminal-launch";
                if (access(launcher, X_OK) != 0 || access("/usr/bin/lilsmoke-terminal", X_OK) != 0) {
                    terminalError_ = "Shell Terminal is not installed";
                    LOG_WARN("Settings: Shell Terminal helper is not installed");
                } else {
                    terminalError_.clear();
                    config_.save();
                    requestProcessLaunch({launcher, {"lilsmoke-terminal-launch"}});
                }
            }
            return nullptr;
        }
    }

    switch (btn) {
        case InputButton::Up:
            if (selected_ > 0) selected_--;
            return nullptr;
        case InputButton::Down:
            if (selected_ < (int)settings_.size() - 1) selected_++;
            return nullptr;
        case InputButton::Left: {
            const Setting &s = settings_[selected_];
            if (s.options.size() > 1) {
                int idx = optionIndex(s.key);
                if (s.key == "EMULATOR_BACKEND") setOption(s.key, (idx + 1) % 2);
                else if (idx > 0) setOption(s.key, idx - 1);
            }
            return nullptr;
        }
        case InputButton::Right: {
            const Setting &s = settings_[selected_];
            if (s.options.size() > 1) {
                int idx = optionIndex(s.key);
                if (s.key == "EMULATOR_BACKEND") setOption(s.key, (idx + 1) % 2);
                else if (idx < (int)s.options.size() - 1) setOption(s.key, idx + 1);
            }
            return nullptr;
        }
        case InputButton::B:
            if (onBack_) return onBack_();
            return nullptr;
        default:
            return nullptr;
    }
}

bool SettingsView::unavailable(const std::string &key) const {
    const bool tv = config_.get("DEVICE_MODE", "handheld") == "tv";
    return tv ? (key == "LCD_BRIGHTNESS" || key == "AUTO_SLEEP_MINUTES" ||
                 key == "SHOW_BATTERY") : key == "TV_SAFE_AREA_PERCENT";
}

int SettingsView::pointerSettingIndex(int x, int y) const {
    if (x < hitX_ || x >= hitX_ + hitW_ ||
        y < hitY_ || y >= hitY_ + hitH_) return -1;
    int index = scrollOffset_ + (y - hitY_) / std::max(1, hitRowH_);
    return index >= 0 && index < static_cast<int>(settings_.size()) ? index : -1;
}

View *SettingsView::handlePointer(const PointerEvent &event) {
    if (pendingPowerOff_) {
        if (event.type == PointerEventType::Pressed) {
            if (event.button == PointerButton::Secondary)
                return handleInput(InputButton::B, InputEvent::Pressed);
            if (event.button == PointerButton::Primary &&
                event.y >= screenH_ / 2 + 12 && event.y < screenH_ / 2 + 44) {
                if (event.x >= screenW_ / 2 - 170 && event.x < screenW_ / 2 - 22) {
                    confirmPowerOff_ = true;
                    return handleInput(InputButton::A, InputEvent::Pressed);
                }
                if (event.x >= screenW_ / 2 + 10 && event.x < screenW_ / 2 + 142)
                    return handleInput(InputButton::B, InputEvent::Pressed);
            }
        }
        return nullptr;
    }
    if (pendingUsbOption_ >= 0) {
        if (event.type == PointerEventType::Pressed) {
            if (event.button == PointerButton::Secondary)
                return handleInput(InputButton::B, InputEvent::Pressed);
            if (event.button == PointerButton::Primary &&
                event.y >= screenH_ / 2 + 12 && event.y < screenH_ / 2 + 44) {
                if (event.x >= screenW_ / 2 - 170 && event.x < screenW_ / 2 - 22)
                    return handleInput(InputButton::A, InputEvent::Pressed);
                if (event.x >= screenW_ / 2 + 10 && event.x < screenW_ / 2 + 142)
                    return handleInput(InputButton::B, InputEvent::Pressed);
            }
        }
        return nullptr;
    }
    if (event.type == PointerEventType::Pressed && event.button == PointerButton::Primary &&
        pointerSettingIndex(event.x, event.y) < 0) return nullptr;
    if (event.type == PointerEventType::Moved ||
        event.type == PointerEventType::Pressed) {
        int index = pointerSettingIndex(event.x, event.y);
        if (index >= 0) {
            selected_ = index;
            if (event.type == PointerEventType::Pressed &&
                event.button == PointerButton::Primary &&
                settings_[static_cast<size_t>(index)].key.rfind("__", 0) != 0) {
                const Setting &setting = settings_[static_cast<size_t>(index)];
                if (setting.key == "EMULATOR_BACKEND")
                    return handleInput(InputButton::A, InputEvent::Pressed);
                int next = optionIndex(setting.key) + 1;
                if (next >= static_cast<int>(setting.options.size())) next = 0;
                setOption(setting.key, next);
                return nullptr;
            }
        }
    }
    return View::handlePointer(event);
}

CursorKind SettingsView::cursorAt(int x, int y) const {
    if (pendingPowerOff_) return CursorKind::Help;
    if (pendingUsbOption_ >= 0) return CursorKind::Help;
    int index = pointerSettingIndex(x, y);
    if (index < 0) return CursorKind::Normal;
    if (settings_[static_cast<size_t>(index)].key.rfind("__", 0) == 0)
        return CursorKind::Hand;
    return CursorKind::Hand;
}

void SettingsView::render(Renderer &r, Font &font, const Theme &theme) {
    (void)theme;
    screenW_ = r.width();
    screenH_ = r.height();
    r.clear(0x2B2D31FF);
    if (pendingPowerOff_) {
        const int x = std::max(8, r.width() / 2 - 190);
        const int y = r.height() / 2 - 56;
        r.fillRect(x, y, std::min(380, r.width() - 16), 112, 0x15191FFF);
        font.render(r, "Power Off?", x + 12, y + 12, 0xFFFFFFFF);
        r.fillRect(r.width() / 2 - 170, r.height() / 2 + 12, 148, 32,
                   confirmPowerOff_ ? 0x9C3E3EFF : 0x343A40FF);
        r.fillRect(r.width() / 2 + 10, r.height() / 2 + 12, 132, 32,
                   confirmPowerOff_ ? 0x343A40FF : 0x476D5BFF);
        font.render(r, "Power Off", r.width() / 2 - 160, r.height() / 2 + 20, 0xFFFFFFFF);
        font.render(r, "Cancel", r.width() / 2 + 20, r.height() / 2 + 20, 0xFFFFFFFF);
        return;
    }
    if (pendingUsbOption_ >= 0) {
        const int x = std::max(8, r.width() / 2 - 190);
        const int y = r.height() / 2 - 56;
        const int width = std::min(380, r.width() - 16);
        r.fillRect(x, y, width, 112, 0x15191FFF);
        font.renderStyled(r, "Safely eject LILDRIVE on the PC first.", x + 12, y + 12,
                          width - 24, 40, 0xFFFFFFFF, "", 16, "left", "top", true);
        font.render(r, "A Continue", r.width() / 2 - 170, r.height() / 2 + 20, 0xFFFFFFFF);
        font.render(r, "B Cancel", r.width() / 2 + 10, r.height() / 2 + 20, 0xFFFFFFFF);
        return;
    }

    // Title
    r.fillRect(0, 0, r.width(), 36, 0x1E2A47FF);
    font.render(r, "Settings", 12, 8, 0xD9C9A3FF);

    int y = 50;
    int rowH = 28;
    int labelX = 16;
    int valueX = r.width() / 2;
    int rowW = r.width() - 32;
    auto drawCell = [&](std::string text, int x, int top, int width, uint32_t color) {
        int size = 16, tw = 0, th = 0;
        font.textSizeStyled(text, "", size, tw, th);
        while (tw > width && size > 12) {
            font.textSizeStyled(text, "", --size, tw, th);
        }
        if (tw > width) {
            const auto removeLastCharacter = [](std::string &value) {
                size_t last = value.size() - 1;
                while (last > 0 && (static_cast<unsigned char>(value[last]) & 0xC0) == 0x80)
                    --last;
                value.resize(last);
            };
            do {
                removeLastCharacter(text);
                font.textSizeStyled(text + "...", "", size, tw, th);
            } while (tw > width && !text.empty());
            text += "...";
        }
        font.renderStyled(r, text, x, top, width, rowH - 6, color, "", size);
        return tw;
    };

    int visibleRows = std::max(1, (r.height() - y - 34) / rowH);
    if (selected_ < scrollOffset_) scrollOffset_ = selected_;
    if (selected_ >= scrollOffset_ + visibleRows)
        scrollOffset_ = selected_ - visibleRows + 1;
    int end = std::min((int)settings_.size(), scrollOffset_ + visibleRows);
    hitX_ = labelX;
    hitY_ = y;
    hitW_ = rowW;
    hitH_ = visibleRows * rowH;
    hitRowH_ = rowH;

    for (int i = scrollOffset_; i < end; i++) {
        bool selected = (i == selected_);
        uint32_t bg = selected ? 0x1F3D2BFF : 0x1E2A4799;

        r.fillRect(labelX, y, rowW, rowH - 2, bg);

        // Label
        drawCell(settings_[(size_t)i].label, labelX + 8, y + 6,
                 valueX - labelX - 32, 0xD9C9A3FF);

        // Current value
        std::string valStr;
        const bool disabled = unavailable(settings_[static_cast<size_t>(i)].key);
        if (disabled) {
            valStr = "-";
        } else if (settings_[(size_t)i].key.rfind("__", 0) == 0) {
            // Special submenu entry
            valStr = "open >";
        } else {
            int idx = optionIndex(settings_[(size_t)i].key);
            valStr = settings_[(size_t)i].options[(size_t)idx];
            if (settings_[(size_t)i].key == "EMULATOR_BACKEND")
                valStr = valStr == "opendingux" ? "OpenDingux" : "RetroArch";
        }
        int tw = drawCell(valStr, valueX, y + 6,
                          r.width() - valueX - 40, 0xD9C9A3FF);

        // Arrow indicators (only for normal settings)
        if (selected && !disabled && settings_[(size_t)i].key.rfind("__", 0) != 0) {
            font.render(r, "<", valueX - 16, y + 6, 0xD9C9A3AA);
            font.render(r, ">", std::min(valueX + tw + 8, r.width() - 28), y + 6, 0xD9C9A3AA);
        }

        y += rowH;
    }

    // Footer
    int fy = r.height() - 20;
    r.fillRect(0, fy - 4, r.width(), 24, 0x1E2A47FF);
    drawCell(terminalError_.empty() ? "Up/Down: select   Left/Right: change   B: back" : terminalError_, 12, fy,
             r.width() - 24, 0xD9C9A399);
}

}  // namespace lilsmoke
