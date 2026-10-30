#include "Input.h"
#include "PointerMotionBatch.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

using namespace lilsmoke;
#ifdef LILSMOKE_COUNT_READS
static size_t parserReads = 0;
extern "C" ssize_t __real_read(int, void *, size_t);
extern "C" ssize_t __wrap_read(int fd, void *data, size_t bytes) {
    if (fd == 100) {
        ++parserReads;
        if (std::getenv("LILSMOKE_TEST_SINGLE_READ")) bytes = std::min(bytes, sizeof(input_event));
    }
    return __real_read(fd, data, bytes);
}
#endif
namespace lilsmoke {
struct InputTestAccess {
    static int attach(Input &input, bool touchpad = false) {
        int fds[2];
        if (pipe2(fds, O_NONBLOCK | O_CLOEXEC)) std::abort();
        // Keep syscall accounting restricted to this isolated input stream.
        int reader = fcntl(fds[0], F_DUPFD_CLOEXEC, 100);
        close(fds[0]);
        if (reader < 0) std::abort();
        input.inputDir_ = "/nonexistent-cursor-qa";
        input.pointerState_.x = 320; input.pointerState_.y = 240;
        Input::Device device;
        device.fd = reader;
        device.relativePointer = !touchpad;
        device.absolutePointer = device.hasTouchSignal = touchpad;
        device.absMaxX = device.absMaxY = 1000;
        input.devices_.push_back(std::move(device));
        return fds[1];
    }
};
}
static int checks = 0;
static void check(bool ok, const char *label) {
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", label); std::exit(1); }
    ++checks;
}
static void emit(int fd, unsigned type, unsigned code, int value) {
    input_event event{};
    event.type = type; event.code = code; event.value = value;
    check(write(fd, &event, sizeof(event)) == sizeof(event), "write isolated pipe");
}
static void sync(int fd) { emit(fd, EV_SYN, SYN_REPORT, 0); }
static std::vector<PointerEvent> drain(Input &input, bool coalesce = false) {
    std::vector<PointerEvent> result;
    PointerMotionBatch motion;
    InputButton button; InputEvent event; PointerEvent pointer;
    while (input.poll(button, event, pointer)) {
        if (coalesce && !input.pointerState().dragging() && motion.append(pointer)) continue;
        if (motion.pending()) result.push_back(motion.take());
        result.push_back(pointer);
    }
    if (motion.pending()) result.push_back(motion.take());
    return result;
}
int main(int argc, char **argv) {
    {
        Input input;
        int writer = InputTestAccess::attach(input);
        for (int i = 0; i < 128; ++i) { emit(writer, EV_REL, REL_X, 1); sync(writer); }
        const auto events = drain(input, true);
        check(events.size() == 1 && events[0].x == 448 && events[0].dx == 128,
              "128 complete packets coalesce to latest actual position");
        check(input.pointerState().x == 448, "parser retains all packet deltas");
        close(writer);
        if (argc > 1 && std::strcmp(argv[1], "--burst") == 0) {
            std::printf("UI_CURSOR_BURST_OK raw_records=256 packets=128 dispatched_motion=1 read_fd=100\n");
#ifdef LILSMOKE_COUNT_READS
            std::printf("parser_read_syscalls=%zu mode=%s\n", parserReads,
                        std::getenv("LILSMOKE_TEST_SINGLE_READ") ? "single-record-control" : "production-batched");
#endif
            return 0;
        }
    }
    {
        Input input;
        int writer = InputTestAccess::attach(input);
        emit(writer, EV_REL, REL_X, 2); sync(writer);
        emit(writer, EV_KEY, BTN_LEFT, 1); sync(writer);
        emit(writer, EV_REL, REL_X, 3); sync(writer);
        emit(writer, EV_REL, REL_X, 4); sync(writer);
        emit(writer, EV_KEY, BTN_LEFT, 0); sync(writer);
        emit(writer, EV_REL, REL_X, 5); sync(writer);
        emit(writer, EV_REL, REL_WHEEL, -1); sync(writer);
        emit(writer, EV_KEY, BTN_RIGHT, 1); sync(writer);
        emit(writer, EV_KEY, BTN_RIGHT, 0); sync(writer);
        emit(writer, EV_KEY, BTN_MIDDLE, 1); sync(writer);
        emit(writer, EV_KEY, BTN_MIDDLE, 0); sync(writer);
        const auto events = drain(input, true);
        check(events.size() == 11, "actions and held-button drag are not coalesced");
        check(events[0].x == 322 && events[1].type == PointerEventType::Pressed && events[1].x == 322,
              "press observes preceding actual motion");
        check(events[2].x == 325 && events[3].x == 329 && events[4].type == PointerEventType::Released,
              "all drag packets and release stay ordered");
        check(events[5].x == 334 && events[6].wheelY == -1 && events[6].x == 334,
              "wheel observes preceding actual motion");
        check(events[7].button == PointerButton::Secondary && events[8].type == PointerEventType::Released &&
              events[9].button == PointerButton::Middle && events[10].type == PointerEventType::Released,
              "secondary and middle press/release preserved");
        close(writer);
    }
    {
        Input input;
        int writer = InputTestAccess::attach(input);
        input.keymap().set(KEY_ENTER, InputButton::Start);
        for (int i = 0; i < 31; ++i) { emit(writer, EV_REL, REL_X, 1); sync(writer); }
        emit(writer, EV_KEY, KEY_ENTER, 1); sync(writer);
        emit(writer, EV_KEY, KEY_ENTER, 0); sync(writer);
        InputButton button; InputEvent event; PointerEvent pointer;
        for (int i = 0; i < 31; ++i) check(input.poll(button, event, pointer), "buffered motion");
        check(input.poll(button, event, pointer) && button == InputButton::Start && event == InputEvent::Pressed,
              "native button at read-buffer boundary preserved");
        check(input.poll(button, event, pointer) && button == InputButton::Start && event == InputEvent::Released,
              "native release across read-buffer boundary preserved");
        check(!input.poll(button, event, pointer) && !input.state().start, "no lost records or stuck button");
        close(writer);
    }
    {
        Input input;
        int writer = InputTestAccess::attach(input);
        input.setPointerOptions(true, "slow");
        emit(writer, EV_REL, REL_X, 10000); sync(writer);
        emit(writer, EV_REL, REL_X, -1); sync(writer);
        emit(writer, EV_REL, REL_X, -1); sync(writer);
        const auto events = drain(input, true);
        check(events.size() == 1 && events[0].x == 638,
              "coalescing follows per-packet edge clamp and subpixel remainder");
        close(writer);
    }
    {
        Input input;
        int writer = InputTestAccess::attach(input, true);
        emit(writer, EV_KEY, BTN_TOUCH, 1);
        emit(writer, EV_ABS, ABS_X, 100); emit(writer, EV_ABS, ABS_Y, 100); sync(writer);
        check(drain(input, true).empty(), "touchpad initial contact anchors");
        emit(writer, EV_ABS, ABS_X, 200); sync(writer);
        emit(writer, EV_ABS, ABS_X, 250); sync(writer);
        auto events = drain(input, true);
        check(events.size() == 1 && events[0].touchpad && events[0].x == 416,
              "touchpad packets coalesce without discarding scaled remainder");
        emit(writer, EV_KEY, BTN_TOUCH, 0); sync(writer);
        emit(writer, EV_KEY, BTN_TOUCH, 1); emit(writer, EV_ABS, ABS_X, 900); sync(writer);
        check(drain(input, true).empty() && input.pointerState().x == 416, "touchpad recontact still anchors");
        close(writer);
    }
    {
        for (unsigned code : {BTN_RIGHT, BTN_MIDDLE}) {
            Input input;
            int writer = InputTestAccess::attach(input);
            emit(writer, EV_KEY, code, 1); sync(writer);
            emit(writer, EV_REL, REL_X, 2); sync(writer);
            emit(writer, EV_REL, REL_X, 3); sync(writer);
            emit(writer, EV_KEY, code, 0); sync(writer);
            const auto events = drain(input, true);
            check(events.size() == 4 && events[1].x == 322 && events[2].x == 325 &&
                  !input.pointerState().dragging(), "secondary/middle drag retains each packet and clears held state");
            close(writer);
        }
    }
    {
        Input input;
        int writer = InputTestAccess::attach(input, true);
        emit(writer, EV_KEY, BTN_TOUCH, 1);
        emit(writer, EV_ABS, ABS_X, 100); emit(writer, EV_ABS, ABS_Y, 100); sync(writer);
        check(drain(input, true).empty(), "absolute touchpad contact anchor before drag");
        emit(writer, EV_ABS, ABS_X, 200); sync(writer);
        emit(writer, EV_KEY, BTN_LEFT, 1); sync(writer);
        emit(writer, EV_ABS, ABS_X, 220); sync(writer);
        emit(writer, EV_ABS, ABS_X, 240); sync(writer);
        emit(writer, EV_KEY, BTN_LEFT, 0); sync(writer);
        emit(writer, EV_ABS, ABS_X, 250); sync(writer);
        const auto events = drain(input, true);
        check(events.size() == 6 && events[0].touchpad && events[1].touchpad && events[2].touchpad &&
              events[3].touchpad && events[4].touchpad && events[5].touchpad,
              "absolute touchpad motion/click/drag preserve source and all packets");
        check(events[1].type == PointerEventType::Pressed && events[4].type == PointerEventType::Released &&
              events[2].x < events[3].x && events[3].x < events[5].x && !input.pointerState().dragging(),
              "touchpad click coordinates and ordered drag/release remain intact");
        close(writer);
    }
    {
        PointerMotionBatch motion;
        PointerEvent pointer;
        pointer.type = PointerEventType::Moved;
        pointer.dx = std::numeric_limits<int>::max();
        check(motion.append(pointer), "motion accepted");
        pointer.dx = 100;
        check(motion.append(pointer) && motion.take().dx == std::numeric_limits<int>::max(),
              "motion delta accumulation cannot overflow");
        motion.append(pointer);
        pointer.touchpad = true;
        check(!motion.append(pointer), "mouse/touchpad switch is a dispatch barrier");
    }
    std::printf("UI_CURSOR_INPUT_SMOKE_OK checks=%d input_event_bytes=%zu\n", checks, sizeof(input_event));
}
