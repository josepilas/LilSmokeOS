// LilSmokeUI — View.h
//
// Base class for UI views. A view is a screen the user can interact with:
//   - SystemList: pick which system (NES, SNES, PS1, ...)
//   - GameList:   pick which game within a system
//   - Settings:   tweak lilsmoke.conf

#pragma once

#include "Input.h"
#include "Cursor.h"
#include "Renderer.h"
#include "Theme.h"
#include "Font.h"
#include "ViewActivity.h"

#include <vector>
#include <string>

namespace lilsmoke {

class View {
public:
    virtual ~View() = default;

    // Unknown views retain the old every-frame behavior, including render side effects.
    virtual ViewActivity frameActivity(uint32_t, const Theme &) const { return {}; }
    void invalidateVisual() { ++visualRevision_; }
    uint64_t visualRevision() const { return visualRevision_; }

    // Called when the view becomes active.
    virtual void onEnter() {}

    // Called when the view is about to be left.
    virtual void onLeave() {}

    // Handle an input event. Returns the next view to switch to (or this).
    virtual View *handleInput(InputButton btn, InputEvent evt) = 0;
    virtual bool acceptsText() const { return false; }
    virtual void handleText(const std::string &) {}
    virtual std::string textTitle() const { return "Text"; }
    virtual std::string textValue() const { return {}; }
    virtual bool textNumeric() const { return false; }
    virtual bool textSecret() const { return false; }

    // Pointer defaults make every existing view usable with a mouse: wheel
    // navigates, primary activates, secondary goes back, and middle opens
    // the menu. Views with rows/tiles override this to add hover hit-testing.
    virtual View *handlePointer(const PointerEvent &event) {
        if (event.type == PointerEventType::Scroll) {
            if (event.wheelY > 0)
                return handleInput(InputButton::Up, InputEvent::Repeat);
            if (event.wheelY < 0)
                return handleInput(InputButton::Down, InputEvent::Repeat);
            if (event.wheelX > 0)
                return handleInput(InputButton::Right, InputEvent::Repeat);
            if (event.wheelX < 0)
                return handleInput(InputButton::Left, InputEvent::Repeat);
        }
        if (event.type != PointerEventType::Pressed) return nullptr;
        if (event.button == PointerButton::Primary)
            return handleInput(InputButton::A, InputEvent::Pressed);
        if (event.button == PointerButton::Secondary)
            return handleInput(InputButton::B, InputEvent::Pressed);
        if (event.button == PointerButton::Middle)
            return handleInput(InputButton::Home, InputEvent::Pressed);
        return nullptr;
    }

    virtual CursorKind cursorAt(int, int) const { return CursorKind::Normal; }

    // The app shell pops non-root views on B by default. Views with their own
    // back behavior, such as the file explorer, can opt out.
    virtual bool allowAutoBackPop() const { return true; }

    // Render the view. Called every frame (~60 FPS).
    virtual void render(Renderer &r, Font &font, const Theme &theme) = 0;
private:
    uint64_t visualRevision_ = 0;
};

}  // namespace lilsmoke
