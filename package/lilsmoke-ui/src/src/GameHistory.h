#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lilsmoke {

struct RecentGame {
    std::string system, path, name;
    uint64_t totalMs = 0, lastMs = 0, sessions = 0;
};

class GameHistory {
public:
    static constexpr size_t MaxEntries = 32;
    static constexpr size_t MaxBytes = 96 * 1024;
    // Explicit roots are for isolated tests; production always uses mounted LILROMS.
    explicit GameHistory(std::string storage = "/storage");
    bool load();
    bool record(const RecentGame &game, uint64_t elapsedMs);
    bool setStartInRecents(bool enabled);
    bool startInRecents() const { return startInRecents_; }
    const std::vector<RecentGame> &entries() const { return entries_; }
    bool available() const;
    static bool validGame(const RecentGame &game);

private:
    std::string storage_;
    bool startInRecents_ = false;
    std::vector<RecentGame> entries_;
    bool update(const RecentGame *game, uint64_t elapsedMs, const bool *start);
    bool save() const;
};

std::string sessionTime(uint64_t milliseconds);
uint64_t monotonicMilliseconds();
int runGameSession(const RecentGame &game, const std::string &executable,
                   const std::vector<std::string> &arguments, GameHistory &history);

} // namespace lilsmoke
