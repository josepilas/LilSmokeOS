#include "runtime_assets.hpp"
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <sys/resource.h>

using namespace portsmoke::sm64;
namespace {
#include "asset_manifest.inc"
constexpr GameDefinition definition{"9bef1128717f958171a4afac3ed78ee2bb4e86ce", 8388608,
                                    manifestIdentity, assetSpecs, sizeof(assetSpecs) / sizeof(assetSpecs[0])};
void reportMemory() {
    if (std::getenv("PORTSMOKE_SM64_DIAGNOSTICS")) {
        rusage usage{};
        if (getrusage(RUSAGE_SELF, &usage) == 0) std::cerr << "ExtractorPeakRSSKB=" << usage.ru_maxrss << '\n';
    }
}
}

int main(int argc, char **argv) {
    try {
        if (argc == 4 && std::string(argv[1]) == "--original") {
            auto rom = discoverRom(argv[2], definition);
            bool extracted = prepare(rom, argv[3], definition);
            std::cout << (extracted ? "Extracted " : "Reused verified cache: ") << definition.count << " private US assets\n";
            reportMemory();
            return 0;
        }
        if (argc != 3) {
            std::cerr << "Usage: portsmoke-sm64-data ROM OUTPUT.pdat\n"
                         "       portsmoke-sm64-data --verify OUTPUT.pdat\n"
                         "       portsmoke-sm64-data --texture-checksums OUTPUT.pdat\n"
                         "       portsmoke-sm64-data --original ORIGINAL_DIRECTORY OUTPUT.pdat\n"
                         "Development data tool only; no native gameplay engine.\n";
            return 64;
        }
        if (std::string(argv[1]) == "--verify" || std::string(argv[1]) == "--texture-checksums") {
            RuntimeAssets assets(argv[2], definition);
            if (std::string(argv[1]) == "--texture-checksums") {
                std::size_t count = 0;
                for (const auto& entry : assets.pack().entries()) {
                    if (entry.format < 1 || entry.format > 5) continue;
                    auto texture = assets.texture(entry.name);
                    std::cout << "TEXTURE\t" << entry.name << '\t' << sha1(texture.rgba) << '\n';
                    ++count;
                }
                std::cout << "Development diagnostics: " << count << " linear textures checked; no gameplay\n";
            } else {
                std::cout << "Verified " << assets.pack().entries().size() << " private US assets\n";
            }
            reportMemory();
            return 0;
        }
        bool extracted = prepare(argv[1], argv[2], definition);
        std::cout << (extracted ? "Extracted " : "Reused verified cache: ") << definition.count << " private US assets\n";
        reportMemory();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "SM64 BYO-Data: " << error.what() << '\n';
        return 1;
    }
}
