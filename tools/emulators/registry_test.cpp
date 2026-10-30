#include "EmulatorRegistry.h"
#include <cassert>
#include <iostream>

int main(int argc, char **argv) {
    using namespace lilsmoke;
    if (argc == 2 && std::string(argv[1]) == "spam") {
        for (int i = 0; i < 20000; ++i) std::cout << "012345678901234567890123456789012345678901234567890123456789\n";
        return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "stall") {
        close(STDOUT_FILENO); sleep(30); return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "scan") {
        std::cout << "safe\tTest App\tgb,gbc\t\t1\n"
                     "../escape\tBad App\tsnes\t\t1\n"
                     "gpu\tGPU App\t\tRequires OpenGL\t0\n"
                     "invalid\tBad System\tanything\t\t1\n";
        return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "emulators") {
        std::cout << "org.test.emulator\tMulti System\tgba,n64\t1.2.0\n"
                     "../escape\tBad App\tgba\t1\n"
                     "unknown\tUnknown\tmissing\t1\n";
        return 0;
    }
    const auto root = std::filesystem::temp_directory_path() / ("lilsmoke-registry-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    setenv("LILSMOKE_CORES_DIR", root.c_str(), 1);
    setenv("LILSMOKE_OD_EMULATORS_DIR", root.c_str(), 1);
    setenv("LILSMOKE_EXTRA_CORES_DIR", root.c_str(), 1);
    setenv("LILSMOKE_EXTRA_OD_DIR", root.c_str(), 1);
    setenv("LILSMOKE_OPENDINGUX_HELPER", argv[0], 1);
    setenv("LILSMOKE_APPS_HELPER", argv[0], 1);
    auto providers = emulatorProviders(false);
    for (const auto &p : providers) assert(!p.available() || p.id == "od:pcsx4all");
    std::ofstream(root / "fceumm_libretro.so") << "fixture";
    std::ofstream(root / "mgba_libretro.so") << "fixture";
    std::ofstream(root / "snes9x_2005_libretro.so") << "fixture";
    std::ofstream(root / "pocketsnes") << "fixture";
    std::filesystem::permissions(root / "pocketsnes", std::filesystem::perms::owner_all);
    providers = emulatorProviders(false);
    assert(configurableEmulatorSystems(providers).size() == 13);
    std::ofstream(root / "stella2014_libretro.so") << "fixture";
    providers = emulatorProviders(false);
    assert(configurableEmulatorSystems(providers).size() == 13);
    std::ofstream(root / "stella") << "fixture";
    std::filesystem::permissions(root / "stella", std::filesystem::perms::owner_all);
    providers = emulatorProviders(false);
    const auto paired = configurableEmulatorSystems(providers);
    assert(paired.size() == 14 && paired.back() == "atari2600");
    assert(canonicalEmulatorSystem("a26") == "atari2600");
    assert(emulatorContentMarker("cavestory", "Doukutsu.exe"));
    assert(!emulatorContentMarker("cavestory", "unrelated.exe"));
    assert(emulatorContentMarker("easyrpg", "RPG_RT.ldb"));
    assert(!emulatorContentMarker("doom", "prboom.wad"));
    std::map<std::string, std::string> config;
    assert(selectedEmulator("nes", config, providers) == "ra:fceumm");
    config["SNES_EMULATOR"] = "pocketsnes";
    assert(selectedEmulator("snes", config, providers) == "od:pocketsnes");
    config["EMULATOR_BACKEND"] = "retroarch";
    assert(selectedEmulator("snes", config, providers) == "ra:snes9x2005");
    config["EMULATOR_BACKEND"] = "opendingux";
    assert(selectedEmulator("snes", config, providers) == "od:pocketsnes");
    assert(selectedEmulator("gba", config, providers).empty());
    config["EMULATOR_gba"] = "ra:mgba";
    assert(selectedEmulator("gba", config, providers) == "ra:mgba");
    config["EMULATOR_gba"] = "";
    assert(selectedEmulator("gba", config, providers).empty());
    std::ofstream(root / "mgba") << "fixture";
    std::filesystem::permissions(root / "mgba", std::filesystem::perms::owner_all);
    std::ofstream(root / "openbor") << "fixture";
    std::filesystem::permissions(root / "openbor", std::filesystem::perms::owner_all);
    providers = emulatorProviders(false);
    assert(selectedEmulator("gba", config, providers) == "od:mgba");
    config["EMULATOR_BACKEND"] = "retroarch";
    assert(selectedEmulator("openbor", config, providers) == "od:openbor");
    const auto withNativeOnly = configurableEmulatorSystems(providers);
    assert(std::find(withNativeOnly.begin(),withNativeOnly.end(),"openbor") != withNativeOnly.end());
    assert(canonicalEmulatorSystem("MEGADRIVE") == "md");
    assert(canonicalEmulatorSystem("sfc") == "snes");
    assert(!emulatorToken("../bad") && !emulatorToken("a;exit") && emulatorToken("safe_app-1"));
    config["EMULATOR_psx"]="od:pcsx4all";
    assert(selectedEmulator("psx",config,providers)=="app:org.lilsmoke.pcsx4all");
    for(const auto &p:providers)assert(p.id!="od:pcsx4all");
    std::ofstream(root / "config") << " # ignored\n EMULATOR_BACKEND = \"retroarch\" # note\n"
        "EMULATOR_snes=\"od:pocketsnes\"\nEMULATOR_BACKEND='opendingux'\n";
    const auto parsed = readEmulatorConfig((root / "config").string());
    assert(parsed.at("EMULATOR_BACKEND") == "opendingux" && parsed.at("EMULATOR_snes") == "od:pocketsnes");
    if (argc == 1) {
        const auto scanned = emulatorProviders();
        assert(scanned.size() == providers.size() + 3);
        assert(scanned[scanned.size() - 3].id == "opk:safe");
        assert(scanned[scanned.size() - 2].reason == "Requires OpenGL" && !scanned[scanned.size() - 2].needsRom);
        assert(scanned[scanned.size() - 3].supports("gb") && !scanned[scanned.size() - 3].supports("gba"));
        const auto &app = scanned.back();
        assert(app.id == "app:org.test.emulator" && app.backend == "applications");
        assert(app.supports("gba") && app.supports("n64") && !app.supports("snes"));
        config["EMULATOR_gba"] = app.id;
        assert(selectedEmulator("gba", config, scanned) == app.id);
        assert(selectedEmulator("gba", config, providers) == app.id); // Missing explicit choices fail; never silently change engines.
        assert(readEmulatorHelper(argv[0], {"spam"}, 8192, 2).empty());
        const auto before = std::chrono::steady_clock::now();
        assert(readEmulatorHelper(argv[0], {"stall"}, 8192, 1).empty());
        assert(std::chrono::steady_clock::now() - before < std::chrono::seconds(3));
    }
    std::filesystem::remove_all(root);
    std::cout << "PASS emulator registry: global policy, overrides, legacy migration, installed filtering, bounded catalog, aliases\n";
}
