// SPDX-License-Identifier: MPL-2.0
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "Pty.h"
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

extern char **environ;

namespace lilsmoke {
namespace {
bool failure(std::string &error, const char *operation) {
    error = std::string(operation) + ": " + std::strerror(errno);
    return false;
}

void childFailure(int fd) {
    const int value = errno;
    ssize_t result;
    do { result = write(fd, &value, sizeof(value)); } while (result < 0 && errno == EINTR);
    _exit(127);
}
}

Pty::~Pty() { stop(); }

bool Pty::start(int rows, int cols, std::string &error, const char *shell) {
    if (master_ >= 0 || child_ > 0) { errno = EBUSY; return failure(error, "PTY already started"); }
    error.clear();
    reaped_ = false; eof_ = false; status_ = 0;
    master_ = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (master_ < 0) return failure(error, "open /dev/ptmx (UNIX98 PTYs/devpts required)");
    if (grantpt(master_) < 0 || unlockpt(master_) < 0) {
        failure(error, "grant/unlock PTY"); stop(); return false;
    }
    char name[128];
    int rc = ptsname_r(master_, name, sizeof(name));
    if (rc) { errno = rc; failure(error, "resolve PTY slave"); stop(); return false; }
    const int slave = open(name, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (slave < 0) { failure(error, "open PTY slave (mount /dev/pts)"); stop(); return false; }
    termios attributes{};
    if (tcgetattr(slave, &attributes) < 0) {
        failure(error, "read PTY termios"); close(slave); stop(); return false;
    }
    attributes.c_iflag = ICRNL | IXON;
#ifdef IUTF8
    attributes.c_iflag |= IUTF8;
#endif
    attributes.c_oflag = OPOST | ONLCR;
    attributes.c_cflag = CS8 | CREAD;
    attributes.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | IEXTEN;
    attributes.c_cc[VINTR] = 3; attributes.c_cc[VQUIT] = 28;
    attributes.c_cc[VERASE] = 127; attributes.c_cc[VKILL] = 21;
    attributes.c_cc[VEOF] = 4; attributes.c_cc[VSUSP] = 26;
    attributes.c_cc[VSTART] = 17; attributes.c_cc[VSTOP] = 19;
    attributes.c_cc[VMIN] = 1; attributes.c_cc[VTIME] = 0;
    if (tcsetattr(slave, TCSANOW, &attributes) < 0 || !resize(rows, cols, error)) {
        if (error.empty()) failure(error, "configure PTY termios");
        close(slave); stop(); return false;
    }
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) < 0) {
        failure(error, "create shell error pipe"); close(slave); stop(); return false;
    }
    // Prepare allocations before fork: SDL/DirectFB may already have threads.
    std::vector<std::string> environment;
    for (char **p = environ; *p; ++p) {
        if (std::strncmp(*p, "TERM=", 5) && std::strncmp(*p, "COLORTERM=", 10) &&
            std::strncmp(*p, "PS1=", 4) && std::strncmp(*p, "LINES=", 6) &&
            std::strncmp(*p, "COLUMNS=", 8)) environment.emplace_back(*p);
    }
    environment.emplace_back("TERM=xterm-256color");
    environment.emplace_back("COLORTERM=truecolor");
    environment.emplace_back(geteuid() == 0 ? "PS1=LilSmoke# " : "PS1=LilSmoke$ ");
    std::vector<char *> env;
    for (auto &entry : environment) env.push_back(entry.data());
    env.push_back(nullptr);
    const char *home = std::getenv("HOME");
    if (!home || !*home) home = "/";
    std::vector<int> descriptors;
    if (DIR *directory = opendir("/proc/self/fd")) {
        while (dirent *entry = readdir(directory)) {
            char *end = nullptr;
            const long fd = std::strtol(entry->d_name, &end, 10);
            if (*entry->d_name && !*end && fd >= 3 && fd != pipefd[1])
                descriptors.push_back(static_cast<int>(fd));
        }
        closedir(directory);
    } else {
        const long limit = sysconf(_SC_OPEN_MAX);
        for (long fd = 3; fd < (limit > 0 ? limit : 1024); ++fd)
            if (fd != pipefd[1]) descriptors.push_back(static_cast<int>(fd));
    }
    child_ = fork();
    if (child_ == 0) {
        close(pipefd[0]);
        if (setsid() < 0 || ioctl(slave, TIOCSCTTY, 0) < 0 || tcsetpgrp(slave, getpid()) < 0)
            childFailure(pipefd[1]);
        for (int fd = 0; fd < 3; ++fd) if (dup2(slave, fd) < 0) childFailure(pipefd[1]);
        for (int fd : descriptors) close(fd);
        struct sigaction action{};
        action.sa_handler = SIG_DFL;
        sigemptyset(&action.sa_mask);
        for (int sig : {SIGINT, SIGQUIT, SIGTERM, SIGHUP, SIGPIPE, SIGCHLD, SIGTSTP, SIGTTIN, SIGTTOU})
            sigaction(sig, &action, nullptr);
        sigset_t mask;
        sigemptyset(&mask);
        sigprocmask(SIG_SETMASK, &mask, nullptr);
        if (chdir(home) < 0 && chdir("/") < 0) childFailure(pipefd[1]);
        char *args[] = {const_cast<char *>(shell), const_cast<char *>("-i"), nullptr};
        execve(shell, args, env.data());
        childFailure(pipefd[1]);
    }
    close(slave);
    close(pipefd[1]);
    if (child_ < 0) { failure(error, "fork shell"); close(pipefd[0]); stop(); return false; }
    execError_ = pipefd[0];
    if (fcntl(execError_, F_SETFL, O_NONBLOCK) < 0) {
        failure(error, "set error pipe nonblocking"); stop(); return false;
    }
    return true;
}

bool Pty::resize(int rows, int cols, std::string &error) {
    winsize size{};
    size.ws_row = static_cast<unsigned short>(std::clamp(rows, 1, Terminal::MaxRows));
    size.ws_col = static_cast<unsigned short>(std::clamp(cols, 1, Terminal::MaxCols));
    if (ioctl(master_, TIOCSWINSZ, &size) < 0) return failure(error, "resize PTY");
    return true;
}

bool Pty::pump(Terminal &terminal, std::string &error) {
    if (execError_ >= 0) {
        int code = 0;
        const ssize_t n = read(execError_, &code, sizeof(code));
        if (n == sizeof(code)) { errno = code; return failure(error, "start interactive shell"); }
        if (n == 0) { close(execError_); execError_ = -1; }
        else if (n < 0 && errno != EAGAIN && errno != EINTR) return failure(error, "read shell error");
    }
    auto &queue = terminal.output();
    size_t written = 0;
    while (queue.size() && !eof_ && written < ByteQueue::Capacity) {
        const ssize_t n = write(master_, queue.data(), queue.contiguous());
        if (n > 0) { queue.consume(n); written += n; }
        else if (n < 0 && errno == EINTR) continue;
        else if (n < 0 && errno == EAGAIN) break;
        else if (n < 0 && errno == EIO) { eof_ = true; break; }
        else return failure(error, "write PTY");
    }
    // Bound each turn so an unthrottled producer cannot starve keyboard/events.
    char bytes[4096];
    for (size_t count = 0; count < 65536 && !eof_;) {
        const ssize_t n = read(master_, bytes, sizeof(bytes));
        if (n > 0) { terminal.feed(bytes, n); count += n; }
        else if (n == 0 || (n < 0 && errno == EIO)) { eof_ = true; break; }
        else if (errno == EINTR) continue;
        else if (errno == EAGAIN) break;
        else return failure(error, "read PTY");
    }
    return true;
}

bool Pty::exited() {
    if (child_ <= 0 || reaped_) return reaped_;
    const pid_t result = waitpid(child_, &status_, WNOHANG);
    if (result == child_ || (result < 0 && errno == ECHILD)) reaped_ = true;
    return reaped_;
}

void Pty::stop() {
    const pid_t foreground = master_ >= 0 ? tcgetpgrp(master_) : -1;
    if (foreground > 0 && foreground != getpgrp()) kill(-foreground, SIGHUP);
    if (child_ > 0) kill(-child_, SIGHUP);
    if (master_ >= 0) { close(master_); master_ = -1; }
    if (execError_ >= 0) { close(execError_); execError_ = -1; }
    if (child_ > 0) {
        for (int i = 0; i < 30 && !exited(); ++i) usleep(5000);
        if (foreground > 0 && foreground != getpgrp()) kill(-foreground, SIGKILL);
        kill(-child_, SIGKILL);
        if (!reaped_) kill(child_, SIGKILL);
        for (int i = 0; i < 30 && !exited(); ++i) usleep(5000);
        child_ = -1;
    }
}

} // namespace lilsmoke
