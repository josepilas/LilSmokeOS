// SPDX-License-Identifier: MPL-2.0
#pragma once

#include "Terminal.h"
#include <string>
#include <sys/types.h>

namespace lilsmoke {

class Pty {
public:
    ~Pty();
    Pty() = default;
    Pty(const Pty &) = delete;
    Pty &operator=(const Pty &) = delete;
    bool start(int rows, int cols, std::string &error, const char *shell = "/bin/sh");
    bool resize(int rows, int cols, std::string &error);
    bool pump(Terminal &terminal, std::string &error);
    bool exited();
    bool eof() const { return eof_; }
    int status() const { return status_; }
    int fd() const { return master_; }
    pid_t pid() const { return child_; }
    void stop();
private:
    int master_ = -1, execError_ = -1;
    pid_t child_ = -1;
    bool reaped_ = false, eof_ = false;
    int status_ = 0;
};

} // namespace lilsmoke
