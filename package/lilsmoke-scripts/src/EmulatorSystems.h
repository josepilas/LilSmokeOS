#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace lilsmoke {
struct EmulatorSystemDefinition {
    std::string id, name;
    std::vector<std::string> extensions, aliases;
    bool dataProject = false;
    bool expansion = false;
};

inline const std::vector<EmulatorSystemDefinition> &emulatorSystemDefinitions() {
    static const std::vector<EmulatorSystemDefinition> definitions = {
        {"nes", "NES / Famicom", {".nes", ".unif", ".unf", ".fds"}, {"famicom", "fds"}},
        {"snes", "Super Nintendo", {".sfc", ".smc", ".fig", ".bs"}, {"sfc"}},
        {"n64", "Nintendo 64", {".z64", ".v64", ".n64"}, {}},
        {"gb", "Game Boy", {".gb"}, {"gameboy"}},
        {"gbc", "Game Boy Color", {".gbc", ".gb"}, {"gbcolor"}},
        {"gba", "Game Boy Advance", {".gba", ".agb"}, {"gbadvance"}},
        {"md", "Mega Drive", {".md", ".gen", ".smd", ".bin"}, {"genesis", "megadrive"}},
        {"sms", "Master System", {".sms"}, {"mastersystem"}},
        {"gg", "Game Gear", {".gg"}, {"gamegear"}},
        {"pce", "PC Engine", {".pce"}, {"pcengine", "tg16", "turbografx16"}},
        {"arcade", "Arcade", {".zip", ".7z"}, {"fbneo", "mame"}},
        {"lynx", "Atari Lynx", {".lnx", ".lyx"}, {"atarilynx"}},
        {"psx", "PlayStation", {".cue", ".pbp", ".iso", ".img", ".bin", ".ecm", ".chd", ".m3u"}, {"ps1", "playstation"}},
        {"atari2600", "Atari 2600", {".a26", ".bin", ".rom"}, {"a26", "stella"}, false, true},
        {"atari7800", "Atari 7800", {".a78", ".bin"}, {"a78", "prosystem"}, false, true},
        {"atari800", "Atari 800 / 5200", {".atr", ".xfd", ".xex", ".car", ".rom", ".a52", ".bin", ".cas"}, {"atari8bit", "atari5200", "a52"}, false, true},
        {"ngpc", "Neo Geo Pocket", {".ngp", ".ngc", ".ngpc"}, {"ngp", "neogeopocket"}, false, true},
        {"wswan", "WonderSwan", {".ws", ".wsc"}, {"wonderswan", "wonderswancolor"}, false, true},
        {"pokemini", "Pokemon Mini", {".min"}, {"pokemonmini"}, false, true},
        {"doom", "Doom / PrBoom", {".wad"}, {"prboom"}, true, true},
        {"cavestory", "Cave Story / NXEngine", {".exe"}, {"nxengine"}, true, true},
        {"easyrpg", "RPG Maker 2000 / 2003", {".ldb"}, {"rpgmaker", "rpgmaker2000", "rpgmaker2003"}, true, true},
        {"scummvm", "ScummVM", {".scummvm", ".svm"}, {}, true, true},
        {"openbor", "OpenBOR", {".pak"}, {}, true, true}
    };
    return definitions;
}

inline std::string canonicalEmulatorSystem(std::string system) {
    for (char &c : system) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const auto &definition : emulatorSystemDefinitions()) {
        if (system == definition.id || std::find(definition.aliases.begin(), definition.aliases.end(), system) != definition.aliases.end())
            return definition.id;
    }
    return system;
}

inline const EmulatorSystemDefinition *emulatorSystemDefinition(const std::string &id) {
    const auto canonical = canonicalEmulatorSystem(id);
    for (const auto &definition : emulatorSystemDefinitions()) if (definition.id == canonical) return &definition;
    return nullptr;
}

inline const std::vector<std::string> &emulatorSystems() {
    static const std::vector<std::string> systems = [] {
        std::vector<std::string> result;
        for (const auto &definition : emulatorSystemDefinitions()) result.push_back(definition.id);
        return result;
    }();
    return systems;
}

inline std::string emulatorSystemName(const std::string &id) {
    const auto *definition = emulatorSystemDefinition(id);
    return definition ? definition->name : id;
}

inline bool emulatorContentMarker(const std::string &system, std::string filename) {
    for (char &c : filename) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (system == "cavestory") return filename == "doukutsu.exe";
    if (system == "easyrpg") return filename == "rpg_rt.ldb";
    if (system == "doom") return filename != "prboom.wad" && filename != "prboom-plus.wad";
    return true;
}
}
