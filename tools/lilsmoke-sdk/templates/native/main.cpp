// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Jose Pilas.
#include <lilsmoke/app.h>
#include <iostream>

int main() {
    char path[8192];
    const char *id = lilsmoke_app_id();
    const char *name = lilsmoke_app_name();
    if (!id || !name || !lilsmoke_app_directory()) {
        std::cerr << "Launch this example through LilSmoke Applications.\n";
        return 2;
    }
    std::cout << "SDK " << LILSMOKE_SDK_VERSION << ": " << name << " [" << id << "]\n";
    const int result = lilsmoke_open_rom_path(path, sizeof path);
    if (result < 0) { std::cerr << "Invalid Open Core environment\n"; return 2; }
    if (!result) std::cout << "Logical ROM path: " << path << '\n';
    else std::cout << "Normal application launch; no ROM arguments.\n";
    // Deliberately CPU-only, log-producing and short-lived; not an emulator.
    return 0;
}
