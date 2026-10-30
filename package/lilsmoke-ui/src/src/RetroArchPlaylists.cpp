#include "RetroArchPlaylists.h"
#include "Log.h"
#include "EmulatorRegistry.h"
#include "FilePersistence.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace lilsmoke {
namespace {
// RetroArch's JSON playlist schema accepts filenames without database CRCs.
void jsonString(std::ostream &out, const std::string &value) {
    static const char hex[] = "0123456789abcdef";
    out.put('"');
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { out.put('\\'); out.put(c); }
        else if (c < 32) { out << "\\u00" << hex[c >> 4] << hex[c & 15]; }
        else out.put(c);
    }
    out.put('"');
}
}

bool writeRetroArchPlaylists(const std::vector<ROM> &roms, const std::string &directory,
                            const std::string &cores) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(directory, ec);
    if (ec) { LOG_WARN("Playlists: cannot create %s", directory.c_str()); return false; }
    auto providers = emulatorProviders(false);
    for (auto &p : providers) if (p.backend == "retroarch") {
        const auto supplied = fs::path(cores) / p.argument;
        if (fs::is_regular_file(supplied, ec)) { p.path = supplied.string(); p.reason.clear(); }
    }
    const auto systems = configurableEmulatorSystems(providers);
    const char *configPath = std::getenv("LILSMOKE_SYSTEM_CONFIG");
    if (!configPath) configPath = std::getenv("LILSMOKE_CONFIG");
    const auto config = readEmulatorConfig(configPath ? configPath : "/persist/config/lilsmoke.conf");
    bool success = true;
    for (const auto &system : emulatorSystems()) {
        const std::string filename = "LilSmoke-" + system + ".lpl";
        const fs::path final = fs::path(directory) / filename;
        const EmulatorProvider *provider = nullptr;
        const auto preference = config.find("EMULATOR_" + system);
        for (const auto &p : providers) if (p.backend == "retroarch" && p.available() && p.supports(system)) {
            if (!provider) provider = &p;
            if (preference != config.end() && preference->second == p.id) { provider = &p; break; }
        }
        if (!provider || std::find(systems.begin(), systems.end(), system) == systems.end()) {
            // Only our generated playlist is removed; user-created lists remain untouched.
            if(fs::remove(final, ec) && !syncDirectory(final.parent_path()))success=false;
            if(ec)success=false;
            continue;
        }
        const std::string &core = provider->path;
        std::ostringstream out;
        out << "{\n\"version\":\"1.5\",\"default_core_path\":";
        jsonString(out, core); out << ",\"default_core_name\":";
        jsonString(out, provider->name);
        out << ",\"label_display_mode\":0,\"right_thumbnail_mode\":0,\"left_thumbnail_mode\":0,\"sort_mode\":0,\"items\":[\n";
        bool first = true;
        for (const auto &rom : roms) {
            if (rom.system != system) continue;
            if (!first) out << ",\n";
            first = false;
            out << "{\"path\":"; jsonString(out, rom.path);
            out << ",\"label\":"; jsonString(out, rom.name);
            out << ",\"core_path\":"; jsonString(out, core);
            out << ",\"core_name\":"; jsonString(out, provider->name);
            out << ",\"crc32\":\"00000000|crc\",\"db_name\":";
            jsonString(out, filename); out << '}';
        }
        out << "\n]}\n";
        if (!out || !durableReplace(final,out.str())) {
            LOG_WARN("Playlists: cannot safely replace %s errno=%d",final.c_str(),errno);
            success=false;
        }
    }
    return success;
}
}
