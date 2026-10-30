// SPDX-License-Identifier: MPL-2.0
#include "Keyboard.h"
#include "Pty.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <string>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

using namespace lilsmoke;
namespace {
std::string drain(ByteQueue &queue) {
    std::string result;
    while (queue.size()) {
        size_t length = queue.contiguous();
        result.append(queue.data(), length);
        queue.consume(length);
    }
    return result;
}
void feed(Terminal &terminal, const char *text) { terminal.feed(text, std::strlen(text)); }
std::string text(const Terminal &terminal) {
    std::string result;
    for (int r = 0; r < terminal.rows(); ++r) {
        for (int c = 0; c < terminal.cols(); ++c) {
            VTermScreenCell cell{};
            terminal.cell(r, c, cell);
            result += cell.chars[0] >= 32 && cell.chars[0] < 127 ? static_cast<char>(cell.chars[0]) : ' ';
        }
        result += '\n';
    }
    return result;
}
void until(const std::function<bool()> &condition) {
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (!condition()) {
        assert(std::chrono::steady_clock::now() < limit);
        usleep(2000);
    }
}
void queueTest() {
    ByteQueue q;
    std::string a(16000, 'a'), b(1000, 'b');
    assert(q.append(a.data(), a.size()));
    assert(!q.append(b.data(), b.size()));
    assert(q.size() == a.size());
    q.consume(15000);
    assert(q.append(b.data(), b.size()));
    assert(drain(q) == std::string(1000, 'a') + b);
    assert(q.size() == 0);
    assert(q.append(a.data(), a.size()));
    assert(drain(q) == a);
    std::puts("PASS bounded queue, atomic overflow, wraparound");
}
void parserTest() {
    Terminal t(8, 40);
    feed(t, "abc\rX\033[2;4H\033[31mred\033[0m");
    VTermScreenCell cell{};
    assert(t.cell(0, 0, cell) && cell.chars[0] == 'X');
    assert(t.cell(1, 3, cell) && cell.chars[0] == 'r');
    assert(t.rgb(cell.fg).rgb.red > t.rgb(cell.fg).rgb.green);
    feed(t, "\033[3;1H\xe7\x95\x8c\x65\xcc\x81");
    assert(t.cell(2, 0, cell) && cell.chars[0] == 0x754c && cell.width == 2);
    assert(t.cell(2, 2, cell) && cell.chars[0] == 'e' && cell.chars[1] == 0x301);
    for (int i = 0; i < 10000; ++i) feed(t, "line\r\n");
    assert(t.historySize() == Terminal::HistoryLines);
    t.scroll(99999);
    assert(t.scrollOffset() == Terminal::HistoryLines);
    assert(!t.cursorVisible());
    feed(t, "\033[?1049h");
    assert(t.alternate() && t.scrollOffset() == 0);
    int history = t.historySize();
    for (int i = 0; i < 20; ++i) feed(t, "alternate\r\n");
    assert(t.historySize() == history);
    feed(t, "\033[?1049l");
    assert(!t.alternate());
    t.resize(17, 61);
    assert(t.rows() == 17 && t.cols() == 61);
    t.scroll(128);
    assert(t.cell(0, 60, cell) && cell.chars[0] == 0);
    t.resize(0, 9999);
    assert(t.rows() == 1 && t.cols() == Terminal::MaxCols);
    feed(t, "\033[3J");
    assert(t.historySize() == 0 && t.scrollOffset() == 0);
    std::puts("PASS ANSI cursor/color, Unicode/wide/combining, alternate screen, bounded history, resize");
}
SDL_Event key(SDL_Keycode code, Uint16 mod = 0) {
    SDL_Event event{};
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = code;
    event.key.keysym.mod = mod;
    return event;
}
SDL_Event input(const char *s) {
    SDL_Event event{};
    event.type = SDL_TEXTINPUT;
    std::strncpy(event.text.text, s, sizeof(event.text.text) - 1);
    return event;
}
void keyboardTest() {
    Terminal t(8, 40);
    Keyboard k;
    k.event(key(SDLK_c, KMOD_CTRL), t);
    k.event(input("c"), t);
    k.event(key(SDLK_d, KMOD_CTRL), t);
    k.event(key(SDLK_z, KMOD_CTRL), t);
    assert(drain(t.output()) == std::string("\003\004\032", 3));
    k.event(key(SDLK_UP), t);
    assert(drain(t.output()) == "\033[A");
    feed(t, "\033[?1h");
    k.event(key(SDLK_UP), t);
    assert(drain(t.output()) == "\033OA");
    k.event(key(SDLK_UP, KMOD_CTRL), t);
    assert(drain(t.output()) == "\033[1;5A");
    k.event(key(SDLK_TAB, KMOD_SHIFT), t);
    assert(drain(t.output()) == "\033[Z");
    k.event(key(SDLK_BACKSPACE), t);
    k.event(key(SDLK_RETURN), t);
    k.event(input("\r"), t);
    assert(drain(t.output()) == "\177\r");
    k.event(key(SDLK_e), t);
    k.event(input("\xc3\xa9"), t);
    assert(drain(t.output()) == "\xc3\xa9");
    k.event(key(SDLK_x, KMOD_ALT), t);
    k.event(input("x"), t);
    assert(drain(t.output()) == "\033x");
    k.event(key(SDLK_q, KMOD_CTRL | KMOD_RALT), t);
    k.event(input("@"), t);
    assert(drain(t.output()) == "@");
    k.event(key(SDLK_2, KMOD_CTRL | KMOD_SHIFT), t);
    assert(drain(t.output()) == std::string(1, '\0'));
    k.event(key(SDLK_i, KMOD_CTRL), t);
    k.event(key(SDLK_LEFTBRACKET, KMOD_CTRL), t);
    k.event(key(SDLK_SPACE, KMOD_CTRL), t);
    assert(drain(t.output()) == std::string("\t\033\0", 3));
    for (int i = 0; i < 20; ++i) feed(t, "scroll\r\n");
    k.event(key(SDLK_PAGEUP, KMOD_SHIFT), t);
    assert(t.scrollOffset() > 0 && t.output().size() == 0);
    k.event(key(SDLK_END, KMOD_SHIFT), t);
    assert(t.scrollOffset() == 0);
    feed(t, "\033[6n");
    assert(!drain(t.output()).empty());
    std::puts("PASS Ctrl-C/D/Z, arrows/application mode, modifiers, Shift-Tab, UTF-8, Alt/AltGr, scroll keys, terminal replies");
}
void ptyTest() {
    Terminal t(24, 80);
    Pty p;
    std::string error;
    const char *shell = std::getenv("LILSMOKE_TERMINAL_TEST_SHELL");
    assert(p.start(24, 80, error, shell ? shell : "/bin/sh"));
    auto pump = [&] { if (!p.pump(t, error)) std::fprintf(stderr, "%s\n", error.c_str()); assert(error.empty()); };
    auto send = [&](const char *s) { assert(t.output().append(s, std::strlen(s))); };
    auto expect = [&](const char *s) {
        std::printf("PTY expect: %s\n", s);
        until([&] { pump(); return text(t).find(s) != std::string::npos; });
    };
    // Octal escapes keep the expected marker out of the echoed input command.
    send("stty -echo; printf '\\122\\105\\101\\104\\131\\n'\n");
    expect("READY");
    send("test -t 0 && test -t 1 && test -t 2 && printf '\\124\\124\\131\\117\\113\\n'; stty size\n");
    expect("TTYOK"); expect("24 80");
    until([&] { pump(); return tcgetpgrp(p.fd()) == p.pid(); });
    assert(p.resize(17, 61, error)); t.resize(17, 61);
    send("stty size\n"); expect("17 61");
    until([&] { pump(); return tcgetpgrp(p.fd()) == p.pid(); });
    // Absolute path forces a separate job even in BusyBox standalone ash,
    // where a plain 'sleep' may execute as an in-process applet.
    send("/bin/sleep 30\n");
    std::puts("PTY wait: foreground sleep");
    until([&] { pump(); return tcgetpgrp(p.fd()) != p.pid(); });
    t.character('c', VTERM_MOD_CTRL);
    std::puts("PTY wait: Ctrl-C returns foreground to shell");
    until([&] { pump(); return tcgetpgrp(p.fd()) == p.pid(); });
    send("printf '\\111\\116\\124\\105\\122\\122\\125\\120\\124\\105\\104\\n'\n");
    expect("INTERRUPTED");
    send("/usr/bin/yes flood\n");
    until([&] { pump(); return tcgetpgrp(p.fd()) != p.pid(); });
    for (int i = 0; i < 5; ++i) {
        auto start = std::chrono::steady_clock::now();
        pump();
        assert(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
    }
    t.character('c', VTERM_MOD_CTRL);
    until([&] { pump(); return tcgetpgrp(p.fd()) == p.pid(); });
    assert(t.historySize() <= Terminal::HistoryLines);
    t.character('d', VTERM_MOD_CTRL);
    until([&] { pump(); return p.exited(); });
    assert(WIFEXITED(p.status()));
    p.stop();
    std::puts("PASS real /bin/sh, controlling TTY, job control, resize, Ctrl-C/D, output flood responsiveness, exit/reap");
}
void errorTest() {
    Pty p;
    Terminal t(24, 80);
    std::string error;
    assert(p.start(24, 80, error, "/no-such-lilsmoke-shell"));
    until([&] { return !p.pump(t, error); });
    assert(error.find("start interactive shell") != std::string::npos);
    p.stop();
    assert(p.fd() == -1);
    std::puts("PASS exec failure diagnostic and cleanup");
}
void stopTest() {
    Pty p;
    Terminal t(24, 80);
    std::string error;
    const char *shell = std::getenv("LILSMOKE_TERMINAL_TEST_SHELL");
    assert(p.start(24, 80, error, shell ? shell : "/bin/sh"));
    const char command[] = "trap '' HUP; /bin/sleep 30\n";
    assert(t.output().append(command, sizeof(command) - 1));
    until([&] { assert(p.pump(t, error)); const pid_t fg = tcgetpgrp(p.fd()); return fg > 0 && fg != p.pid(); });
    int fd = p.fd();
    auto start = std::chrono::steady_clock::now();
    p.stop();
    assert(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
    assert(p.pid() == -1 && p.fd() == -1 && fcntl(fd, F_GETFD) == -1);
    assert(p.start(24, 80, error));
    const char quit[] = "exit\n";
    t.output().consume(t.output().size());
    assert(t.output().append(quit, sizeof(quit) - 1));
    until([&] { assert(p.pump(t, error)); return p.exited(); });
    std::puts("PASS bounded HUP/KILL escalation, PTY descriptor cleanup, session reuse");
}
}
int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    queueTest(); parserTest(); keyboardTest(); ptyTest(); errorTest(); stopTest();
    std::puts("All terminal tests passed");
}
