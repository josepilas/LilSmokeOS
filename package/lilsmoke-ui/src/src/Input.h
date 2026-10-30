// LilSmokeUI — Input.h
//
// Input normally comes from /dev/input/event*. Stock SF3000-family kernels
// can instead expose the controller through cubevol's /tmp/joy_key SysV
// shared-memory bitmask, which is used as a fallback when evdev is absent.

#pragma once

#include "Keymap.h"
#include "gamepad-state.h"

#include <cstdint>
#include <array>
#include <deque>
#include <linux/input.h>
#include <map>
#include <string>
#include <vector>

union SDL_Event;
struct _SDL_GameController;

namespace lilsmoke {

enum class InputButton {
    None = 0,
    Up, Down, Left, Right,
    A, B, X, Y,
    L1, R1, L2, R2, L3, R3,
    Select, Start,
    Home,        // usually the menu combo
    VolUp, VolDown,
    Power,
};

enum class InputEvent {
    None,
    Pressed,
    Released,
    Repeat,
};

enum class PointerButton {
    None = 0,
    Primary,
    Secondary,
    Middle,
};

enum class PointerEventType {
    None = 0,
    Moved,
    Pressed,
    Released,
    Scroll,
};

struct PointerEvent {
    PointerEventType type = PointerEventType::None;
    PointerButton button = PointerButton::None;
    int x = 0;
    int y = 0;
    int dx = 0;
    int dy = 0;
    int wheelX = 0;
    int wheelY = 0;
    bool touchpad = false;
};

struct PointerState {
    int x = 0;
    int y = 0;
    bool visible = false;
    bool primaryDown = false;
    bool secondaryDown = false;
    bool middleDown = false;
    bool touchpad = false;
    bool dragging() const { return primaryDown || secondaryDown || middleDown; }
};

struct InputState {
    bool up = false, down = false, left = false, right = false;
    bool a = false, b = false, x = false, y = false;
    bool l1 = false, r1 = false, l2 = false, r2 = false, l3 = false, r3 = false;
    bool select = false, start = false;
    bool home = false;
};

struct GamepadAnalogState {
    int16_t leftX = 0, leftY = 0, rightX = 0, rightY = 0;
    bool hasLeft = false, hasRight = false;
};

struct RawBinding {
    std::string deviceID, deviceName;
    unsigned type = 0, code = 0;
    int direction = 0;
    bool ready = false, cancelled = false;
};

class Input {
public:
    Input();
    ~Input();

    // Open all /dev/input/event* devices and load keymaps.
    // Returns number of devices opened.
    int init(int screenWidth = 640, int screenHeight = 480);
    // Protocol bridges may publish legacy input themselves; avoid feedback.
    void disconnectLegacySharedInput();

    // Poll for events. Returns true if state changed.
    // `btn` and `evt` receive the most recent event (or None).
    bool poll(InputButton &btn, InputEvent &evt, PointerEvent &pointer);
    void handleSDLEvent(const SDL_Event &event);
    // US physical-keyboard text entry; GPIO/gamepad keymaps remain unchanged.
    void setTextMode(bool enabled) { textMode_ = enabled; }
    void setKeyboardNavigation(bool enabled) { keyboardNavigation_ = enabled; }
    const std::string &text() const { return text_; }

    // Runtime settings can change these without restarting the frontend.
    void setPointerOptions(bool enabled, const std::string &speed);
    void setPointerViewport(int outputWidth, int outputHeight,
                            int insetX, int insetY, int width, int height);

    // Current button state.
    const InputState &state() const { return state_; }
    InputState gameplayState() const;
    GamepadAnalogState analogState() const;
    const PointerState &pointerState() const { return pointerState_; }
    void setBindingCapture(bool enabled) { capture_ = enabled; rawBinding_ = {}; }
    const RawBinding &rawBinding() const { return rawBinding_; }
    bool bindCaptured(InputButton action);
    const std::string &bindingStatus() const { return bindingStatus_; }
    std::string bindingLabel(InputButton action) const;

    // Access the loaded keymap (for the settings UI to display/edit).
    Keymap &keymap() { return keymap_; }
    const Keymap &keymap() const { return keymap_; }

private:
    friend struct InputTestAccess;
    struct Device {
        int fd = -1;
        std::string path;
        std::string id, name;
        bool gamepad = false, profileReady = false;
        _SDL_GameController *sdlController = nullptr;
        uint64_t usbGeneration = 0;
        uint32_t usbInstance = 0, usbPid = 0;
        uint16_t usbVendor = 0, usbProduct = 0, usbVersion = 0;
        std::map<int, InputButton> held, overrides;
        struct Axis { int minimum = -32768, maximum = 32767, flat = 0, value = 0; };
        std::map<unsigned, Axis> axes;
        bool relativePointer = false;
        bool keyboard = false;
        bool leftShift = false;
        bool rightShift = false;
        bool capsLock = false;
        bool dropped = false;
        std::array<input_event, 64> events{};
        size_t nextEvent = 0, eventCount = 0;
        std::vector<PointerEvent> pointerButtons;
        bool absolutePointer = false;
        bool directPointer = false;
        bool usesMultitouch = false;
        bool hasTouchSignal = false;
        bool touchActive = false;
        int currentSlot = 0;
        int absX = 0;
        int absY = 0;
        int absMinX = 0;
        int absMaxX = 1;
        int absMinY = 0;
        int absMaxY = 1;
        int lastAbsX = 0;
        int lastAbsY = 0;
        bool lastAbsValid = false;
        bool absDirty = false;
        int relativeX = 0;
        int relativeY = 0;
        int wheelX = 0;
        int wheelY = 0;
        int remainderX = 0;
        int remainderY = 0;
        int hatX = 0;
        int hatY = 0;
    };

    struct QueuedEvent {
        InputButton button = InputButton::None;
        InputEvent inputEvent = InputEvent::None;
        PointerEvent pointer;
    };

    std::vector<Device> devices_;
    std::deque<QueuedEvent> queued_;
    bool textMode_ = false;
    bool keyboardNavigation_ = true;
    std::string text_;
    bool capture_ = false;
    RawBinding rawBinding_;
    std::string bindingStatus_;
    std::string lastMappedDevice_;
    std::string gamepadDir_ = "/persist/config/gamepads";
    ls_gp_reader usbReader_ = LS_GP_READER_INIT;
    uint64_t lastUsbSnapshotMs_=0,lastUsbGeneration_=0;
    std::string autoconfigDir_ = "/persist/config/retroarch-autoconfig";
    InputState state_;
    PointerState pointerState_;
    Keymap keymap_;
    std::string inputDir_ = "/dev/input";
    int screenWidth_ = 640;
    int screenHeight_ = 480;
    int pointerOutputWidth_ = 640, pointerOutputHeight_ = 480;
    int pointerInsetX_ = 0, pointerInsetY_ = 0;
    bool pointerEnabled_ = true;
    int pointerSpeedNumerator_ = 1;
    int pointerSpeedDenominator_ = 1;
    uint64_t lastDeviceScanMs_ = 0;
    int sharedMemoryId_ = -1;
    volatile uint32_t *sharedKeys_ = nullptr;
    uint32_t sharedLastRaw_ = 0;
    uint32_t sharedPendingChanges_ = 0;

    int scanDevices();
    void openSDLController(int index);
    void pollUsbGamepads();
    void configureDevice(Device &device);
    void queueButton(InputButton button, InputEvent event);
    void refreshState();
    void releaseButtons(Device &device);
    void sourceButton(Device &device, int source, InputButton button, InputEvent event);
    InputButton deviceButton(const Device &device, unsigned code) const;
    void queueAxis(Device &device, unsigned code, int value);
    bool captureBinding(Device &device, unsigned type, unsigned code, int direction);
    void loadGamepadMap(Device &device);
    bool exportGamepadProfile(Device &device);
    std::string gamepadProfile(const Device &device, const std::string &name,
                              const std::vector<unsigned> &axes,
                              const std::vector<unsigned> &buttons) const;
    void queueHat(Device &device, bool horizontal, int value);
    bool flushPointerPacket(Device &device, PointerEvent &event);
    bool popQueued(InputButton &button, InputEvent &event,
                   PointerEvent &pointer);
};

}  // namespace lilsmoke
