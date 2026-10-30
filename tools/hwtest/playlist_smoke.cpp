#include "RetroArchPlaylists.h"
#include <cassert>
#include <filesystem>
#include <vector>
int main(int argc, char **argv) {
    assert(argc == 2);
    std::vector<lilsmoke::ROM> games(2);
    games[0].system = "gba"; games[0].name = "Quotes \"and\" newline\n";
    games[0].path = "/storage/roms/gba/test.zip";
    games[1].system = "md"; games[1].name = "Sonic";
    games[1].path = "/storage/roms/md/test.7z";
    assert(lilsmoke::writeRetroArchPlaylists(games, argv[1], "/usr/lib/libretro"));
    assert(std::filesystem::is_regular_file(std::filesystem::path(argv[1]) / "LilSmoke-gba.lpl"));
    return 0;
}
