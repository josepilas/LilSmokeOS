#include "FavoritesStore.h"
#include "GameHistory.h"
#include "FilePersistence.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <sys/file.h>

namespace lilsmoke {
bool FavoritesStore::valid(const ROM &rom) {
    return GameHistory::validGame({rom.system, rom.path, rom.name}) &&
        std::filesystem::path(rom.path).lexically_normal().string() == rom.path;
}
bool FavoritesStore::present(const ROM &rom) {
    struct stat st{};
    return stat(rom.path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
bool FavoritesStore::contains(const ROM &rom) const {
    return std::any_of(entries_.begin(), entries_.end(), [&](const ROM &entry) {
        return entry.system == rom.system && entry.path == rom.path;
    });
}
bool FavoritesStore::load() {
    entries_.clear(); hideEmpty_ = true; error_.clear();
    if (!GameHistory(storage_).available()) { error_ = "LILROMS unavailable"; return false; }
    int fd = open((storage_ + "/.lilsmoke/favorites.v1").c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
        if (errno == ENOENT) return true;
        error_ = "Could not read Favorites"; return false;
    }
    struct stat st{};
    bool ok = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size >= 0 &&
        static_cast<uint64_t>(st.st_size) <= MaxBytes;
    std::string data;
    char buffer[4096];
    while (ok) {
        const ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n == 0) break;
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 || data.size() + static_cast<size_t>(n) > MaxBytes) { ok = false; break; }
        data.append(buffer, static_cast<size_t>(n));
    }
    close(fd);
    if (!ok) { error_ = "Invalid or oversized Favorites file"; return false; }
    std::istringstream in(data);
    std::string magic, preference, countText;
    if (!(in >> magic >> preference >> countText) || magic != "LILSMOKE_FAVORITES_1" ||
        (preference != "0" && preference != "1") || countText.empty() || countText.size() > 3 ||
        !std::all_of(countText.begin(), countText.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        error_ = "Invalid Favorites header"; return false;
    }
    const size_t count = static_cast<size_t>(std::stoi(countText));
    if (count > MaxEntries) { error_ = "Too many Favorites"; return false; }
    std::vector<ROM> parsed;
    for (size_t i = 0; i < count; ++i) {
        ROM rom;
        for (std::string *field : {&rom.system, &rom.path, &rom.name}) {
            in >> std::ws;
            if (in.peek() != '"' || !(in >> std::quoted(*field))) { error_ = "Invalid Favorites record"; return false; }
        }
        if (!valid(rom) || std::any_of(parsed.begin(), parsed.end(), [&](const ROM &other) {
            return rom.system == other.system && rom.path == other.path;
        })) { error_ = "Invalid or duplicate Favorite"; return false; }
        parsed.push_back(std::move(rom));
    }
    std::string end;
    if (!(in >> end) || end != "END" || !(in >> std::ws).eof()) { error_ = "Incomplete Favorites file"; return false; }
    entries_ = std::move(parsed);
    hideEmpty_ = preference == "1";
    return true;
}
bool FavoritesStore::save() {
    std::ostringstream out;
    out << "LILSMOKE_FAVORITES_1 " << (hideEmpty_ ? 1 : 0) << ' ' << entries_.size() << '\n';
    for (const auto &rom : entries_)
        out << std::quoted(rom.system) << ' ' << std::quoted(rom.path) << ' ' << std::quoted(rom.name) << '\n';
    out << "END\n";
    const std::string data = out.str();
    if (data.size() > MaxBytes || !GameHistory(storage_).available() ||
        !durableReplace(storage_ + "/.lilsmoke/favorites.v1", data)) {
        error_ = "Could not save Favorites on LILROMS"; return false;
    }
    return true;
}
bool FavoritesStore::update(const ROM *rom, bool enabled, const bool *hide) {
    error_.clear();
    if (!GameHistory(storage_).available()) { error_ = "LILROMS unavailable"; return false; }
    if (rom && (!valid(*rom) || (enabled && !present(*rom)))) { error_ = "Game file is missing or invalid"; return false; }
    const std::string dir = storage_ + "/.lilsmoke";
    if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) { error_ = "Favorites storage is not writable"; return false; }
    if (!GameHistory(storage_).available()) { error_ = "Unsafe Favorites directory"; return false; }
    int lock = open((dir + "/favorites.lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    if (lock < 0) { error_ = "Could not lock Favorites"; return false; }
    struct stat st{};
    if (fstat(lock, &st) || !S_ISREG(st.st_mode) || flock(lock, LOCK_EX | LOCK_NB)) {
        close(lock); error_ = "Favorites is busy or unsafe"; return false;
    }
    // A corrupt collection must not be silently replaced by a preference change.
    if (!load()) { close(lock); return false; }
    const auto previous = entries_;
    const bool oldHide = hideEmpty_;
    if (hide) hideEmpty_ = *hide;
    if (rom) {
        auto it = std::find_if(entries_.begin(), entries_.end(), [&](const ROM &entry) {
            return entry.system == rom->system && entry.path == rom->path;
        });
        if (enabled && it == entries_.end()) {
            if (entries_.size() == MaxEntries) { close(lock); error_ = "Favorites is full (128)"; return false; }
            ROM entry; entry.system = rom->system; entry.path = rom->path; entry.name = rom->name;
            entries_.push_back(std::move(entry));
        } else if (!enabled && it != entries_.end()) entries_.erase(it);
    }
    const bool ok = save();
    if (!ok) { entries_ = previous; hideEmpty_ = oldHide; }
    close(lock);
    return ok;
}
bool FavoritesStore::setFavorite(const ROM &rom, bool enabled) { return update(&rom, enabled, nullptr); }
bool FavoritesStore::setHideEmptySystems(bool enabled) { return update(nullptr, false, &enabled); }
} // namespace lilsmoke
