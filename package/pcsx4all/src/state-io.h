#pragma once

#include "src/psxcommon.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <zlib.h>

// Scoped to paused engine serialization. Never replace the qualified core.
class PcsxStateIo {
public:
    PcsxStateIo() : previous_(SaveFuncs) {
        active_ = this;
        SaveFuncs.open = openFile;
        SaveFuncs.read = readFile;
        SaveFuncs.write = writeFile;
        SaveFuncs.seek = seekFile;
        SaveFuncs.close = closeFile;
    }
    ~PcsxStateIo() { SaveFuncs = previous_; active_ = nullptr; }
    PcsxStateIo(const PcsxStateIo &) = delete;
    PcsxStateIo &operator=(const PcsxStateIo &) = delete;

    static bool syncFile(int fd, bool directory) {
        if (fsync(fd) == 0) return true;
        const int error = errno;
        if (error != ENOSYS && error != EOPNOTSUPP && !(directory && error == EINVAL)) {
            diagnostic("fsync", error, 0);
            return false;
        }
        std::fprintf(stderr, "PCSX_STATE_SYNC_FALLBACK stage=fsync errno=%d directory=%d next=%s\n",
                     error, directory, directory ? "syncfs" : "fdatasync");
        // Both fallbacks report errors. A void sync() is not proof of success.
        const int result = directory ? static_cast<int>(syscall(SYS_syncfs, fd)) : fdatasync(fd);
        if (result == 0) return true;
        diagnostic(directory ? "syncfs" : "fdatasync", errno, 0);
        return false;
    }

    static void diagnostic(const char *stage, int error, int zerror) {
        std::fprintf(stderr, "PCSX_STATE_IO_ERROR stage=%s errno=%d error=%s zlib=%d\n",
                     stage, error, error ? std::strerror(error) : "none", zerror);
    }

    bool engineResult(int result, const char *operation, const char *path) const {
        if (result == 0 && !failed_) return true;
        std::fprintf(stderr, "PCSX_STATE_ENGINE_ERROR operation=%s path=%s result=%d errno=%d bytes=%llu io_failed=%d\n",
                     operation, path, result, errno, static_cast<unsigned long long>(bytes_), failed_);
        return false;
    }

private:
    inline static PcsxStateIo *active_ = nullptr;
    PcsxSaveFuncs previous_;
    bool writing_ = false, failed_ = false;
    uint64_t bytes_ = 0;

    static void *openFile(const char *name, boolean writing) {
        auto &io = *active_;
        io.writing_ = writing;
        io.bytes_ = 0;
        io.failed_ = false;
        SaveFuncs.fd = SaveFuncs.lib_fd = -1;
        errno = 0;
        SaveFuncs.fd = open(name, (writing ? O_RDWR | O_CREAT | O_TRUNC : O_RDONLY) | O_CLOEXEC, 0600);
        const char *stage = "open";
        if (SaveFuncs.fd >= 0) {
            stage = "dup";
            SaveFuncs.lib_fd = dup(SaveFuncs.fd);
            if (SaveFuncs.lib_fd >= 0) {
                stage = "gzdopen";
                if (gzFile file = gzdopen(SaveFuncs.lib_fd, writing ? "wb4" : "rb")) return file;
            }
        }
        const int error = errno;
        io.failed_ = true;
        diagnostic(stage, error, 0);
        if (SaveFuncs.lib_fd >= 0) close(SaveFuncs.lib_fd);
        if (SaveFuncs.fd >= 0) close(SaveFuncs.fd);
        SaveFuncs.fd = SaveFuncs.lib_fd = -1;
        errno = error;
        return nullptr;
    }

    static int transfer(void *file, void *data, u32 len, bool writing) {
        auto &io = *active_;
        // The legacy serializer skips screenshot bytes if malloc fails, which
        // otherwise produces a CRC-valid but shifted/unloadable state.
        if (writing && io.bytes_ == 32 + sizeof(u32) + sizeof(boolean) && len != 160 * 120 * 2) {
            io.failed_ = true;
            diagnostic("screenshot-allocation", ENOMEM, 0);
            errno = ENOMEM;
            return -1;
        }
        char buffer[8192];
        u32 done = 0;
        // MIPS JIT psxM is a valid mapping at address zero. zlib rejects NULL
        // next_in/next_out even though memcpy can access that mapped memory.
        // Always stage, and compile this frontend with -fno-delete-null-pointer-checks.
        auto address = reinterpret_cast<uintptr_t>(data);
        while (done < len) {
            const unsigned chunk = std::min<unsigned>(sizeof buffer, len - done);
            errno = 0;
            int count;
            if (writing) {
                std::memcpy(buffer, reinterpret_cast<const void *>(address + done), chunk);
                count = gzwrite(static_cast<gzFile>(file), buffer, chunk);
            } else {
                count = gzread(static_cast<gzFile>(file), buffer, chunk);
                if (count > 0) std::memcpy(reinterpret_cast<void *>(address + done), buffer, count);
            }
            if (count != static_cast<int>(chunk)) {
                const int error = errno;
                int zerror = 0;
                const char *detail = gzerror(static_cast<gzFile>(file), &zerror);
                io.failed_ = true;
                diagnostic(writing ? "gzwrite" : "gzread", error, zerror);
                std::fprintf(stderr, "PCSX_STATE_TRANSFER requested=%u completed=%u bytes=%llu detail=%s\n",
                             len, done, static_cast<unsigned long long>(io.bytes_), detail);
                errno = error;
                return -1;
            }
            done += chunk;
            io.bytes_ += chunk;
        }
        return static_cast<int>(done);
    }

    static int readFile(void *file, void *data, u32 len) { return transfer(file, data, len, false); }
    static int writeFile(void *file, const void *data, u32 len) {
        return transfer(file, const_cast<void *>(data), len, true);
    }
    static long seekFile(void *file, long offset, int whence) {
        const long result = gzseek(static_cast<gzFile>(file), offset, whence);
        if (result < 0) { active_->failed_ = true; diagnostic("gzseek", errno, 0); }
        return result;
    }
    static int closeFile(void *file) {
        auto &io = *active_;
        const int result = gzclose(static_cast<gzFile>(file));
        if (result != Z_OK) { io.failed_ = true; diagnostic("gzclose", errno, result); }
        // Read-only loads/checks must not require any syncing syscall.
        if (io.writing_ && !io.failed_ && !syncFile(SaveFuncs.fd, false)) io.failed_ = true;
        if (close(SaveFuncs.fd) != 0) { io.failed_ = true; diagnostic("close", errno, 0); }
        SaveFuncs.fd = SaveFuncs.lib_fd = -1;
        return io.failed_ ? -1 : 0;
    }
};
