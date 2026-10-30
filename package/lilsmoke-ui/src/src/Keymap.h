// LilSmokeUI — Keymap.h
//
// Loads keymap files from /etc/lilsmoke/input-maps/*.map and
// /persist/config/input-maps/*.map (user overrides).
//
// File format (one binding per line):
//   <device> <code> <action>
//
// Where:
//   <device> = "evdev" | "js"
//   <code>   = decimal integer (evdev keycode or js button index)
//   <action> = up | down | left | right | a | b | x | y |
//              l1 | r1 | l2 | r2 | select | start | home |
//              vol_up | vol_down | power | menu
//
// Comments start with #. Blank lines are ignored.
//
// Example:
//   # SF3000/GB350 gamepad
//   evdev 103 up       # KEY_UP
//   evdev 108 down     # KEY_DOWN
//   evdev 105 left     # KEY_LEFT
//   evdev 106 right    # KEY_RIGHT
//   evdev 28  start    # KEY_ENTER
//   evdev 1   select   # KEY_ESC
//   evdev 57  a        # KEY_SPACE
//   evdev 42  b        # KEY_G
//   ...
//
// Multiple maps can coexist (e.g., default.map + user-override.map).
// Later maps override earlier ones for the same (device, code) pair.

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace lilsmoke {

enum class InputButton;

class Keymap {
public:
    Keymap();

    // Load all .map files from the given directory (sorted alphabetically).
    // Returns the number of bindings loaded.
    int loadDir(const std::string &dir);

    // Load a single .map file. Returns the number of bindings loaded
    // (or -1 on error).
    int loadFile(const std::string &path);

    // Look up the button for an evdev keycode.
    // Returns InputButton::None if no binding.
    InputButton lookup(int evdevCode) const;

    // Total bindings loaded.
    int size() const { return (int)bindings_.size(); }

    // Get all bindings (for the settings UI to display / edit).
    struct Binding {
        int code;
        InputButton action;
        std::string sourceFile;  // which .map file defined this
    };
    const std::vector<Binding> &bindings() const { return bindings_; }

    // Add or override a binding (in-memory only; call save() to persist).
    void set(int code, InputButton action);

    // Save the user's overrides to /persist/config/input-maps/user.map.
    bool saveUserMap(const std::string &path) const;

    static InputButton parseAction(const std::string &s);
    static const char *actionName(InputButton b);

private:
    std::vector<Binding> bindings_;

};

}  // namespace lilsmoke
