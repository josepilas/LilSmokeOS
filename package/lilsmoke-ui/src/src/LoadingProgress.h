#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace lilsmoke {
inline void reportLoadingProgress(unsigned percent, const char *label) {
    const char *path = std::getenv("LILSMOKE_LOADING_PROGRESS");
    if (!path || std::strcmp(path, "/run/m8-loading/progress") || percent > 100) return;
    // Only the private RAM directory prepared by the M8 supervisor is used.
    const int fd = ::open("/run/m8-loading/progress.new", O_WRONLY | O_CREAT |
                          O_TRUNC | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
    if (fd < 0) return;
    char line[96];
    const int n = std::snprintf(line, sizeof(line), "%u\t%.64s\n", percent, label);
    const bool written = n > 0 && static_cast<size_t>(n) < sizeof(line) &&
                         ::write(fd, line, static_cast<size_t>(n)) == n;
    ::close(fd);
    if (written) (void)::rename("/run/m8-loading/progress.new", path);
    else (void)::unlink("/run/m8-loading/progress.new");
}
}
