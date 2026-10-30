#pragma once

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/vfs.h>
#include <unistd.h>

namespace lilsmoke {
class BootReadyMarker {
public:
    BootReadyMarker() {
        const char *path = std::getenv("LILSMOKE_BOOT_READY_FILE");
        enabled_ = path && !std::strcmp(path, "/run/lilsmoke-ui-ready");
    }
    void submitted(bool success) {
        if (!enabled_ || !success) return;
        enabled_ = false; // One attempt, never per-frame filesystem polling.
        int dir = open("/run", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (dir < 0) return;
        struct statfs fs{};
        const bool ram = fstatfs(dir, &fs) == 0 &&
            (fs.f_type == 0x01021994 || fs.f_type == 0x858458f6); // tmpfs / ramfs
        close(dir);
        if (!ram) return;
        char temporary[] = "/run/.lilsmoke-ui-ready.XXXXXX";
        const int fd = mkstemp(temporary);
        if (fd < 0) return;
        constexpr char message[] = "UI_FIRST_SUBMITTED_FRAME\n";
        ssize_t n;
        do { n = write(fd, message, sizeof(message) - 1); } while (n < 0 && errno == EINTR);
        const bool closed = close(fd) == 0;
        if (n != static_cast<ssize_t>(sizeof(message) - 1) || !closed || rename(temporary, "/run/lilsmoke-ui-ready") != 0)
            unlink(temporary);
    }
private:
    bool enabled_ = false;
};
} // namespace lilsmoke
