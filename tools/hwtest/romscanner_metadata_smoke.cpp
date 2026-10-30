#include "ROMScanner.h"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

int main() {
    const fs::path root = fs::temp_directory_path() / "lilsmoke-romscanner-smoke";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "snes" / "media", ec);
    if (ec) return 2;

    std::ofstream(root / "snes" / "legal-test.7z") << "test";
    std::ofstream(root / "snes" / "media" / "cover.webp") << "webp";
    std::ofstream gamelist(root / "snes" / "gamelist.xml");
    gamelist << R"XML(<?xml version="1.0"?>
<gameList>
  <game>
    <path>./legal-test.7z</path>
    <name>Super &amp; Test</name>
    <desc>A legal test entry.</desc>
    <image>./media/cover.webp</image>
    <releasedate>19901121000000</releasedate>
    <developer>Nintendo</developer>
    <publisher>Nintendo</publisher>
    <genre>Platform</genre>
    <players>1-2</players>
    <rating>0.9</rating>
  </game>
</gameList>)XML";
    gamelist.close();

    lilsmoke::ROMScanner scanner;
    if (scanner.scan(root.string()) != 1) return 3;
    const lilsmoke::ROM &rom = scanner.roms().front();
    const bool ok = rom.system == "snes" && rom.name == "Super & Test" &&
                    rom.description == "A legal test entry." &&
                    rom.releaseDate == "1990-11-21" &&
                    rom.developer == "Nintendo" && rom.publisher == "Nintendo" &&
                    rom.genre == "Platform" && rom.players == "1-2" &&
                    rom.rating == "0.9" &&
                    fs::path(rom.coverPath).filename() == "cover.webp";
    fs::remove_all(root, ec);
    if (!ok) {
        std::cerr << "ROM metadata mismatch\n";
        return 4;
    }
    std::cout << "ROMSCANNER_METADATA_OK\n";
    return 0;
}
