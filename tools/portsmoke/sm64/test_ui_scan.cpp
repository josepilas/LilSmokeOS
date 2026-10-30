#include "ROMScanner.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

namespace fs = std::filesystem;
int main(int argc, char **argv) {
    if (argc != 3) return 64;
    char directory[] = "/tmp/portsmoke-sm64-scan-XXXXXX";
    if (!mkdtemp(directory)) return 1;
    fs::path root(directory), source(argv[1]);
    unsigned checks = 0;
    auto check = [&](bool condition) {
        ++checks;
        if (!condition) throw std::runtime_error("scanner check " + std::to_string(checks) + " failed");
    };
    try {
        fs::create_directories(root / "ports/sm64");
        fs::create_directory(root / "engines");
        fs::copy_file(source / "portsmoke.conf", root / "ports/sm64/portsmoke.conf");
        fs::copy_file(source / "SM64.sh", root / "ports/sm64/SM64.sh");
        auto engine = root / "engines/sm64-byo-dev.peng";
        fs::copy_file(argv[2], engine);
        auto found = lilsmoke::scanPorts((root / "ports").string());
        check(found.size() == 1);
        check(found.front().name == "Super Mario 64 (BYO development)");
        check(found.front().scriptPath == (root / "ports/sm64/SM64.sh").string());
        check(found.front().coverPath == (root / "ports/sm64/cover.webp").string());
        fs::remove(engine);
        check(lilsmoke::scanPorts((root / "ports").string()).empty());
        fs::create_symlink(argv[2], engine);
        check(lilsmoke::scanPorts((root / "ports").string()).empty());
        fs::remove(engine); fs::copy_file(argv[2], engine);
        fs::remove(root / "ports/sm64/SM64.sh");
        check(lilsmoke::scanPorts((root / "ports").string()).empty());
        fs::copy_file(source / "SM64.sh", root / "ports/sm64/SM64.sh");
        std::ofstream(root / "ports/sm64/portsmoke.conf") <<
            "[PortSmoke]\nName=bad\nEngineFile=../sm64-byo-dev.peng\nScript=SM64.sh\n";
        check(lilsmoke::scanPorts((root / "ports").string()).empty());
        fs::remove_all(root);
        std::cout << "PASS " << checks << " SM64 LilSmokeUI scanner checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; fs::remove_all(root); return 1;
    }
}
