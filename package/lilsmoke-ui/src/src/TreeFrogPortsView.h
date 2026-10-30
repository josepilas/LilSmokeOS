// SPDX-License-Identifier: MPL-2.0
#pragma once

#include "ListView.h"
#include "TreeFrogCatalog.h"
#include <memory>
#include <utility>

namespace lilsmoke {
class TreeFrogPortsView : public View {
public:
    explicit TreeFrogPortsView(std::string root = TreeFrogCatalog::DefaultRoot)
        : root_(std::move(root)) {}
    void onEnter() override;
    View *handleInput(InputButton button, InputEvent event) override;
    View *handlePointer(const PointerEvent &event) override;
    CursorKind cursorAt(int x, int y) const override;
    void render(Renderer &renderer, Font &font, const Theme &theme) override;

private:
    std::string root_;
    std::vector<TreeFrogPort> ports_;
    std::unique_ptr<ListView> list_;
    View *select(int index);
};
} // namespace lilsmoke
