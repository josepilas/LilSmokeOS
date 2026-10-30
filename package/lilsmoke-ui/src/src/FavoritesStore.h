#pragma once
#include "ROMScanner.h"
#include <string>
#include <vector>

namespace lilsmoke {
class FavoritesStore {
public:
    static constexpr size_t MaxEntries = 128;
    static constexpr size_t MaxBytes = 384 * 1024;
    explicit FavoritesStore(std::string storage = "/storage") : storage_(std::move(storage)) {}
    bool load();
    bool contains(const ROM &rom) const;
    bool setFavorite(const ROM &rom, bool enabled);
    bool setHideEmptySystems(bool enabled);
    bool hideEmptySystems() const { return hideEmpty_; }
    const std::vector<ROM> &entries() const { return entries_; }
    const std::string &error() const { return error_; }
    static bool valid(const ROM &rom);
    static bool present(const ROM &rom);
private:
    std::string storage_, error_;
    std::vector<ROM> entries_;
    bool hideEmpty_ = true;
    bool update(const ROM *rom, bool enabled, const bool *hide);
    bool save();
};
} // namespace lilsmoke
