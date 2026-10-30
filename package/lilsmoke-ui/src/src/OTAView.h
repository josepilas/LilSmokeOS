// LilSmokeUI — OTAView.h
//
// UI for the lilsmoke-update OTA updater. Shows:
//   - Current version + active slot
//   - Latest available version (if WiFi is on)
//   - Buttons: Check / Download / Install / Activate / Reboot
//
// All actions shell out to /usr/lib/lilsmoke/lilsmoke-update.

#pragma once

#include "View.h"
#include "Config.h"
#include "Input.h"
#include "Renderer.h"
#include "Theme.h"
#include "Font.h"

#include <string>
#include <vector>
#include <functional>

namespace lilsmoke {

class OTAView : public View {
public:
    explicit OTAView(Config &config);

    void onEnter() override;
    View *handleInput(InputButton btn, InputEvent evt) override;
    void render(Renderer &r, Font &font, const Theme &theme) override;

    void setOnBack(std::function<View *()> cb) { onBack_ = std::move(cb); }

private:
    Config &config_;
    std::function<View *()> onBack_;

    int selected_ = 0;

    // Cached status info (refreshed on entry and after each action)
    std::string currentVersion_;
    std::string activeSlot_;
    std::string inactiveSlot_;
    std::string bootCounter_;
    std::string slotAStatus_;
    std::string slotBStatus_;
    std::string latestVersion_;     // empty if not checked
    std::string lastMessage_;       // status message from last action

    void refreshStatus();
    bool runUpdateCmd(const std::string &subcmd);
};

}  // namespace lilsmoke
