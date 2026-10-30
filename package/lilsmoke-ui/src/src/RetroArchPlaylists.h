#pragma once
#include "ROMScanner.h"
#include <string>
#include <vector>
namespace lilsmoke {
bool writeRetroArchPlaylists(const std::vector<ROM> &roms,
                            const std::string &directory = "/persist/playlists",
                            const std::string &cores = "/usr/lib/libretro");
}
