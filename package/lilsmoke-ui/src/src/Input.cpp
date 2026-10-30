// LilSmokeUI — Input.cpp
//
// Polls /dev/input/event* directly using the target ABI's input_event size:
//   struct input_event { struct timeval time; __u16 type; __u16 code; __s32 value; };
//
// type == EV_KEY (1) for button events. value == 1 (press), 0 (release), 2 (repeat).
//
// Key codes are mapped to internal button enum via the Keymap class,
// which loads /etc/lilsmoke/input-maps/*.map (built-in) and
// /persist/config/input-maps/*.map (user overrides) at init time.

#include "Input.h"
#include "Log.h"
#include <SDL2/SDL.h>

#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/joystick.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <time.h>
#include <unistd.h>
#include "gamepad-state.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace lilsmoke {

Input::Input() = default;
void Input::disconnectLegacySharedInput() {
    if (sharedKeys_) shmdt(const_cast<uint32_t *>(sharedKeys_));
    sharedKeys_ = nullptr;
    sharedMemoryId_ = -1;
    sharedLastRaw_ = 0;
}
Input::~Input() {
    for (const Device &device : devices_) {
        if (device.fd >= 0) close(device.fd);
        if (device.sdlController) SDL_GameControllerClose(device.sdlController);
    }
    ls_gp_close(&usbReader_);
    if (sharedKeys_) shmdt(const_cast<uint32_t *>(sharedKeys_));
}

namespace {

static uint64_t monotonicMilliseconds() {
    struct timespec now = {};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<uint64_t>(now.tv_sec) * 1000ULL +
           static_cast<uint64_t>(now.tv_nsec / 1000000L);
}

static bool testBit(const std::vector<unsigned long> &bits, int bit) {
    const int perWord = static_cast<int>(sizeof(unsigned long) * 8U);
    const size_t word = static_cast<size_t>(bit / perWord);
    return word < bits.size() &&
           (bits[word] & (1UL << static_cast<unsigned>(bit % perWord))) != 0;
}

static std::vector<unsigned long> capabilityBits(int fd, int eventType,
                                                  int maximum) {
    const size_t words = static_cast<size_t>(maximum) /
                         (sizeof(unsigned long) * 8U) + 1U;
    std::vector<unsigned long> bits(words, 0);
    if (ioctl(fd, EVIOCGBIT(eventType,
                           static_cast<int>(bits.size() * sizeof(unsigned long))),
              bits.data()) < 0) {
        std::fill(bits.begin(), bits.end(), 0);
    }
    return bits;
}

static std::vector<unsigned long> propertyBits(int fd) {
    const size_t words = static_cast<size_t>(INPUT_PROP_MAX) /
                         (sizeof(unsigned long) * 8U) + 1U;
    std::vector<unsigned long> bits(words, 0);
    if (ioctl(fd, EVIOCGPROP(
                      static_cast<int>(bits.size() * sizeof(unsigned long))),
              bits.data()) < 0) {
        std::fill(bits.begin(), bits.end(), 0);
    }
    return bits;
}

static int clampCoordinate(int value, int maximum) {
    return std::max(0, std::min(value, std::max(0, maximum - 1)));
}

static PointerButton pointerButton(unsigned code) {
    if (code == BTN_LEFT) return PointerButton::Primary;
    if (code == BTN_RIGHT) return PointerButton::Secondary;
    if (code == BTN_MIDDLE) return PointerButton::Middle;
    return PointerButton::None;
}

static InputButton hatButton(bool horizontal, int value) {
    if (horizontal) {
        if (value < 0) return InputButton::Left;
        if (value > 0) return InputButton::Right;
    } else {
        if (value < 0) return InputButton::Up;
        if (value > 0) return InputButton::Down;
    }
    return InputButton::None;
}

static char keyboardCharacter(unsigned code, bool shift, bool caps) {
    const char *rows[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
    const unsigned starts[] = {KEY_Q, KEY_A, KEY_Z};
    for (unsigned i = 0; i < 3; ++i) {
        const size_t length = std::strlen(rows[i]);
        if (code >= starts[i] && code < starts[i] + length) {
            char c = rows[i][code - starts[i]];
            return shift != caps ? static_cast<char>(c - 'a' + 'A') : c;
        }
    }
    if (code >= KEY_1 && code <= KEY_0)
        return (shift ? "!@#$%^&*()" : "1234567890")[code - KEY_1];
    switch (code) {
        case KEY_SPACE: return ' ';
        case KEY_MINUS: return shift ? '_' : '-';
        case KEY_EQUAL: return shift ? '+' : '=';
        case KEY_LEFTBRACE: return shift ? '{' : '[';
        case KEY_RIGHTBRACE: return shift ? '}' : ']';
        case KEY_SEMICOLON: return shift ? ':' : ';';
        case KEY_APOSTROPHE: return shift ? '"' : '\'';
        case KEY_GRAVE: return shift ? '~' : '`';
        case KEY_BACKSLASH: return shift ? '|' : '\\';
        case KEY_COMMA: return shift ? '<' : ',';
        case KEY_DOT: return shift ? '>' : '.';
        case KEY_SLASH: return shift ? '?' : '/';
        case KEY_BACKSPACE: return '\b';
        case KEY_DELETE: return 127;
        default: return 0;
    }
}

}  // namespace

static void updateButtonState(InputState &state, InputButton button, bool pressed) {
    switch (button) {
        case InputButton::Up:     state.up = pressed; break;
        case InputButton::Down:   state.down = pressed; break;
        case InputButton::Left:   state.left = pressed; break;
        case InputButton::Right:  state.right = pressed; break;
        case InputButton::A:      state.a = pressed; break;
        case InputButton::B:      state.b = pressed; break;
        case InputButton::X:      state.x = pressed; break;
        case InputButton::Y:      state.y = pressed; break;
        case InputButton::L1:     state.l1 = pressed; break;
        case InputButton::R1:     state.r1 = pressed; break;
        case InputButton::L2:     state.l2 = pressed; break;
        case InputButton::R2:     state.r2 = pressed; break;
        case InputButton::L3:     state.l3 = pressed; break;
        case InputButton::R3:     state.r3 = pressed; break;
        case InputButton::Select: state.select = pressed; break;
        case InputButton::Start:  state.start = pressed; break;
        case InputButton::Home:   state.home = pressed; break;
        default: break;
    }
}

static InputButton sharedBitButton(unsigned bit) {
    switch (bit) {
        case 0:  return InputButton::Select;
        case 3:  return InputButton::Start;
        case 4:  return InputButton::Up;
        case 5:  return InputButton::Right;
        case 6:  return InputButton::Down;
        case 7:  return InputButton::Left;
        case 10: return InputButton::L1;
        case 11: return InputButton::R1;
        case 12: return InputButton::X;
        case 13: return InputButton::A;
        case 14: return InputButton::B;
        case 15: return InputButton::Y;
        default: return InputButton::None;
    }
}

void Input::setPointerOptions(bool enabled, const std::string &speed) {
    pointerEnabled_ = enabled;
    pointerSpeedNumerator_ = 1;
    pointerSpeedDenominator_ = 1;
    if (speed == "slow") {
        pointerSpeedDenominator_ = 2;
    } else if (speed == "fast") {
        pointerSpeedNumerator_ = 2;
    }
    if (!pointerEnabled_) {
        pointerState_.visible = false;
        pointerState_.primaryDown = false;
        pointerState_.secondaryDown = pointerState_.middleDown = false;
    }
}

void Input::configureDevice(Device &device) {
    const auto eventBits = capabilityBits(device.fd, 0, EV_MAX);
    const auto relativeBits = capabilityBits(device.fd, EV_REL, REL_MAX);
    const auto absoluteBits = capabilityBits(device.fd, EV_ABS, ABS_MAX);
    const auto keyBits = capabilityBits(device.fd, EV_KEY, KEY_MAX);
    const auto properties = propertyBits(device.fd);
    device.keyboard = testBit(keyBits, KEY_A) && testBit(keyBits, KEY_Z) &&
                      testBit(keyBits, KEY_ENTER) && testBit(keyBits, KEY_SPACE);

    device.relativePointer = testBit(eventBits, EV_REL) &&
        testBit(relativeBits, REL_X) && testBit(relativeBits, REL_Y);
    device.usesMultitouch = testBit(absoluteBits, ABS_MT_POSITION_X) &&
                            testBit(absoluteBits, ABS_MT_POSITION_Y);
    const bool pointerProperty = testBit(properties, INPUT_PROP_POINTER) ||
                                 testBit(properties, INPUT_PROP_BUTTONPAD);
    device.directPointer = testBit(properties, INPUT_PROP_DIRECT);
    device.hasTouchSignal = testBit(keyBits, BTN_TOUCH) ||
                            testBit(absoluteBits, ABS_MT_TRACKING_ID);
    const bool hasPadButtons = testBit(keyBits, BTN_GAMEPAD) || testBit(keyBits, BTN_JOYSTICK) ||
                               testBit(keyBits, BTN_0) || testBit(keyBits, BTN_DPAD_UP);
    const bool singleTouch = !hasPadButtons && testBit(absoluteBits, ABS_X) &&
                             testBit(absoluteBits, ABS_Y) &&
                             (pointerProperty || device.directPointer ||
                              device.hasTouchSignal);
    device.absolutePointer = device.usesMultitouch || singleTouch;
    device.gamepad = !device.keyboard && hasPadButtons;
    if (device.gamepad) {
        for (unsigned code = 0; code <= ABS_HAT3Y; ++code) {
            if (!testBit(absoluteBits, code)) continue;
            input_absinfo info{};
            if (ioctl(device.fd, EVIOCGABS(code), &info) == 0 && info.maximum > info.minimum)
                device.axes[code] = {info.minimum, info.maximum, info.flat, info.value};
        }
    }
    device.touchActive = !device.hasTouchSignal;

    const int xCode = device.usesMultitouch ? ABS_MT_POSITION_X : ABS_X;
    const int yCode = device.usesMultitouch ? ABS_MT_POSITION_Y : ABS_Y;
    struct input_absinfo axis = {};
    if (device.absolutePointer && ioctl(device.fd, EVIOCGABS(xCode), &axis) == 0) {
        device.absMinX = axis.minimum;
        device.absMaxX = std::max(axis.minimum + 1, axis.maximum);
        device.absX = axis.value;
    }
    if (device.absolutePointer && ioctl(device.fd, EVIOCGABS(yCode), &axis) == 0) {
        device.absMinY = axis.minimum;
        device.absMaxY = std::max(axis.minimum + 1, axis.maximum);
        device.absY = axis.value;
    }

    char name[128] = {};
    if (ioctl(device.fd, EVIOCGNAME(sizeof(name)), name) < 0)
        std::strncpy(name, "unknown", sizeof(name) - 1);
    device.name = name;
    input_id identity{};
    ioctl(device.fd, EVIOCGID, &identity);
    device.usbVendor = identity.vendor;
    device.usbProduct = identity.product;
    uint32_t signature = 2166136261U;
    for (unsigned char c : device.name) signature = (signature ^ c) * 16777619U;
    for (unsigned code = 0; code <= KEY_MAX; ++code)
        if (testBit(keyBits, code)) signature = (signature ^ code) * 16777619U;
    for (const auto &axis : device.axes) signature = (signature ^ axis.first) * 16777619U;
    char id[80];
    std::snprintf(id, sizeof id, "%04x-%04x-%04x-%04x-%08x",
                  identity.bustype, identity.vendor, identity.product, identity.version, signature);
    device.id = id;
    if (device.gamepad) {
        loadGamepadMap(device);
        device.profileReady = exportGamepadProfile(device);
    }
    LOG_INFO("Input: %s (%s%s%s)", name,
             device.relativePointer ? "mouse" : "keys",
             device.absolutePointer ? (device.directPointer ? ", touchscreen" : ", touchpad") : "",
             device.usesMultitouch ? ", multitouch" : "");
}

int Input::scanDevices() {
    int added = 0;
    DIR *directory = opendir(inputDir_.c_str());
    if (!directory) return 0;

    struct dirent *entry;
    while ((entry = readdir(directory)) != nullptr) {
        if (std::strncmp(entry->d_name, "event", 5) != 0) continue;
        std::string path = inputDir_ + "/" + entry->d_name;
        bool known = false;
        for (Device &device : devices_) {
            if (device.path == path) {
                if (device.gamepad && !device.profileReady) {
                    // joydev can appear shortly after the evdev node.
                    device.profileReady = exportGamepadProfile(device);
                }
                known = true;
                break;
            }
        }
        if (known || devices_.size() >= 32) continue;

        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            LOG_WARN("Input: cannot open %s (%s)", path.c_str(), strerror(errno));
            continue;
        }
        Device device;
        device.fd = fd;
        device.path = path;
        configureDevice(device);
        devices_.push_back(std::move(device));
        ++added;
        LOG_INFO("Input: opened %s (fd=%d)", path.c_str(), fd);
    }
    closedir(directory);
    return added;
}

int Input::init(int screenWidth, int screenHeight) {
    screenWidth_ = std::max(1, screenWidth);
    screenHeight_ = std::max(1, screenHeight);
    setPointerViewport(screenWidth_, screenHeight_, 0, 0, screenWidth_, screenHeight_);
    pointerState_.x = screenWidth_ / 2;
    pointerState_.y = screenHeight_ / 2;
    if (const char *inputDir = std::getenv("LILSMOKE_INPUT_DIR"))
        inputDir_ = inputDir;
    const char *enabled = std::getenv("LILSMOKE_POINTER_ENABLED");
    const char *speed = std::getenv("LILSMOKE_POINTER_SPEED");
    setPointerOptions(!enabled || std::strcmp(enabled, "0") != 0,
                      speed ? speed : "normal");

    if (const char *directory = std::getenv("LILSMOKE_GAMEPAD_MAP_DIR"))
        gamepadDir_ = directory;
    if (const char *directory = std::getenv("LILSMOKE_GAMEPAD_AUTOCONFIG_DIR"))
        autoconfigDir_ = directory;

    // --- 1. Load keymaps ---
    // System-wide defaults first, then user overrides (which win)
    keymap_.loadDir("/etc/lilsmoke/input-maps");
    keymap_.loadDir("/persist/config/input-maps");

    if (keymap_.size() == 0) {
        LOG_WARN("Input: no keymap bindings loaded — falling back to hardcoded defaults");
        // Fall back to a minimal hardcoded map so the UI is usable
        // (matches /etc/lilsmoke/input-maps/default.map from J0)
        keymap_.set(KEY_UP,        InputButton::Up);
        keymap_.set(KEY_DOWN,      InputButton::Down);
        keymap_.set(KEY_LEFT,      InputButton::Left);
        keymap_.set(KEY_RIGHT,     InputButton::Right);
        keymap_.set(KEY_SPACE,     InputButton::A);
        keymap_.set(KEY_LEFTCTRL,  InputButton::B);
        keymap_.set(KEY_LEFTSHIFT, InputButton::X);
        keymap_.set(KEY_LEFTALT,   InputButton::Y);
        keymap_.set(KEY_TAB,       InputButton::L1);
        keymap_.set(KEY_BACKSPACE, InputButton::R1);
        keymap_.set(KEY_ESC,       InputButton::Select);
        keymap_.set(KEY_ENTER,     InputButton::Start);
        keymap_.set(KEY_Q,         InputButton::Home);
        keymap_.set(KEY_VOLUMEDOWN, InputButton::VolDown);
        keymap_.set(KEY_VOLUMEUP,   InputButton::VolUp);
        keymap_.set(KEY_POWER,     InputButton::Power);
    }

    // --- 2. Open evdev devices ---
    int count = scanDevices();
    lastDeviceScanMs_ = monotonicMilliseconds();
    if (count == 0)
        LOG_WARN("Input: cannot find evdev devices in %s", inputDir_.c_str());

    if (access("/tmp/joy_key", F_OK) == 0) {
        key_t key = ftok("/tmp/joy_key", 'a');
        if (key != (key_t)-1) {
            sharedMemoryId_ = shmget(key, sizeof(uint32_t), 0666);
            if (sharedMemoryId_ >= 0) {
                void *memory = shmat(sharedMemoryId_, nullptr, SHM_RDONLY);
                if (memory != (void *)-1) {
                    sharedKeys_ = static_cast<volatile uint32_t *>(memory);
                    sharedLastRaw_ = *sharedKeys_ & 0xFFFFU;
                    ++count;
                    LOG_INFO("Input: using SF3000 cubevol shared-memory fallback");
                }
            }
        }
    }

    if (count > 0) {
        LOG_INFO("Input: %d device(s), %d keymap bindings", count, keymap_.size());
    } else {
        LOG_WARN("Input: no evdev or cubevol input source found");
    }
    const bool eventOnly = !SDL_WasInit(SDL_INIT_VIDEO) && SDL_WasInit(SDL_INIT_EVENTS);
    if (eventOnly) SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if ((SDL_WasInit(SDL_INIT_VIDEO) || eventOnly) && SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0)
        for (int i = 0; i < SDL_NumJoysticks(); ++i) openSDLController(i);
    return count;
}

namespace {
int axisSource(unsigned code, int direction) {
    return KEY_MAX + 1 + static_cast<int>(code) * 2 + (direction > 0);
}
bool writeInputFile(const std::string &path, const std::string &data) {
    namespace fs = std::filesystem;
    fs::path destination(path);
    std::error_code ec;
    fs::create_directories(destination.parent_path(), ec);
    if (ec) return false;
    for (fs::path p = destination.parent_path(); !p.empty() && p != p.root_path(); p = p.parent_path())
        if (!fs::is_directory(p, ec) || fs::is_symlink(p, ec) || ec) return false;
    const std::string temporary = path + ".new";
    int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    size_t offset = 0;
    while (offset < data.size()) {
        ssize_t n = write(fd, data.data() + offset, data.size() - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        offset += static_cast<size_t>(n);
    }
    bool ok = offset == data.size() && fsync(fd) == 0;
    close(fd);
    if (ok) ok = rename(temporary.c_str(), path.c_str()) == 0;
    if (!ok) unlink(temporary.c_str());
    return ok;
}
InputButton standardPadButton(unsigned code) {
    switch (code) {
        case BTN_SOUTH: case BTN_TRIGGER: case BTN_0: return InputButton::A;
        case BTN_EAST: case BTN_THUMB: case BTN_1: return InputButton::B;
        case BTN_NORTH: case BTN_THUMB2: case BTN_2: return InputButton::X;
        case BTN_WEST: case BTN_TOP: case BTN_3: return InputButton::Y;
        case BTN_TL: case BTN_TOP2: case BTN_4: return InputButton::L1;
        case BTN_TR: case BTN_PINKIE: case BTN_5: return InputButton::R1;
        case BTN_TL2: case BTN_BASE: case BTN_6: return InputButton::L2;
        case BTN_TR2: case BTN_BASE2: case BTN_7: return InputButton::R2;
        case BTN_SELECT: case BTN_BASE3: case BTN_8: return InputButton::Select;
        case BTN_START: case BTN_BASE4: case BTN_9: return InputButton::Start;
        case BTN_THUMBL: return InputButton::L3;
        case BTN_THUMBR: return InputButton::R3;
        case BTN_MODE: return InputButton::Home;
        case BTN_DPAD_UP: return InputButton::Up;
        case BTN_DPAD_DOWN: return InputButton::Down;
        case BTN_DPAD_LEFT: return InputButton::Left;
        case BTN_DPAD_RIGHT: return InputButton::Right;
        default: return InputButton::None;
    }
}
const char *retroPadAction(InputButton button) {
    // UI confirms with south; RetroPad calls that same position B.
    switch (button) {
        case InputButton::A: return "b";
        case InputButton::B: return "a";
        case InputButton::Y: return "y";
        case InputButton::X: return "x";
        case InputButton::L1: return "l";
        case InputButton::R1: return "r";
        case InputButton::L2: return "l2";
        case InputButton::R2: return "r2";
        case InputButton::Home: return "menu_toggle";
        default: return Keymap::actionName(button);
    }
}
}

InputButton Input::deviceButton(const Device &device, unsigned code) const {
    auto mapped = device.overrides.find(static_cast<int>(code));
    if (mapped != device.overrides.end()) return mapped->second;
    return device.gamepad ? standardPadButton(code) : keymap_.lookup(code);
}

void Input::refreshState() {
    state_ = {};
    for (const Device &device : devices_)
        for (const auto &held : device.held) updateButtonState(state_, held.second, true);
    if (sharedKeys_)
        for (unsigned bit = 0; bit < 16; ++bit)
            if ((*sharedKeys_ & (1U << bit)) != 0) updateButtonState(state_, sharedBitButton(bit), true);
}

void Input::sourceButton(Device &device, int source, InputButton button, InputEvent event) {
    const auto isHeld = [&]() {
        for (const auto &d : devices_) for (const auto &entry : d.held)
            if (entry.second == button) return true;
        if (sharedKeys_) for (unsigned bit = 0; bit < 16; ++bit)
            if ((*sharedKeys_ & (1U << bit)) && sharedBitButton(bit) == button) return true;
        return false;
    };
    const bool before = isHeld();
    if (event == InputEvent::Released) device.held.erase(source);
    else if (button != InputButton::None) device.held[source] = button;
    refreshState();
    const bool after = isHeld();
    if (before != after) queueButton(button, after ? InputEvent::Pressed : InputEvent::Released);
    else if (after && event == InputEvent::Repeat) queueButton(button, event);
}

void Input::releaseButtons(Device &device) {
    const auto held = device.held;
    for (const auto &entry : held) sourceButton(device, entry.first, entry.second, InputEvent::Released);
}

void Input::openSDLController(int index) {
    if (devices_.size() >= 32 || !SDL_IsGameController(index)) return;
    const SDL_JoystickID instance = SDL_JoystickGetDeviceInstanceID(index);
    const std::string path = "sdl-controller:" + std::to_string(instance);
    if (std::any_of(devices_.begin(), devices_.end(), [&](const Device &d) { return d.path == path; })) return;
    SDL_GameController *controller = SDL_GameControllerOpen(index);
    if (!controller) return;
    Device device;
    device.fd = -3;
    device.gamepad = true;
    device.path = path;
    device.sdlController = controller;
    device.usbVendor = SDL_GameControllerGetVendor(controller);
    device.usbProduct = SDL_GameControllerGetProduct(controller);
    devices_.push_back(std::move(device));
}

void Input::handleSDLEvent(const SDL_Event &event) {
    if (event.type == SDL_CONTROLLERDEVICEADDED) { openSDLController(event.cdevice.which); return; }
    if (event.type != SDL_CONTROLLERAXISMOTION && event.type != SDL_CONTROLLERBUTTONDOWN &&
        event.type != SDL_CONTROLLERBUTTONUP && event.type != SDL_CONTROLLERDEVICEREMOVED) return;
    const SDL_JoystickID instance = event.type == SDL_CONTROLLERAXISMOTION ? event.caxis.which :
        event.type == SDL_CONTROLLERDEVICEREMOVED ? event.cdevice.which : event.cbutton.which;
    const std::string path = "sdl-controller:" + std::to_string(instance);
    auto found = std::find_if(devices_.begin(), devices_.end(), [&](const Device &d) { return d.path == path; });
    if (found == devices_.end()) return;
    Device &device = *found;
    if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
        releaseButtons(device);
        if (device.sdlController) SDL_GameControllerClose(device.sdlController);
        devices_.erase(found);
        return;
    }
    // Prefer native mappings when SDL also sees the same hardware. Otherwise
    // a complete SDL press/release packet can be replayed later through evdev.
    if ((device.usbVendor || device.usbProduct) &&
        std::any_of(devices_.begin(), devices_.end(), [&](const Device &other) {
            return other.gamepad && other.fd != -3 && other.usbVendor == device.usbVendor &&
                   other.usbProduct == device.usbProduct;
        })) {
        releaseButtons(device);
        return;
    }
    if (event.type == SDL_CONTROLLERAXISMOTION) {
        static const unsigned axisCodes[] = {ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ};
        if(event.caxis.axis >= sizeof(axisCodes)/sizeof(axisCodes[0])) return;
        for(unsigned i=0;i<sizeof(axisCodes)/sizeof(axisCodes[0]);++i)
            if(!device.axes.count(axisCodes[i])) device.axes[axisCodes[i]]={i<4?-32768:0,32767,i<4?3500:0,0};
        if(event.caxis.axis < SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
            queueAxis(device,axisCodes[event.caxis.axis],event.caxis.value);
            return;
        }
        const bool right = event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
        device.axes[right?ABS_RZ:ABS_Z].value=event.caxis.value;
        const int source = right ? BTN_TR2 : BTN_TL2;
        // Hysteresis avoids repeated edges around a resting trigger threshold.
        const bool down = event.caxis.value > (device.held.count(source) ? 6000 : 10000);
        sourceButton(device, source, right ? InputButton::R2 : InputButton::L2,
                     down ? InputEvent::Pressed : InputEvent::Released);
        return;
    }
    static const unsigned codes[] = {BTN_SOUTH, BTN_EAST, BTN_WEST, BTN_NORTH,
        BTN_SELECT, BTN_MODE, BTN_START, BTN_THUMBL, BTN_THUMBR, BTN_TL, BTN_TR,
        BTN_DPAD_UP, BTN_DPAD_DOWN, BTN_DPAD_LEFT, BTN_DPAD_RIGHT};
    if (event.cbutton.button >= sizeof(codes) / sizeof(codes[0])) return;
    const unsigned code = codes[event.cbutton.button];
    sourceButton(device, code, deviceButton(device, code),
                 event.type == SDL_CONTROLLERBUTTONDOWN ? InputEvent::Pressed : InputEvent::Released);
}

bool Input::captureBinding(Device &device, unsigned type, unsigned code, int direction) {
    if (!capture_ || code > (type == EV_KEY ? KEY_MAX : ABS_MAX)) return false;
    rawBinding_ = {device.id, device.name, type, code, direction, true,
                   device.keyboard && type == EV_KEY && code == KEY_ESC};
    return true;
}

InputState Input::gameplayState() const {
    InputState result{};
    for (const auto &device : devices_) for (const auto &held : device.held) {
        auto action = held.second;
        // UI uses RetroPad positions; native PC ports use Xbox/SDL X/Y letters.
        // Explicit user remaps and keyboard-style stock pads keep their meaning.
        if (device.gamepad && device.overrides.count(held.first) == 0) {
            if (held.first == BTN_WEST) action = InputButton::X;
            else if (held.first == BTN_NORTH) action = InputButton::Y;
        }
        updateButtonState(result, action, true);
    }
    if (sharedKeys_) for (unsigned bit = 0; bit < 16; ++bit)
        if ((*sharedKeys_ & (1U << bit)) != 0) updateButtonState(result, sharedBitButton(bit), true);
    return result;
}

GamepadAnalogState Input::analogState() const {
    GamepadAnalogState result;
    int64_t leftStrength=-1,rightStrength=-1;
    for (const auto &device : devices_) {
        if (!device.gamepad || device.dropped) continue;
        if(device.fd==-3 && (device.usbVendor || device.usbProduct) &&
           std::any_of(devices_.begin(),devices_.end(),[&](const Device &other) {
               return other.gamepad && other.fd!=-3 && other.usbVendor==device.usbVendor && other.usbProduct==device.usbProduct;
           })) continue;
        const auto normalize = [&](unsigned code) -> int16_t {
            const auto it = device.axes.find(code);
            if (it == device.axes.end()) return 0;
            const auto &axis = it->second;
            const int64_t center = (int64_t(axis.minimum) + axis.maximum) / 2;
            const int64_t offset = int64_t(axis.value) - center;
            const int64_t span = offset < 0 ? center - axis.minimum : axis.maximum - center;
            const int64_t dead = std::max<int64_t>(0, axis.flat);
            if (span <= dead || std::abs(offset) <= dead) return 0;
            const int64_t distance = std::min(span, std::abs(offset)) - dead;
            const int64_t scaled = distance * 32767 / (span - dead);
            return static_cast<int16_t>(offset < 0 ? -scaled : scaled);
        };
        if (device.axes.count(ABS_X) && device.axes.count(ABS_Y)) {
            const auto x=normalize(ABS_X),y=normalize(ABS_Y);
            const int64_t strength=int64_t(x)*x+int64_t(y)*y;
            if(strength>leftStrength) {
                result.hasLeft = true;
                result.leftX=x;result.leftY=y;leftStrength=strength;
            }
        }
        if (device.axes.count(ABS_RX) && device.axes.count(ABS_RY)) {
            const auto x=normalize(ABS_RX),y=normalize(ABS_RY);
            const int64_t strength=int64_t(x)*x+int64_t(y)*y;
            if(strength>rightStrength) {
                result.hasRight = true;
                result.rightX=x;result.rightY=y;rightStrength=strength;
            }
        }
    }
    return result;
}

void Input::queueAxis(Device &device, unsigned code, int value) {
    auto found = device.axes.find(code);
    if (found == device.axes.end()) return;
    auto &axis = found->second;
    const int64_t range = static_cast<int64_t>(axis.maximum) - axis.minimum;
    const bool hat = code >= ABS_HAT0X && code <= ABS_HAT3Y;
    const bool trigger = ((code == ABS_Z || code == ABS_RZ) &&
                         device.axes.count(ABS_RX) && device.axes.count(ABS_RY)) ||
                         (code >= ABS_HAT1X && code <= ABS_HAT2Y && axis.minimum >= 0);
    value = std::max(axis.minimum, std::min(value, axis.maximum));
    auto directionFor = [&](int sample) {
        int64_t scaled = (static_cast<int64_t>(sample) - axis.minimum) * 2000 / range - 1000;
        if (trigger) return scaled > 0 ? 1 : 0;
        if (hat) return scaled < -500 ? -1 : (scaled > 500 ? 1 : 0);
        int dead = std::max<int64_t>(250, std::min<int64_t>(800, int64_t(axis.flat) * 2000 / range));
        // Release hysteresis avoids chatter around the stick dead zone.
        if (device.held.count(axisSource(code, -1)) || device.held.count(axisSource(code, 1)))
            dead = std::max(100, dead - 100);
        return scaled < -dead ? -1 : (scaled > dead ? 1 : 0);
    };
    int oldDirection = directionFor(axis.value), direction = directionFor(value);
    axis.value = value;
    if (capture_ && direction && direction != oldDirection &&
        captureBinding(device, EV_ABS, code, direction)) return;
    for (int sign : {-1, 1}) {
        const int source = axisSource(code, sign);
        InputButton action = InputButton::None;
        auto override = device.overrides.find(source);
        if (override != device.overrides.end()) action = override->second;
        else if (code == ABS_X || code == ABS_Y || code == ABS_HAT0X || code == ABS_HAT0Y)
            action = hatButton(code == ABS_X || code == ABS_HAT0X, sign);
        else if (trigger && sign > 0) {
            if (code == ABS_HAT1X) action = InputButton::R1;
            else if (code == ABS_HAT1Y) action = InputButton::L1;
            else if (code == ABS_HAT2Y || code == ABS_Z) action = InputButton::L2;
            else action = InputButton::R2;
        }
        if (direction == sign && action != InputButton::None && !device.held.count(source))
            sourceButton(device, source, action, InputEvent::Pressed);
        else if (direction != sign && device.held.count(source))
            sourceButton(device, source, device.held.at(source), InputEvent::Released);
    }
}

void Input::loadGamepadMap(Device &device) {
    std::ifstream file(gamepadDir_ + "/" + device.id + ".map");
    std::map<int, InputButton> mapped;
    std::string line;
    size_t bytes = 0, count = 0;
    while (std::getline(file, line)) {
        bytes += line.size();
        if (bytes > 32768 || ++count > 512) return;
        if (line.empty() || line[0] == '#') continue;
        std::istringstream input(line);
        unsigned type, code;
        int direction;
        std::string name, extra;
        if (!(input >> type >> code >> direction >> name) || (input >> extra)) return;
        InputButton action = Keymap::parseAction(name);
        if (action == InputButton::None || action > InputButton::Home ||
            !((type == EV_KEY && code <= KEY_MAX && direction == 0) ||
              (type == EV_ABS && code <= ABS_MAX && (direction == -1 || direction == 1)))) return;
        mapped[type == EV_KEY ? static_cast<int>(code) : axisSource(code, direction)] = action;
    }
    device.overrides = std::move(mapped);
}

bool Input::bindCaptured(InputButton action) {
    if (!rawBinding_.ready || rawBinding_.cancelled || action == InputButton::None || action > InputButton::Home)
        return false;
    for (Device &device : devices_) {
        if (device.id != rawBinding_.deviceID) continue;
        if (!device.gamepad) {
            if (rawBinding_.type != EV_KEY) return false;
            keymap_.set(rawBinding_.code, action);
            bool ok = keymap_.saveUserMap("/persist/config/input-maps/user.map");
            bindingStatus_ = ok ? "Saved keyboard/device binding" : "Could not save binding";
            return ok;
        }
        lastMappedDevice_ = device.id;
        auto mapped = device.overrides;
        int source = rawBinding_.type == EV_KEY ? static_cast<int>(rawBinding_.code) :
                     axisSource(rawBinding_.code, rawBinding_.direction);
        for (auto binding = mapped.begin(); binding != mapped.end();) {
            if (binding->second == action) binding = mapped.erase(binding);
            else ++binding;
        }
        mapped[source] = action;
        std::ostringstream data;
        data << "# LilSmokeUI per-device bindings: type code direction action\n";
        for (const auto &binding : mapped) {
            int code = binding.first, direction = 0;
            unsigned type = EV_KEY;
            if (code > KEY_MAX) {
                type = EV_ABS;
                direction = (code - KEY_MAX - 1) % 2 ? 1 : -1;
                code = (code - KEY_MAX - 1) / 2;
            }
            data << type << ' ' << code << ' ' << direction << ' '
                 << Keymap::actionName(binding.second) << '\n';
        }
        if (!writeInputFile(gamepadDir_ + "/" + device.id + ".map", data.str())) {
            bindingStatus_ = "Could not save controller binding";
            return false;
        }
        for (Device &pad : devices_) {
            if (pad.id != device.id) continue;
            pad.overrides = mapped;
            pad.held.clear();
            pad.profileReady = exportGamepadProfile(pad);
        }
        refreshState();
        bindingStatus_ = device.profileReady ? "Saved for UI and RetroArch" : "Saved for UI; joydev profile unavailable";
        return true;
    }
    bindingStatus_ = "Controller disconnected";
    return false;
}

std::string Input::bindingLabel(InputButton action) const {
    for (const Device &device : devices_) {
        if (device.id != lastMappedDevice_) continue;
        for (const auto &binding : device.overrides) {
            if (binding.second != action) continue;
            if (binding.first <= KEY_MAX) return "button " + std::to_string(binding.first);
            int source = binding.first - KEY_MAX - 1;
            return "axis " + std::to_string(source / 2) + (source % 2 ? "+" : "-");
        }
        return "Default";
    }
    for (const auto &binding : keymap_.bindings())
        if (binding.action == action) return "evdev " + std::to_string(binding.code);
    return "Unmapped";
}

bool Input::exportGamepadProfile(Device &device) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path parent = fs::canonical(fs::path("/sys/class/input") / fs::path(device.path).filename() / "device", ec);
    if (ec) return false;
    DIR *directory = opendir(inputDir_.c_str());
    if (!directory) return false;
    int fd = -1;
    while (dirent *entry = readdir(directory)) {
        if (std::strncmp(entry->d_name, "js", 2)) continue;
        fs::path candidate = fs::canonical(fs::path("/sys/class/input") / entry->d_name / "device", ec);
        if (!ec && candidate == parent) {
            fd = open((inputDir_ + "/" + entry->d_name).c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            break;
        }
    }
    closedir(directory);
    if (fd < 0) return false;
    unsigned char axisCount = 0, buttonCount = 0, axes[ABS_CNT]{};
    uint16_t buttons[KEY_MAX - BTN_MISC + 1]{};
    char name[128]{};
    bool ok = ioctl(fd, JSIOCGAXES, &axisCount) == 0 &&
              ioctl(fd, JSIOCGBUTTONS, &buttonCount) == 0 &&
              ioctl(fd, JSIOCGAXMAP, axes) == 0 && ioctl(fd, JSIOCGBTNMAP, buttons) == 0 &&
              ioctl(fd, JSIOCGNAME(sizeof name), name) >= 0;
    close(fd);
    if (!ok || axisCount > 32 || buttonCount > 32) return false;
    std::vector<unsigned> axisCodes(axes, axes + axisCount);
    std::vector<unsigned> buttonCodes(buttons, buttons + buttonCount);
    std::string data = gamepadProfile(device, name, axisCodes, buttonCodes);
    if (data.empty()) return false;
    const std::string profileDirectory = autoconfigDir_ + "/linuxraw";
    if (!writeInputFile(profileDirectory + "/" + device.id + ".cfg", data)) return false;
    // linuxraw matches by name. Remove only our own copied fallback for this
    // name so its equal match score cannot override a user remapping.
    const std::string identity = "input_device = \"" + std::string(name) + "\"";
    fs::directory_iterator files(profileDirectory, ec), end;
    for (; !ec && files != end; files.increment(ec)) {
        const auto &entry = *files;
        if (entry.path().filename().string().rfind("upstream-", 0) != 0 ||
            entry.is_symlink(ec) || !entry.is_regular_file(ec)) continue;
        std::ifstream profile(entry.path());
        std::string line;
        while (std::getline(profile, line)) {
            if (line == identity) { fs::remove(entry.path(), ec); break; }
        }
    }
    return true;
}

std::string Input::gamepadProfile(const Device &device, const std::string &name,
                                  const std::vector<unsigned> &axes,
                                  const std::vector<unsigned> &buttons) const {
    const size_t axisCount = axes.size(), buttonCount = buttons.size();
    if (axisCount > 32 || buttonCount > 32) return {};
    std::string identity(name);
    if (identity.empty() || identity.find_first_of("\"\\\r\n") != std::string::npos) return {};
    std::map<std::string, std::string> values;
    auto axisIndex = [&](unsigned code) {
        for (unsigned i = 0; i < axisCount; ++i) if (axes[i] == code) return static_cast<int>(i);
        return -1;
    };
    auto analog = [&](unsigned code, const char *action) {
        int index = axisIndex(code);
        if (index >= 0) {
            values[std::string("input_") + action + "_minus_axis"] = "-" + std::to_string(index);
            values[std::string("input_") + action + "_plus_axis"] = "+" + std::to_string(index);
        }
    };
    analog(ABS_X, "l_x"); analog(ABS_Y, "l_y");
    bool right = axisIndex(ABS_RX) >= 0 && axisIndex(ABS_RY) >= 0;
    analog(right ? ABS_RX : ABS_Z, "r_x"); analog(right ? ABS_RY : ABS_RZ, "r_y");
    // Apply explicit bindings last, irrespective of the kernel's joydev order.
    for (bool custom : {false, true}) {
        for (unsigned index = 0; index < buttonCount; ++index) {
            if ((device.overrides.count(static_cast<int>(buttons[index])) != 0) != custom) continue;
            InputButton action = deviceButton(device, buttons[index]);
            if (action != InputButton::None)
                values["input_" + std::string(retroPadAction(action)) + "_btn"] = std::to_string(index);
        }
        for (unsigned i = 0; i < axisCount; ++i) {
            for (int sign : {-1, 1}) {
                InputButton action = InputButton::None;
                auto override = device.overrides.find(axisSource(axes[i], sign));
                if ((override != device.overrides.end()) != custom) continue;
                if (custom) action = override->second;
                else if (axes[i] == ABS_HAT0X || axes[i] == ABS_HAT0Y)
                    action = hatButton(axes[i] == ABS_HAT0X, sign);
                else if (sign > 0) {
                    if (right && axes[i] == ABS_Z) action = InputButton::L2;
                    else if (right && axes[i] == ABS_RZ) action = InputButton::R2;
                    else if (axes[i] == ABS_HAT1X) action = InputButton::R1;
                    else if (axes[i] == ABS_HAT1Y) action = InputButton::L1;
                    else if (axes[i] == ABS_HAT2X) action = InputButton::R2;
                    else if (axes[i] == ABS_HAT2Y) action = InputButton::L2;
                }
                if (action != InputButton::None)
                    values["input_" + std::string(retroPadAction(action)) + "_axis"] =
                        (sign > 0 ? "+" : "-") + std::to_string(i);
            }
        }
    }
    std::ostringstream data;
    data << "# Generated from kernel joydev maps; user UI mappings override generic defaults\n"
         << "input_driver = \"linuxraw\"\ninput_device = \"" << identity << "\"\n";
    for (const auto &item : values) data << item.first << " = \"" << item.second << "\"\n";
    return data.str();
}

void Input::queueButton(InputButton button, InputEvent event) {
    if (button == InputButton::None) return;
    queued_.push_back({button, event, {}});
}

void Input::queueHat(Device &device, bool horizontal, int value) {
    value = value < 0 ? -1 : (value > 0 ? 1 : 0);
    int &oldValue = horizontal ? device.hatX : device.hatY;
    if (oldValue == value) return;
    InputButton oldButton = hatButton(horizontal, oldValue);
    InputButton newButton = hatButton(horizontal, value);
    if (oldButton != InputButton::None)
        sourceButton(device, axisSource(horizontal ? ABS_HAT0X : ABS_HAT0Y, oldValue), oldButton, InputEvent::Released);
    oldValue = value;
    if (newButton != InputButton::None)
        sourceButton(device, axisSource(horizontal ? ABS_HAT0X : ABS_HAT0Y, value), newButton, InputEvent::Pressed);
}

void Input::setPointerViewport(int outputWidth, int outputHeight,
                               int insetX, int insetY, int width, int height) {
    pointerOutputWidth_ = std::max(1, outputWidth);
    pointerOutputHeight_ = std::max(1, outputHeight);
    pointerInsetX_ = std::clamp(insetX, 0, pointerOutputWidth_ - 1);
    pointerInsetY_ = std::clamp(insetY, 0, pointerOutputHeight_ - 1);
    screenWidth_ = std::clamp(width, 1, pointerOutputWidth_ - pointerInsetX_);
    screenHeight_ = std::clamp(height, 1, pointerOutputHeight_ - pointerInsetY_);
    pointerState_.x = clampCoordinate(pointerState_.x, screenWidth_);
    pointerState_.y = clampCoordinate(pointerState_.y, screenHeight_);
}

bool Input::flushPointerPacket(Device &device, PointerEvent &event) {
    if (!pointerEnabled_) {
        device.relativeX = device.relativeY = 0;
        device.wheelX = device.wheelY = 0;
        device.absDirty = false;
        return false;
    }

    int dx = 0;
    int dy = 0;
    bool touchpad = false;
    if (device.relativePointer &&
        (device.relativeX != 0 || device.relativeY != 0)) {
        int rawX = device.relativeX * pointerSpeedNumerator_ + device.remainderX;
        int rawY = device.relativeY * pointerSpeedNumerator_ + device.remainderY;
        dx = rawX / pointerSpeedDenominator_;
        dy = rawY / pointerSpeedDenominator_;
        device.remainderX = rawX % pointerSpeedDenominator_;
        device.remainderY = rawY % pointerSpeedDenominator_;
    } else if (device.absolutePointer && device.absDirty) {
        touchpad = !device.directPointer;
        const int rangeX = std::max(1, device.absMaxX - device.absMinX);
        const int rangeY = std::max(1, device.absMaxY - device.absMinY);
        if (device.directPointer) {
            if (device.touchActive) {
                int newX = static_cast<int64_t>(device.absX - device.absMinX) *
                           std::max(1, pointerOutputWidth_ - 1) / rangeX - pointerInsetX_;
                int newY = static_cast<int64_t>(device.absY - device.absMinY) *
                           std::max(1, pointerOutputHeight_ - 1) / rangeY - pointerInsetY_;
                dx = newX - pointerState_.x;
                dy = newY - pointerState_.y;
            }
        } else if (device.touchActive) {
            if (device.lastAbsValid) {
                int64_t rawX = static_cast<int64_t>(device.absX - device.lastAbsX) * screenWidth_ *
                           pointerSpeedNumerator_ + device.remainderX;
                int64_t rawY = static_cast<int64_t>(device.absY - device.lastAbsY) * screenHeight_ *
                           pointerSpeedNumerator_ + device.remainderY;
                int denominatorX = rangeX * pointerSpeedDenominator_;
                int denominatorY = rangeY * pointerSpeedDenominator_;
                dx = rawX / denominatorX;
                dy = rawY / denominatorY;
                device.remainderX = rawX % denominatorX;
                device.remainderY = rawY % denominatorY;
            }
            device.lastAbsX = device.absX;
            device.lastAbsY = device.absY;
            device.lastAbsValid = true;
        } else {
            device.lastAbsValid = false;
        }
    }

    device.relativeX = device.relativeY = 0;
    device.absDirty = false;
    if (dx == 0 && dy == 0) return false;

    pointerState_.x = clampCoordinate(pointerState_.x + dx, screenWidth_);
    pointerState_.y = clampCoordinate(pointerState_.y + dy, screenHeight_);
    pointerState_.visible = true;
    pointerState_.touchpad = touchpad;
    event.type = PointerEventType::Moved;
    event.x = pointerState_.x;
    event.y = pointerState_.y;
    event.dx = dx;
    event.dy = dy;
    event.touchpad = touchpad;
    return true;
}

bool Input::popQueued(InputButton &button, InputEvent &event,
                      PointerEvent &pointer) {
    if (queued_.empty()) return false;
    QueuedEvent queued = queued_.front();
    queued_.pop_front();
    button = queued.button;
    event = queued.inputEvent;
    pointer = queued.pointer;
    return true;
}

void Input::pollUsbGamepads() {
    ls_gp_state snapshot{};
    if(!ls_gp_read(&usbReader_, &snapshot)) {
        const auto now=ls_gp_now();
        // A writer can own the seqlock for one poll. Do not turn that into a
        // disconnect/reconnect burst; explicit unplug and stale data still clear.
        if(usbReader_.map && usbReader_.generation==lastUsbGeneration_ &&
           lastUsbSnapshotMs_ && now>=lastUsbSnapshotMs_ && now-lastUsbSnapshotMs_<=100) return;
    } else {
        lastUsbSnapshotMs_=snapshot.millis;
        lastUsbGeneration_=usbReader_.generation;
    }
    static const unsigned codes[] = {BTN_SOUTH, BTN_EAST, BTN_WEST, BTN_NORTH,
        BTN_SELECT, BTN_MODE, BTN_START, BTN_THUMBL, BTN_THUMBR, BTN_TL, BTN_TR,
        BTN_DPAD_UP, BTN_DPAD_DOWN, BTN_DPAD_LEFT, BTN_DPAD_RIGHT};
    static const unsigned axes[] = {ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ};
    for (unsigned slot = 0; slot < LS_GP_SLOTS; ++slot) {
        const auto &pad = snapshot.pads[slot];
        const std::string path = "/run/lilsmoke-gamepads/player" + std::to_string(slot);
        auto found = std::find_if(devices_.begin(), devices_.end(), [&](const auto &d) { return d.path == path; });
        const bool replaced = found != devices_.end() &&
            (found->usbGeneration != usbReader_.generation || found->usbPid != snapshot.pid ||
             found->usbInstance != pad.instance || found->usbVendor != pad.vendor ||
             found->usbProduct != pad.product || found->usbVersion != pad.version);
        if (!pad.connected || pad.exported || replaced) {
            if (found != devices_.end()) {
                releaseButtons(*found);
                devices_.erase(found); refreshState();
                found = devices_.end();
            }
            if (!pad.connected || pad.exported) continue;
        }
        if (found == devices_.end()) {
            if (devices_.size() >= 32) continue;
            Device device; device.fd = -2; device.path = path; device.gamepad = true;
            device.usbGeneration = usbReader_.generation; device.usbPid = snapshot.pid;
            device.usbInstance = pad.instance; device.usbVendor = pad.vendor;
            device.usbProduct = pad.product; device.usbVersion = pad.version;
            device.name.assign(pad.name, strnlen(pad.name, sizeof(pad.name)));
            char identity[96];
            std::snprintf(identity, sizeof(identity), "0003-%04x-%04x-%04x-hidapi", pad.vendor, pad.product, pad.version);
            device.id = identity;
            for (unsigned i = 0; i < 6; ++i) device.axes[axes[i]] = {i < 4 ? -32768 : 0, 32767, i < 4 ? 3500 : 0, 0};
            loadGamepadMap(device);
            devices_.push_back(std::move(device)); found = std::prev(devices_.end());
            LOG_INFO("Input: HIDAPI slot=%u name=%s id=%s (shared gamepad, no cursor)", slot, found->name.c_str(), found->id.c_str());
        }
        Device &device = *found;
        for (unsigned i = 0; i < sizeof(codes)/sizeof(codes[0]); ++i) {
            const bool down = (pad.buttons & (1U << i)) != 0;
            const bool before = device.held.count(codes[i]) != 0;
            if (before == down) continue;
            if (capture_ && down && captureBinding(device, EV_KEY, codes[i], 0)) continue;
            const auto button = deviceButton(device, codes[i]);
            if (button != InputButton::None) sourceButton(device, codes[i], button, down ? InputEvent::Pressed : InputEvent::Released);
        }
        for (unsigned i = 0; i < 6; ++i)
            if (device.axes[axes[i]].value != pad.axes[i]) queueAxis(device, axes[i], pad.axes[i]);
    }
}

bool Input::poll(InputButton &btn, InputEvent &evt, PointerEvent &pointer) {
    btn = InputButton::None;
    evt = InputEvent::None;
    pointer = {};
    text_.clear();
    rawBinding_ = {};

    if (popQueued(btn, evt, pointer)) return true;
    pollUsbGamepads();
    if (rawBinding_.ready) { evt = InputEvent::Pressed; return true; }
    if (popQueued(btn, evt, pointer)) return true;

    const uint64_t now = monotonicMilliseconds();
    const uint64_t interval = devices_.empty() ? 500U : 2000U;
    if (now - lastDeviceScanMs_ >= interval) {
        scanDevices();
        lastDeviceScanMs_ = now;
    }

    for (size_t index = 0; index < devices_.size();) {
        Device &device = devices_[index];
        if (device.fd == -2 || device.fd == -3) { ++index; continue; }
        ssize_t n = 0;
        while (true) {
            // Keep unread records across poll() returns, including action barriers.
            if (device.nextEvent == device.eventCount) {
                n = read(device.fd, device.events.data(), sizeof(device.events));
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) break;
                device.nextEvent = 0;
                device.eventCount = static_cast<size_t>(n) / sizeof(input_event);
                if (device.eventCount == 0) break;
            }
            const input_event ev = device.events[device.nextEvent++];
            if (ev.type == EV_SYN && ev.code == SYN_DROPPED) {
                device.dropped = true;
                device.relativeX = device.relativeY = device.wheelX = device.wheelY = 0;
                device.lastAbsValid = device.absDirty = false;
                device.leftShift = device.rightShift = false;
                device.pointerButtons.clear();
                pointerState_.primaryDown = false;
                pointerState_.secondaryDown = pointerState_.middleDown = false;
                releaseButtons(device);
                device.hatX = device.hatY = 0;
                refreshState();
                continue;
            }
            if (device.dropped) {
                if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                    device.dropped = false;
                    const auto keys = capabilityBits(device.fd, EV_KEY, KEY_MAX);
                    std::vector<unsigned long> held(keys.size(), 0);
                    if (ioctl(device.fd, EVIOCGKEY(static_cast<int>(held.size() * sizeof(unsigned long))),
                              held.data()) >= 0) {
                        device.leftShift = testBit(held, KEY_LEFTSHIFT);
                        device.rightShift = testBit(held, KEY_RIGHTSHIFT);
                        for (unsigned code = 0; code <= KEY_MAX; ++code) {
                            if (!testBit(held, code)) continue;
                            InputButton button = deviceButton(device, code);
                            if (button != InputButton::None)
                                sourceButton(device, code, button, InputEvent::Pressed);
                        }
                    }
                    const bool capturing = capture_;
                    capture_ = false;
                    for (auto &entry : device.axes) {
                        input_absinfo current{};
                        if (ioctl(device.fd, EVIOCGABS(entry.first), &current) == 0)
                            queueAxis(device, entry.first, current.value);
                    }
                    capture_ = capturing;
                    if (popQueued(btn, evt, pointer)) return true;
                }
                continue;
            }
            if (ev.type == EV_KEY) {
                if (ev.code == BTN_TOUCH) {
                    if (!device.usesMultitouch) device.touchActive = ev.value != 0;
                    if (!device.touchActive) device.lastAbsValid = false;
                    continue;
                }
                PointerButton mouseButton = pointerButton(ev.code);
                if (pointerEnabled_ && mouseButton != PointerButton::None &&
                    (device.relativePointer || device.absolutePointer)) {
                    PointerEvent click;
                    click.type = ev.value == 0 ? PointerEventType::Released
                                                 : PointerEventType::Pressed;
                    click.button = mouseButton;
                    click.touchpad = device.absolutePointer &&
                                       !device.directPointer;
                    if (device.pointerButtons.size() < 16) device.pointerButtons.push_back(click);
                    continue;
                }

                if (capture_ && ev.value == 1 &&
                    captureBinding(device, EV_KEY, ev.code, 0)) {
                    evt = InputEvent::Pressed;
                    return true;
                }
                InputButton b = deviceButton(device, ev.code);
                if (device.keyboard) {
                    if (ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT) {
                        (ev.code == KEY_LEFTSHIFT ? device.leftShift : device.rightShift) = ev.value != 0;
                        if (textMode_ || b == InputButton::None) continue;
                    }
                    if (ev.code == KEY_CAPSLOCK) {
                        if (ev.value == 1) device.capsLock = !device.capsLock;
                        if (textMode_ || b == InputButton::None) continue;
                    }
                    if (textMode_) {
                        char c = keyboardCharacter(ev.code, device.leftShift || device.rightShift,
                                                   device.capsLock);
                        if (c) {
                            if (ev.value == 0) continue;
                            text_.assign(1, c);
                            pointerState_.visible = false;
                            return true;
                        }
                    }
                    if (keyboardNavigation_) {
                        if (ev.code == KEY_ENTER || ev.code == KEY_KPENTER) b = textMode_ ? InputButton::Start : InputButton::A;
                        else if (ev.code == KEY_ESC) b = InputButton::B;
                        else if (ev.code == KEY_F1) b = InputButton::Home;
                        else if (ev.code == KEY_F2) b = InputButton::X;
                        else if (ev.code == KEY_F3) b = InputButton::Select;
                        else if (ev.code == KEY_F4) b = InputButton::Y;
                    }
                }
                if (b == InputButton::None) continue;
                btn = b;
                if (ev.value == 2) evt = InputEvent::Repeat;
                else evt = ev.value == 1 ? InputEvent::Pressed
                                         : InputEvent::Released;
                sourceButton(device, ev.code, b, evt);
                if (ev.value != 0) pointerState_.visible = false;
                if (popQueued(btn, evt, pointer)) return true;
                btn = InputButton::None; evt = InputEvent::None;
                continue;
            }

            if (ev.type == EV_REL && device.relativePointer) {
                if (ev.code == REL_X) device.relativeX += ev.value;
                else if (ev.code == REL_Y) device.relativeY += ev.value;
                else if (ev.code == REL_WHEEL) device.wheelY += ev.value;
                else if (ev.code == REL_HWHEEL) device.wheelX += ev.value;
                continue;
            }

            if (ev.type == EV_ABS) {
                if (device.gamepad && device.axes.count(ev.code)) {
                    queueAxis(device, ev.code, ev.value);
                    if (rawBinding_.ready) { evt = InputEvent::Pressed; return true; }
                    if (popQueued(btn, evt, pointer)) return true;
                    continue;
                }
                if (ev.code == ABS_HAT0X) {
                    queueHat(device, true, ev.value);
                    if (popQueued(btn, evt, pointer)) return true;
                    continue;
                }
                if (ev.code == ABS_HAT0Y) {
                    queueHat(device, false, ev.value);
                    if (popQueued(btn, evt, pointer)) return true;
                    continue;
                }
                if (!device.absolutePointer) continue;
                if (ev.code == ABS_MT_SLOT) {
                    device.currentSlot = ev.value;
                } else if (ev.code == ABS_MT_TRACKING_ID &&
                           device.currentSlot == 0) {
                    device.touchActive = ev.value >= 0;
                    device.lastAbsValid = false;
                    device.remainderX = device.remainderY = 0;
                } else if (device.usesMultitouch && device.currentSlot == 0 &&
                           ev.code == ABS_MT_POSITION_X) {
                    device.absX = ev.value;
                    device.absDirty = true;
                } else if (device.usesMultitouch && device.currentSlot == 0 &&
                           ev.code == ABS_MT_POSITION_Y) {
                    device.absY = ev.value;
                    device.absDirty = true;
                } else if (!device.usesMultitouch && ev.code == ABS_X) {
                    device.absX = ev.value;
                    device.absDirty = true;
                } else if (!device.usesMultitouch && ev.code == ABS_Y) {
                    device.absY = ev.value;
                    device.absDirty = true;
                }
                continue;
            }

            if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                PointerEvent motion;
                if (flushPointerPacket(device, motion))
                    queued_.push_back({InputButton::None, InputEvent::None, motion});
                if (pointerEnabled_ && (device.wheelX || device.wheelY)) {
                    PointerEvent wheel;
                    wheel.type = PointerEventType::Scroll;
                    wheel.x = pointerState_.x;
                    wheel.y = pointerState_.y;
                    wheel.wheelX = device.wheelX;
                    wheel.wheelY = device.wheelY;
                    queued_.push_back({InputButton::None, InputEvent::None, wheel});
                }
                device.wheelX = device.wheelY = 0;
                for (auto click : device.pointerButtons) {
                    click.x = pointerState_.x;
                    click.y = pointerState_.y;
                    if (pointerEnabled_) {
                        pointerState_.visible = true;
                        if (click.button == PointerButton::Primary)
                            pointerState_.primaryDown = click.type == PointerEventType::Pressed;
                        else if (click.button == PointerButton::Secondary)
                            pointerState_.secondaryDown = click.type == PointerEventType::Pressed;
                        else if (click.button == PointerButton::Middle)
                            pointerState_.middleDown = click.type == PointerEventType::Pressed;
                        queued_.push_back({InputButton::None, InputEvent::None, click});
                    }
                }
                device.pointerButtons.clear();
                if (popQueued(btn, evt, pointer)) return true;
            }
        }
        if ((n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) || n == 0) {
            LOG_INFO("Input: removed %s", device.path.c_str());
            close(device.fd);
            releaseButtons(device);
            refreshState();
            pointerState_.primaryDown = false;
            pointerState_.secondaryDown = pointerState_.middleDown = false;
            devices_.erase(devices_.begin() + index);
            continue;
        }
        ++index;
    }

    if (sharedKeys_) {
        uint32_t raw = *sharedKeys_ & 0xFFFFU;
        sharedPendingChanges_ |= raw ^ sharedLastRaw_;
        sharedLastRaw_ = raw;

        while (sharedPendingChanges_ != 0) {
            unsigned bit = 0;
            while (bit < 16 && (sharedPendingChanges_ & (1U << bit)) == 0) bit++;
            if (bit >= 16) break;
            sharedPendingChanges_ &= ~(1U << bit);

            InputButton b = sharedBitButton(bit);
            if (b == InputButton::None) continue;
            bool pressed = (raw & (1U << bit)) != 0;
            refreshState();
            if (pressed) pointerState_.visible = false;
            btn = b;
            evt = pressed ? InputEvent::Pressed : InputEvent::Released;
            return true;
        }
    }
    return false;
}

}  // namespace lilsmoke
