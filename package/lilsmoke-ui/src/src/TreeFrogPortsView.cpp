// SPDX-License-Identifier: MPL-2.0
#include "TreeFrogPortsView.h"
#include "ProcessLaunch.h"
#include "Log.h"
#include <unistd.h>

namespace lilsmoke {
namespace {
class TreeFrogNotice : public View {
public:
    TreeFrogNotice(std::string title, std::string message)
        : title_(std::move(title)), message_(std::move(message)) {}
    View *handleInput(InputButton, InputEvent) override { return nullptr; }
    void render(Renderer &r, Font &font, const Theme &) override {
        r.clear(0x2B2D31FF);
        font.renderStyled(r, title_, 16, 12, r.width() - 32, 48,
                          0xFFFFFFFF, "", 18, "left", "top", true);
        font.renderStyled(r, message_, 16, 72, r.width() - 32, r.height() - 88,
                          0xFFFFFFFF, "", 16, "left", "top", true);
    }
private:
    std::string title_, message_;
};
} // namespace

void TreeFrogPortsView::onEnter() {
    ports_ = TreeFrogCatalog::scan(root_);
    LOG_INFO("TreeFrog Ports: catalogue ready entries=%zu root=%s", ports_.size(), root_.c_str());
    std::vector<std::string> labels;
    for (const auto &port : ports_)
        labels.push_back(port.name + (port.available() ? "" : " (missing files)"));
    if (labels.empty()) labels.push_back("No ports in LILROMS/treefrog-ports");
    list_ = std::make_unique<ListView>(std::move(labels),
        [this](int index, const std::string &) { return select(index); }, "TreeFrog Ports", false);
    list_->onEnter();
    invalidateVisual();
}

View *TreeFrogPortsView::select(int index) {
    if (index < 0 || static_cast<size_t>(index) >= ports_.size()) return nullptr;
    const auto &selected = ports_[index];
    // Re-read this ID, not an index into a possibly changed directory listing.
    const auto port = TreeFrogCatalog::find(selected.id, root_);
    if (!port) return new TreeFrogNotice(selected.name, "Port metadata is missing or invalid");
    if (!port->available()) return new TreeFrogNotice(port->name, port->reason +
        (port->missingData.empty() ? "" : "\n\n" + port->missingData));
    const char *helper = "/usr/lib/lilsmoke/lilsmoke-treefrog-launch";
    if (access(helper, X_OK)) return new TreeFrogNotice(port->name, "TreeFrog launch helper unavailable");
    // The helper owns the original run.sh and runtime policy. No metadata is
    // interpreted as a command, nor registered as an emulator or recent ROM.
    LOG_INFO("TreeFrog Ports: handoff id=%s type=%s", port->id.c_str(), port->launchType.c_str());
    requestProcessLaunch({helper, {"lilsmoke-treefrog-launch", port->id}});
    return nullptr;
}

View *TreeFrogPortsView::handleInput(InputButton button, InputEvent event) {
    return list_ ? list_->handleInput(button, event) : nullptr;
}
View *TreeFrogPortsView::handlePointer(const PointerEvent &event) {
    return list_ ? list_->handlePointer(event) : nullptr;
}
CursorKind TreeFrogPortsView::cursorAt(int x, int y) const {
    return list_ ? list_->cursorAt(x, y) : CursorKind::Normal;
}
void TreeFrogPortsView::render(Renderer &r, Font &font, const Theme &theme) {
    if (list_) list_->render(r, font, theme);
}
} // namespace lilsmoke
