#include "RetroArchPlaylists.h"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

namespace fs = std::filesystem;
static std::string read(const fs::path &p) {
    std::ifstream in(p); return {std::istreambuf_iterator<char>(in), {}};
}
int main() {
    const auto root = fs::temp_directory_path() / ("lilsmoke-playlists-" + std::to_string(getpid()));
    const auto base = root / "os", extra = root / "extra", native = root / "native", lists = root / "lists";
    for (const auto &p : {base, extra, native, lists}) fs::create_directories(p);
    setenv("LILSMOKE_CORES_DIR", base.c_str(), 1);
    setenv("LILSMOKE_EXTRA_CORES_DIR", extra.c_str(), 1);
    setenv("LILSMOKE_EXTRA_OD_DIR", native.c_str(), 1);
    setenv("LILSMOKE_OD_EMULATORS_DIR", native.c_str(), 1);
    const auto config = root / "config";
    setenv("LILSMOKE_CONFIG", config.c_str(), 1);
    std::ofstream(base / "mgba_libretro.so") << "fixture";
    std::ofstream(extra / "gpsp_libretro.so") << "fixture";
    std::ofstream(extra / "prboom_libretro.so") << "fixture";
    std::ofstream(lists / "User.lpl") << "preserve user list";
    std::vector<lilsmoke::ROM> roms(2);
    roms[0].system="gba"; roms[0].name="A quoted \"Game\""; roms[0].path="/storage/roms/gba/A.zip";
    roms[1].system="doom"; roms[1].name="My Doom"; roms[1].path="/storage/roms/doom/Game.wad";
    assert(lilsmoke::writeRetroArchPlaylists(roms, lists.string(), base.string()));
    assert(read(lists / "LilSmoke-gba.lpl").find((base / "mgba_libretro.so").string()) != std::string::npos);
    assert(!fs::exists(lists / "LilSmoke-doom.lpl"));
    std::ofstream(config) << "EMULATOR_gba=ra:gpsp\n";
    std::ofstream(native / "prboom") << "fixture";
    fs::permissions(native / "prboom", fs::perms::owner_all);
    assert(lilsmoke::writeRetroArchPlaylists(roms, lists.string(), base.string()));
    assert(read(lists / "LilSmoke-gba.lpl").find((extra / "gpsp_libretro.so").string()) != std::string::npos);
    assert(read(lists / "LilSmoke-doom.lpl").find("My Doom") != std::string::npos);
    assert(read(lists / "LilSmoke-gba.lpl").find("\\\"Game\\\"") != std::string::npos);
    fs::remove(extra / "prboom_libretro.so");
    assert(lilsmoke::writeRetroArchPlaylists(roms, lists.string(), base.string()));
    assert(!fs::exists(lists / "LilSmoke-doom.lpl"));
    assert(read(lists / "User.lpl") == "preserve user list");
    fs::remove_all(root);
    std::cout << "PASS playlists: optional SD core paths, selected RA overrides, paired-system gating, quoting and user lists preserved\n";
}
