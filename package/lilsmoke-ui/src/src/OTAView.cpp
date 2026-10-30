// LilSmokeUI — OTAView.cpp

#include "OTAView.h"
#include "Log.h"

#include <cstdio>
#include <array>

namespace lilsmoke {

OTAView::OTAView(Config &config) : config_(config) {}

void OTAView::onEnter() {
    selected_ = 0;
    refreshStatus();
}

static std::string readCmdOutput(const std::string &cmd) {
    std::array<char, 256> buf;
    std::string result;
    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
        result += buf.data();
    }
    pclose(pipe);
    return result;
}

static std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

void OTAView::refreshStatus() {
    // Read version from /etc/lilsmoke-version.txt
    FILE *vf = fopen("/etc/lilsmoke-version.txt", "r");
    if (vf) {
        char buf[128];
        if (fgets(buf, sizeof(buf), vf)) {
            currentVersion_ = trim(buf);
        }
        fclose(vf);
    }

    // Read slot info from /persist/
    activeSlot_ = trim(readCmdOutput("cat /persist/active_slot 2>/dev/null"));
    if (activeSlot_.empty()) activeSlot_ = "A";
    inactiveSlot_ = (activeSlot_ == "A") ? "B" : "A";

    bootCounter_ = trim(readCmdOutput("cat /persist/bootcounter 2>/dev/null"));
    if (bootCounter_.empty()) bootCounter_ = "0";

    slotAStatus_ = trim(readCmdOutput("cat /persist/slot_A_status 2>/dev/null"));
    if (slotAStatus_.empty()) slotAStatus_ = "unknown";
    slotBStatus_ = trim(readCmdOutput("cat /persist/slot_B_status 2>/dev/null"));
    if (slotBStatus_.empty()) slotBStatus_ = "unknown";

    LOG_INFO("OTAView: status refreshed — v%s slot %s counter %s",
             currentVersion_.c_str(), activeSlot_.c_str(), bootCounter_.c_str());
}

bool OTAView::runUpdateCmd(const std::string &subcmd) {
    std::string cmd = "/usr/lib/lilsmoke/lilsmoke-update " + subcmd + " 2>&1";
    LOG_INFO("OTAView: running %s", cmd.c_str());
    std::string output = readCmdOutput(cmd);
    lastMessage_ = trim(output);
    if (lastMessage_.size() > 200) {
        lastMessage_ = lastMessage_.substr(0, 200) + "...";
    }
    refreshStatus();

    // For check: extract latest version from output
    if (subcmd == "check") {
        // Look for "Latest version:  X"
        size_t pos = output.find("Latest version:");
        if (pos != std::string::npos) {
            size_t eol = output.find('\n', pos);
            latestVersion_ = trim(output.substr(pos + 15, eol - pos - 15));
        }
    }
    return true;
}

View *OTAView::handleInput(InputButton btn, InputEvent evt) {
    if (evt != InputEvent::Pressed) return nullptr;

    // 5 menu items: Check / Download / Install / Activate / Reboot
    switch (btn) {
        case InputButton::Up:
            if (selected_ > 0) selected_--;
            return nullptr;
        case InputButton::Down:
            if (selected_ < 4) selected_++;
            return nullptr;
        case InputButton::A:
        case InputButton::Start: {
            switch (selected_) {
                case 0: runUpdateCmd("check");    break;
                case 1: runUpdateCmd("download"); break;
                case 2: runUpdateCmd("install");  break;
                case 3: runUpdateCmd("activate"); break;
                case 4: runUpdateCmd("reboot");   break;
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

void OTAView::render(Renderer &r, Font &font, const Theme &theme) {
    r.clear(0x2B2D31FF);

    // Title
    r.fillRect(0, 0, r.width(), 36, 0x1E2A47FF);
    font.render(r, "OTA Update", 12, 8, 0xD9C9A3FF);

    int y = 50;

    // Status block
    font.render(r, "Current version: " + currentVersion_, 16, y, 0xD9C9A3FF); y += 20;
    if (!latestVersion_.empty()) {
        uint32_t color = (latestVersion_ == currentVersion_) ? 0x99FF99FF : 0xFFAA66FF;
        font.render(r, "Latest version:  " + latestVersion_, 16, y, color); y += 20;
    }
    font.render(r, "Active slot: " + activeSlot_ +
                "    Inactive: " + inactiveSlot_, 16, y, 0xD9C9A3FF); y += 20;
    font.render(r, "Boot counter: " + bootCounter_, 16, y, 0xD9C9A3FF); y += 20;
    font.render(r, "Slot A: " + slotAStatus_ +
                "    Slot B: " + slotBStatus_, 16, y, 0xD9C9A3FF); y += 28;

    // Menu items
    const char *items[] = {
        "Check for updates",
        "Download bundle",
        "Install to inactive slot",
        "Activate for next boot",
        "Reboot",
    };
    int itemH = 28;
    for (int i = 0; i < 5; i++) {
        bool selected = (i == selected_);
        uint32_t bg = selected ? 0x1F3D2BFF : 0x1E2A4799;
        r.fillRect(16, y, r.width() - 32, itemH - 2, bg);
        font.render(r, items[i], 24, y + 5, selected ? 0xD9C9A3FF : 0xD9C9A3CC);
        y += itemH;
    }

    y += 8;

    // Last message (output of last action)
    if (!lastMessage_.empty()) {
        font.render(r, "Last action output:", 16, y, 0xD9C9A399); y += 18;
        // Wrap text to fit width
        std::string remaining = lastMessage_;
        while (!remaining.empty() && y < r.height() - 30) {
            size_t cut = remaining.find('\n');
            std::string line = (cut != std::string::npos) ? remaining.substr(0, cut)
                                                           : remaining;
            if (cut != std::string::npos) remaining = remaining.substr(cut + 1);
            else remaining.clear();
            // Truncate long lines
            int tw = 0, th = 0;
            font.textSize(line, tw, th);
            if (tw > r.width() - 32) {
                while (!line.empty() && tw > r.width() - 40) {
                    line.pop_back();
                    font.textSize(line + "...", tw, th);
                }
                line += "...";
            }
            font.render(r, line, 16, y, 0xD9C9A3CC);
            y += 16;
        }
    }

    // Footer
    int fy = r.height() - 20;
    r.fillRect(0, fy - 4, r.width(), 24, 0x1E2A47FF);
    font.render(r, "Up/Down: select   A: run   B: back", 12, fy, 0xD9C9A399);
}

}  // namespace lilsmoke
