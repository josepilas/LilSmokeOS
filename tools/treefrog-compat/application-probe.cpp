// SPDX-License-Identifier: MPL-2.0
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include "application.h"

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    try {
        auto port = treefrog::application(argv[1]);
        std::cout << port.id << ' ' << port.launchType << '\n';
        return 0;
    } catch (const std::runtime_error &error) {
        std::cerr << error.what() << '\n';
        return 4;
    }
}
