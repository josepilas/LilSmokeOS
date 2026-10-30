#pragma once
#include "SystemView.h"
#include <algorithm>

namespace lilsmoke {
inline void hideEmptyGameSystems(std::vector<SystemEntry> &entries, bool hide) {
    if (hide) entries.erase(std::remove_if(entries.begin(), entries.end(), [](const SystemEntry &entry) {
        return entry.id.rfind("__", 0) != 0 && entry.gameCount == 0;
    }), entries.end());
}
} // namespace lilsmoke
