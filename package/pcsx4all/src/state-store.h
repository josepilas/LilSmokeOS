#pragma once

#include "state-io.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <zlib.h>

// Use the engine's serializer directly. The legacy pcsx_* wrappers create a
// second pointer file and silently rewrite the last three filename characters.
int SaveState(const char *);
int LoadState(const char *);
int CheckState(const char *, bool *, bool, uint16_t *);

class PcsxStateStore {
public:
    void init(const std::string &disc) {
        const char *root = std::getenv("LILSMOKE_STATE_ROOT");
        const char *original = std::getenv("LILSMOKE_STATE_CONTENT");
        std::error_code ec;
        auto identity = std::filesystem::weakly_canonical(original && *original ? original : disc, ec).string();
        if (ec) identity = original && *original ? original : disc;
        uint64_t hash = UINT64_C(14695981039346656037);
        for (unsigned char c : identity) hash = (hash ^ c) * UINT64_C(1099511628211);
        char key[17]; std::snprintf(key, sizeof key, "%016llx", static_cast<unsigned long long>(hash));
        directory_ = std::filesystem::path(root && *root ? root : "/persist/states") / "pcsx4all" / key;
    }

    std::filesystem::path path(int slot) const {
        return directory_ / ("slot-" + std::to_string(slot) + ".state");
    }

    bool exists(int slot) const {
        std::error_code ec;
        return std::filesystem::is_regular_file(path(slot), ec);
    }

    bool save(int slot, std::string &message) const {
        std::error_code ec;
        std::filesystem::create_directories(directory_, ec);
        if (ec) {
            PcsxStateIo::diagnostic("mkdir", ec.value(), 0);
            return storageFail(message, "Cannot create state directory", ec.value());
        }
        PcsxStateIo io;
        int error = 0;
        const auto target = path(slot), temporary = temporaryFile(".saving-", error);
        if (temporary.empty()) return storageFail(message, "Cannot create state file", error);
        errno = 0;
        if (!io.engineResult(SaveState(temporary.c_str()), "save", temporary.c_str()) || !valid(temporary)) {
            std::filesystem::remove(temporary, ec);
            return fail(message, "Save failed; see storage log");
        }
        std::filesystem::rename(temporary, target, ec);
        if (ec) {
            const int error = ec.value();
            PcsxStateIo::diagnostic("rename", error, 0);
            std::filesystem::remove(temporary, ec);
            return storageFail(message, "Could not commit saved state", error);
        }
        const int fd = open(directory_.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (fd < 0) PcsxStateIo::diagnostic("open-directory", errno, 0);
        bool durable = fd >= 0 && PcsxStateIo::syncFile(fd, true);
        if (fd >= 0 && close(fd) != 0) { PcsxStateIo::diagnostic("close-directory", errno, 0); durable = false; }
        if (!durable) return fail(message, "State written; sync failed");
        message = "Saved slot " + std::to_string(slot);
        std::fprintf(stderr, "PCSX_STATE_SAVE slot=%d path=%s\n", slot, target.c_str());
        return true;
    }

    bool load(int slot, std::string &message) const {
        const auto target = path(slot);
        if (!exists(slot)) return fail(message, "This state slot is empty");
        PcsxStateIo io;
        if (!valid(target)) return fail(message, "Invalid or damaged saved state");
        // Loading may mutate RAM before reporting failure. Keep an actual engine
        // checkpoint, never overwrite a previous failed-recovery artifact.
        int error = 0;
        const auto recovery = temporaryFile(".recovery-", error);
        if (recovery.empty()) return storageFail(message, "Load cancelled: no recovery file", error);
        errno = 0;
        if (!io.engineResult(SaveState(recovery.c_str()), "checkpoint", recovery.c_str()) || !valid(recovery)) {
            std::error_code ec; std::filesystem::remove(recovery, ec);
            return fail(message, "Load cancelled: checkpoint failed");
        }
        const bool loaded = io.engineResult(LoadState(target.c_str()), "load", target.c_str());
        if (!loaded && !io.engineResult(LoadState(recovery.c_str()), "rollback", recovery.c_str())) {
            message = "Load recovery failed; exit game";
            std::fprintf(stderr, "PCSX_STATE_RECOVERY_FAILED path=%s\n", recovery.c_str());
            return false;
        }
        std::error_code ec; std::filesystem::remove(recovery, ec);
        if (!loaded) return fail(message, "State incompatible; game restored");
        message = "Loaded slot " + std::to_string(slot);
        std::fprintf(stderr, "PCSX_STATE_LOAD slot=%d path=%s\n", slot, target.c_str());
        return true;
    }

private:
    std::filesystem::path directory_;

    std::filesystem::path temporaryFile(const char *prefix, int &error) const {
        error = 0;
        std::string name = (directory_ / (std::string(prefix) + "XXXXXX")).string();
        const int fd = mkstemp(name.data());
        if (fd < 0) { error = errno; PcsxStateIo::diagnostic("mkstemp", error, 0); return {}; }
        if (close(fd) != 0) {
            error = errno;
            PcsxStateIo::diagnostic("close-temporary", error, 0);
            unlink(name.c_str());
            return {};
        }
        return name;
    }

    static bool valid(const std::filesystem::path &target) {
        // Check the complete gzip stream before the engine mutates emulated RAM.
        // A streaming scan avoids another multi-megabyte allocation on 128 MB MIPS.
        gzFile file = gzopen(target.c_str(), "rb");
        if (!file) { PcsxStateIo::diagnostic("validate-open", errno, 0); return false; }
        char buffer[8192]; size_t size = 0; int count;
        while ((count = gzread(file, buffer, sizeof buffer)) > 0) {
            size += static_cast<size_t>(count);
            if (size > 16 * 1024 * 1024) break;
        }
        const int closed = gzclose(file);
        if (count != 0 || closed != Z_OK || size < 3 * 1024 * 1024 ||
            CheckState(target.c_str(), nullptr, false, nullptr) != 0)
        {
            std::fprintf(stderr, "PCSX_STATE_INVALID path=%s bytes=%zu read=%d close=%d errno=%d\n",
                         target.c_str(), size, count, closed, errno);
            return false;
        }
        return true;
    }
    static bool storageFail(std::string &message, const char *fallback, int error) {
        if (error == EROFS) return fail(message, "Storage read-only; repair SD");
        if (error == ENOSPC) return fail(message, "Storage full; free SD space");
        if (error == EACCES || error == EPERM) return fail(message, "Storage permission denied");
        if (error == EIO) return fail(message, "SD I/O error; check card");
        return fail(message, fallback);
    }
    static bool fail(std::string &message, const char *error) {
        message = error;
        std::fprintf(stderr, "PCSX_STATE_ERROR %s\n", error);
        return false;
    }
};
