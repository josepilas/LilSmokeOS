#include "RemoteAccessView.h"

#include "Font.h"
#include "Renderer.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace lilsmoke {

void RemoteAccessView::refresh() {
    lines_.clear();
    lines_.push_back("SSH user: lil");
    lines_.push_back(config_.getBool("SSH_ENABLED", false) ? "SSH: enabled" : "SSH: disabled");

    bool foundAddress = false;
    FILE *pipe = popen("ip -4 -o addr show scope global 2>/dev/null", "r");
    if (pipe) {
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe)) {
            std::istringstream input(buffer);
            std::string number, interface, family, address;
            if (!(input >> number >> interface >> family >> address) ||
                family != "inet" || interface == "lo") continue;
            size_t slash = address.find('/');
            if (slash != std::string::npos) address.erase(slash);
            lines_.push_back(interface + ": " + address);
            lines_.push_back("ssh lil@" + address);
            foundAddress = true;
        }
        pclose(pipe);
    }
    if (!foundAddress) lines_.push_back("No active IPv4 connection");

    std::ifstream status("/run/lilsmoke-usb/status");
    std::string state = "inactive";
    std::string mode = config_.get("USB_MODE", "off");
    std::string detail;
    std::string line;
    while (std::getline(status, line)) {
        size_t separator = line.find('=');
        if (separator == std::string::npos) continue;
        std::string key = line.substr(0, separator);
        std::string value = line.substr(separator + 1);
        if (key == "state") state = value;
        else if (key == "mode") mode = value;
        else if (key == "detail") detail = value;
    }
    lines_.push_back("");
    lines_.push_back("USB: " + state + " (" + mode + ")");
    if (!detail.empty()) lines_.push_back(detail);
    if (state == "active" && (mode == "network" || mode == "network+drive")) {
        lines_.push_back("ssh lil@" +
            config_.get("USB_NETWORK_ADDRESS", "192.168.7.1"));
    }
    lines_.push_back("LILDRIVE: LILROMS partition (/storage)");
    lines_.push_back("");
    lines_.push_back("Initial password: lilsmoke");
    lines_.push_back("Change it after login with: passwd");
}

void RemoteAccessView::onEnter() {
    refresh();
}

View *RemoteAccessView::handleInput(InputButton button, InputEvent event) {
    if (event != InputEvent::Pressed && event != InputEvent::Repeat) return nullptr;
    if (button == InputButton::Up) scroll_ = std::max(0, scroll_ - 1);
    if (button == InputButton::Down)
        scroll_ = std::min(std::max(0, (int)lines_.size() - 1), scroll_ + 1);
    if (button == InputButton::A || button == InputButton::Start) {
        refresh();
        return nullptr;
    }
    if (button == InputButton::B && onBack_) return onBack_();
    return nullptr;
}

void RemoteAccessView::render(Renderer &renderer, Font &font, const Theme &) {
    renderer.clear(0x20242AFF);
    renderer.fillRect(0, 0, renderer.width(), 38, 0x183047FF);
    font.render(renderer, "Remote access", 12, 9, 0xF2E8C9FF);

    int y = 56;
    for (size_t index = static_cast<size_t>(scroll_); index < lines_.size(); ++index) {
        const std::string &line = lines_[index];
        if (y > renderer.height() - 42) break;
        uint32_t color = line.rfind("ssh ", 0) == 0
            ? 0x8FD7A5FF : 0xC9D7E8FF;
        font.renderStyled(renderer, line, 18, y, renderer.width() - 36, 22, color,
                          "", 16);
        y += 24;
    }

    renderer.fillRect(0, renderer.height() - 26,
                      renderer.width(), 26, 0x183047FF);
    font.render(renderer, "A refresh    B back", 12,
                renderer.height() - 20, 0xC9D7E8FF);
}

}  // namespace lilsmoke
