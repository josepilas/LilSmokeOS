#include "GameHistory.h"
#include "FilePersistence.h"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <sys/file.h>
#include <time.h>

namespace lilsmoke {
namespace {
bool plain(const std::string &text, size_t limit) {
    return !text.empty() && text.size() <= limit &&
        std::none_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
bool directory(const std::string &path) {
    struct stat st{};
    return lstat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
uint64_t addSaturated(uint64_t a, uint64_t b) {
    return b > UINT64_MAX - a ? UINT64_MAX : a + b;
}
bool unsignedNumber(std::istream &in, uint64_t &value) {
    std::string token;
    if (!(in >> token) || token.empty() || token.size() > 20) return false;
    value = 0;
    for (char c : token) {
        if (c < '0' || c > '9' || value > (UINT64_MAX - (c - '0')) / 10) return false;
        value = value * 10 + (c - '0');
    }
    return true;
}
}

GameHistory::GameHistory(std::string storage) : storage_(std::move(storage)) {}

bool GameHistory::available() const {
    if (!directory(storage_)) return false;
    if (storage_ == "/storage") {
        struct stat root{}, storage{}, marker{};
        // Never create history in an unmounted /storage directory on LILOS.
        if (stat("/", &root) || stat("/storage", &storage) || root.st_dev == storage.st_dev ||
            lstat("/run/lilsmoke-storage/exporting", &marker) == 0) return false;
    }
    struct stat st{};
    const std::string dir = storage_ + "/.lilsmoke";
    return lstat(dir.c_str(), &st) == 0 ? S_ISDIR(st.st_mode) : errno == ENOENT;
}

bool GameHistory::validGame(const RecentGame &game) {
    return plain(game.system, 32) && plain(game.path, 1024) && game.path.front() == '/' &&
        plain(game.name, 256) && std::all_of(game.system.begin(), game.system.end(),
            [](unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; });
}

bool GameHistory::load() {
    entries_.clear();
    startInRecents_ = false;
    if (!available()) return false;
    const std::string path = storage_ + "/.lilsmoke/recent-games.v1";
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return errno == ENOENT;
    struct stat st{};
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        static_cast<uint64_t>(st.st_size) > MaxBytes) { close(fd); return false; }
    std::string data;
    char buffer[4096];
    ssize_t n;
    while ((n = read(fd, buffer, sizeof(buffer))) != 0) {
        if (n < 0) { if (errno == EINTR) continue; close(fd); return false; }
        if (data.size() + static_cast<size_t>(n) > MaxBytes) { close(fd); return false; }
        data.append(buffer, static_cast<size_t>(n));
    }
    close(fd);
    std::istringstream in(data);
    std::string magic, preference;
    if (!(in >> magic >> preference) || magic != "LILSMOKE_RECENTS_1" ||
        (preference != "0" && preference != "1")) return false;
    std::vector<RecentGame> parsed;
    while (in >> std::ws && !in.eof()) {
        RecentGame game;
        if (parsed.size() >= MaxEntries || !unsignedNumber(in, game.totalMs) ||
            !unsignedNumber(in, game.lastMs) || !unsignedNumber(in, game.sessions) ||
            !(in >> std::quoted(game.system) >> std::quoted(game.path) >> std::quoted(game.name)) ||
            !validGame(game) || game.sessions == 0 || game.lastMs > game.totalMs) return false;
        for (const auto &other : parsed)
            if (other.system == game.system && other.path == game.path) return false;
        parsed.push_back(std::move(game));
    }
    entries_ = std::move(parsed);
    startInRecents_ = preference == "1";
    return true;
}

bool GameHistory::save() const {
    std::ostringstream out;
    out << "LILSMOKE_RECENTS_1 " << (startInRecents_ ? 1 : 0) << '\n';
    for (const auto &game : entries_)
        out << game.totalMs << ' ' << game.lastMs << ' ' << game.sessions << ' '
            << std::quoted(game.system) << ' ' << std::quoted(game.path) << ' '
            << std::quoted(game.name) << '\n';
    const std::string data = out.str();
    return data.size() <= MaxBytes && available() &&
        durableReplace(storage_ + "/.lilsmoke/recent-games.v1", data);
}

bool GameHistory::update(const RecentGame *game, uint64_t elapsedMs, const bool *start) {
    if (!available() || (game && !validGame(*game))) return false;
    const std::string dir = storage_ + "/.lilsmoke";
    if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) return false;
    if (!directory(dir)) return false;
    int lock = open((dir + "/recent-games.lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    if (lock < 0) return false;
    struct stat st{};
    if (fstat(lock, &st) || !S_ISREG(st.st_mode) || flock(lock, LOCK_EX | LOCK_NB)) { close(lock); return false; }
    // Reload under the lock so another completed session cannot be lost.
    // Corrupt/oversized metadata is replaced only by an explicit mutation.
    load();
    const auto previous = entries_;
    const bool previousStart = startInRecents_;
    if (start) startInRecents_ = *start;
    if (game) {
        RecentGame latest = *game;
        auto it = std::find_if(entries_.begin(), entries_.end(), [&](const auto &entry) {
            return entry.system == game->system && entry.path == game->path;
        });
        latest.totalMs = addSaturated(it == entries_.end() ? 0 : it->totalMs, elapsedMs);
        latest.lastMs = elapsedMs;
        latest.sessions = addSaturated(it == entries_.end() ? 0 : it->sessions, 1);
        if (it != entries_.end()) entries_.erase(it);
        entries_.insert(entries_.begin(), std::move(latest));
        if (entries_.size() > MaxEntries) entries_.resize(MaxEntries);
    }
    const bool ok = save();
    if (!ok) { entries_ = previous; startInRecents_ = previousStart; }
    close(lock);
    return ok;
}

bool GameHistory::record(const RecentGame &game, uint64_t elapsedMs) { return update(&game, elapsedMs, nullptr); }
bool GameHistory::setStartInRecents(bool enabled) { return update(nullptr, 0, &enabled); }

std::string sessionTime(uint64_t ms) {
    const uint64_t seconds = ms / 1000;
    if (seconds < 60) return std::to_string(seconds) + "s";
    if (seconds < 3600) return std::to_string(seconds / 60) + "m " + std::to_string(seconds % 60) + "s";
    return std::to_string(seconds / 3600) + "h " + std::to_string(seconds / 60 % 60) + "m";
}

uint64_t monotonicMilliseconds() {
    timespec time{};
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0) return 0;
    return static_cast<uint64_t>(time.tv_sec) * 1000 + time.tv_nsec / 1000000;
}
} // namespace lilsmoke
