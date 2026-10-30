// LilSmokeUI — ROMScanner.h
//
// Bounded ROM scanning with system-folder aliases and PortSmoke metadata.
// Knows the file extensions for each system (matches lilsmoke-launch routing).

#pragma once

#include <string>
#include <vector>

namespace lilsmoke {

struct ROM {
    std::string system;       // "nes", "snes", "psx", "portsmoke", ...
    std::string name;         // display name
    std::string path;         // absolute ROM path, or PortSmoke .sh script path
    std::string coverPath;    // /persist/scrape/covers/<system>/<name>.(png|webp|jpg) (may not exist)
    std::string description;
    std::string releaseDate;
    std::string developer;
    std::string publisher;
    std::string genre;
    std::string players;
    std::string rating;
};

struct GameEntry {
    std::string name;         // display name from portsmoke.conf
    std::string scriptPath;   // /storage/portsmoke/ports/<game>/<script>.sh
    std::string coverPath;    // /storage/portsmoke/ports/<game>/cover.webp
};

std::vector<GameEntry> scanPorts(const std::string &portsRoot = "/storage/portsmoke/ports");

class ROMScanner {
public:
    ROMScanner();

    // Scan /storage/roms/. Populates roms_.
    // Returns number of ROMs found.
    int scan(const std::string &rootDir = "/storage/roms");

    // Versioned quoted cache: <=4 MiB / 20000 records, thirteen string slots.
    // A failed load clears entries; callers rescan. Port metadata is reread.
    bool saveCache(const std::string &cachePath) const;
    bool loadCache(const std::string &cachePath, const std::string &rootDir);

    const std::vector<ROM> &roms() const { return roms_; }

    // Get ROMs filtered by system. Returns reference into roms_.
    std::vector<const ROM *> bySystem(const std::string &system) const;

    // Get the list of systems that have at least one ROM.
    std::vector<std::string> systems() const;

private:
    std::vector<ROM> roms_;
    std::string scanRoot_;

    // Map system -> list of valid file extensions (lowercase, with dot).
    // Matches what lilsmoke-launch accepts.
    static const std::vector<std::pair<std::string, std::vector<std::string>>> &systemExts();
};

}  // namespace lilsmoke
