// LilSmokeUI — SettingsView.h
//
// Simple settings menu. Lets the user toggle:
//   - WiFi on/off
//   - SSH on/off
//   - Display rotation
//   - Scaling mode (integer / stretch / aspect)
//   - Master volume
//   - CPU governor
//   - CPU overclock profile
//   - Scan ROMs on boot
//   - Default theme
//
// All changes write back to /persist/config/lilsmoke.conf via Config::save().

#pragma once

#include "View.h"
#include "Config.h"
#include "Input.h"
#include "Renderer.h"
#include "Theme.h"
#include "Font.h"
#include "GameHistory.h"
#include "FavoritesStore.h"

#include <vector>
#include <string>
#include <functional>

namespace lilsmoke {

class SettingsView : public View {
public:
    explicit SettingsView(Config &config, FavoritesStore favorites = FavoritesStore());

    void onEnter() override;
    View *handleInput(InputButton btn, InputEvent evt) override;
    View *handlePointer(const PointerEvent &event) override;
    bool allowAutoBackPop() const override { return pendingUsbOption_ < 0 && !pendingPowerOff_; }
    CursorKind cursorAt(int x, int y) const override;
    void render(Renderer &r, Font &font, const Theme &theme) override;

    // Set this to a callback that returns the user back to the system list.
    void setOnBack(std::function<View *()> cb) { onBack_ = std::move(cb); }
    void setOnOpenInput(std::function<View *()> cb) { onOpenInput_ = std::move(cb); }
    void setOnOpenOTA(std::function<View *()> cb) { onOpenOTA_ = std::move(cb); }
    void setOnOpenRemote(std::function<View *()> cb) { onOpenRemote_ = std::move(cb); }
    void setOnPointerChanged(std::function<void(bool, const std::string &)> cb) {
        onPointerChanged_ = std::move(cb);
    }
    void setOnThemeChanged(std::function<void(const std::string &)> cb) {
        onThemeChanged_ = std::move(cb);
    }
    void setOnPresentationChanged(std::function<void()> cb) {
        onPresentationChanged_ = std::move(cb);
    }

    void setOnOpenBluetooth(std::function<View *()> cb) { onOpenBluetooth_ = std::move(cb); }
    void setOnPowerOff(std::function<void()> cb) { onPowerOff_ = std::move(cb); }
    void setOnScanGames(std::function<void()> cb) { onScanGames_ = std::move(cb); }
    void setOnOpenEmulators(std::function<View *()> cb) { onOpenEmulators_ = std::move(cb); }
    void setOnLibraryChanged(std::function<void(bool)> cb) { onLibraryChanged_ = std::move(cb); }

private:
    Config &config_;
    GameHistory history_;
    FavoritesStore favorites_;
    std::function<void(bool)> onLibraryChanged_;
    std::function<View *()> onBack_;
    std::function<View *()> onOpenInput_;
    std::function<View *()> onOpenBluetooth_;
    std::function<View *()> onOpenOTA_;
    std::function<View *()> onOpenRemote_;
    std::function<void()> onPowerOff_;
    std::function<void()> onScanGames_;
    std::function<View *()> onOpenEmulators_;
    std::function<void(bool, const std::string &)> onPointerChanged_;
    std::function<void(const std::string &)> onThemeChanged_;
    std::function<void()> onPresentationChanged_;
    int selected_ = 0;
    int scrollOffset_ = 0;
    int hitX_ = 16;
    int hitY_ = 50;
    int hitW_ = 0;
    int hitH_ = 0;
    int hitRowH_ = 28;
    int pendingUsbOption_ = -1;
    bool pendingPowerOff_ = false;
    bool confirmPowerOff_ = false;
    std::string terminalError_;
    int screenW_ = 640;
    int screenH_ = 480;

    struct Setting {
        std::string key;
        std::string label;
        std::vector<std::string> options;
    };

    std::vector<Setting> settings_;

    int optionIndex(const std::string &key) const;
    void setOption(const std::string &key, int idx, bool confirmed = false);
    void applyRuntimeSetting(const std::string &key) const;
    int pointerSettingIndex(int x, int y) const;
    bool unavailable(const std::string &key) const;
};

}  // namespace lilsmoke
