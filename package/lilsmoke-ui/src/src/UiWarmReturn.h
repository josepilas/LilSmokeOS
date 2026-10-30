#pragma once

#include "FilePersistence.h"
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace lilsmoke {
inline constexpr const char *uiAppHandoff = "/run/lilsmoke-ui-app-handoff";
inline constexpr const char *uiLibraryDirty = "/run/lilsmoke-ui-library-dirty";
inline constexpr const char *uiPlaylistState = "/run/lilsmoke-ui-playlists.state";

inline bool warmReturnMayLoadCache(bool force, bool scanOnBoot, bool warm, bool dirty) {
    return !force && !dirty && (warm || !scanOnBoot);
}

// These values are supplied afresh by the stock supervisor, never by config or SD.
// Consume them before any child is launched, including metadata helpers.
struct UiWarmReturn {
    std::string token;
    bool warm = false;
    UiWarmReturn() {
        const char *owner = std::getenv("LILSMOKE_STOCK_SUPERVISOR");
        const char *generation = std::getenv("LILSMOKE_UI_SUPERVISOR_TOKEN");
        const char *hint = std::getenv("LILSMOKE_UI_WARM_RETURN");
        if (owner && std::string(owner) == "1" && generation) {
            std::string value(generation);
            const auto colon = value.find(':');
            if (value.size() <= 64 && colon != std::string::npos && colon > 0 &&
                colon + 1 < value.size() && value.find(':', colon + 1) == std::string::npos &&
                value.find_first_not_of("0123456789:") == std::string::npos) token = value;
        }
        warm = !token.empty() && hint && std::string(hint) == "1";
        unsetenv("LILSMOKE_UI_WARM_RETURN");
        unsetenv("LILSMOKE_UI_SUPERVISOR_TOKEN");
    }
    bool recordHandoff(const std::string &path = uiAppHandoff) const {
        if (token.empty()) return false;
        const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd < 0) return false;
        const std::string receipt = token + "\n";
        const bool ok = write(fd, receipt.data(), receipt.size()) == static_cast<ssize_t>(receipt.size());
        const bool closed = close(fd) == 0;
        if (!ok || !closed) unlink(path.c_str());
        return ok && closed;
    }
};

// A bounded metadata witness avoids rewriting generated playlists on an unchanged
// warm return. ROMScanner still validates every cached path; this is not a ROM scan.
class UiPlaylistSnapshot {
    std::ostringstream out_;
    bool valid_ = true;
public:
    UiPlaylistSnapshot() { out_ << "LILSMOKE_UI_PLAYLIST_STATE_1\n"; }
    void value(const std::string &value) { out_ << std::quoted(value) << '\n'; }
    void file(const std::string &path) {
        value(path);
        struct stat st;
        if (stat(path.c_str(), &st) != 0) {
            if (errno != ENOENT && errno != ENOTDIR) valid_ = false;
            out_ << "missing\n";
            return;
        }
        out_ << st.st_dev << ' ' << st.st_ino << ' ' << st.st_mode << ' ' << st.st_size << ' '
             << st.st_mtim.tv_sec << ' ' << st.st_mtim.tv_nsec << ' '
             << st.st_ctim.tv_sec << ' ' << st.st_ctim.tv_nsec << '\n';
    }
    std::string data() const {
        const auto data = out_.str();
        return valid_ && data.size() <= 64 * 1024 ? data : std::string();
    }
};

inline bool unchangedUiPlaylists(const std::string &snapshot, const std::string &path = uiPlaylistState) {
    if (snapshot.empty()) return false;
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return false;
    struct stat st;
    bool ok = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) &&
        st.st_size == static_cast<off_t>(snapshot.size());
    std::string previous(snapshot.size(), '\0');
    size_t offset = 0;
    while (ok && offset < previous.size()) {
        const ssize_t count = read(fd, &previous[offset], previous.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { ok = false; break; }
        offset += static_cast<size_t>(count);
    }
    char extra;
    if (ok && read(fd, &extra, 1) != 0) ok = false;
    close(fd);
    return ok && previous == snapshot;
}
} // namespace lilsmoke
