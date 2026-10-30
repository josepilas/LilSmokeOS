#pragma once
#include "EmulatorSystems.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace lilsmoke {

struct EmulatorProvider {
    std::string id, backend, systems, name, path, argument, reason;
    bool needsRom = true;
    bool available() const { return reason.empty(); }
    bool supports(const std::string &system) const {
        return ("," + systems + ",").find("," + system + ",") != std::string::npos;
    }
};

inline bool emulatorToken(const std::string &s) {
    return !s.empty() && s.size() <= 128 && s.find("..") == std::string::npos &&
        std::all_of(s.begin(), s.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '_' || c == '-' || c == '.';
        });
}

inline std::vector<std::string> emulatorFields(const std::string &s) {
    std::vector<std::string> fields;
    size_t begin = 0, end;
    do {
        end = s.find('\t', begin);
        fields.push_back(s.substr(begin, end == std::string::npos ? end : end - begin));
        begin = end == std::string::npos ? s.size() : end + 1;
    } while (end != std::string::npos);
    return fields;
}

// Metadata helpers are separate processes: no SDL/desktop code is loaded into the UI.
inline std::string readEmulatorHelper(const std::string &helper, const std::vector<std::string> &args,
                                      size_t maxBytes, unsigned seconds, bool quiet = true) {
    if (access(helper.c_str(), X_OK) != 0) return {};
    int fds[2];
    if (pipe(fds) != 0) return {};
    const pid_t child = fork();
    if (child == 0) {
        dup2(fds[1], STDOUT_FILENO);
        const int null = open("/dev/null", O_RDWR);
        if (null >= 0) { dup2(null, STDIN_FILENO); if (quiet) dup2(null, STDERR_FILENO); close(null); }
        close(fds[0]); close(fds[1]);
        std::vector<char *> argv{const_cast<char *>(helper.c_str())};
        for (const auto &arg : args) argv.push_back(const_cast<char *>(arg.c_str()));
        argv.push_back(nullptr);
        execv(helper.c_str(), argv.data());
        _exit(127);
    }
    close(fds[1]);
    if (child < 0) { close(fds[0]); return {}; }
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    std::string out;
    bool done = false, valid = true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (!done && std::chrono::steady_clock::now() < deadline) {
        pollfd fd{fds[0], POLLIN | POLLHUP, 0};
        poll(&fd, 1, 100);
        char buffer[4096];
        const ssize_t n = read(fds[0], buffer, sizeof(buffer));
        if (n > 0) {
            if (out.size() + static_cast<size_t>(n) > maxBytes) { valid = false; break; }
            out.append(buffer, static_cast<size_t>(n));
        } else if (n == 0) done = true;
        else if (errno != EINTR && errno != EAGAIN) { valid = false; break; }
    }
    close(fds[0]);
    if (!done || !valid) kill(child, SIGKILL);
    int status = 0;
    pid_t reaped = 0;
    while ((reaped = waitpid(child, &status, WNOHANG)) == 0 && std::chrono::steady_clock::now() < deadline)
        poll(nullptr, 0, 10);
    if (reaped != child) {
        valid = false;
        kill(child, SIGKILL);
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    }
    return done && valid && WIFEXITED(status) && WEXITSTATUS(status) == 0 ? out : std::string();
}

inline std::string readOpenDinguxCatalog(const std::string &helper) {
    return readEmulatorHelper(helper, {"scan"}, 128 * 1024, 8);
}

inline std::vector<EmulatorProvider> emulatorProviders(bool includePackages = true) {
    const char *coresEnv = std::getenv("LILSMOKE_CORES_DIR");
    const std::string cores = coresEnv ? coresEnv : "/usr/lib/libretro";
    const char *odEnv = std::getenv("LILSMOKE_OD_EMULATORS_DIR");
    const std::string od = odEnv ? odEnv : "/usr/lib/lilsmoke/od-emulators";
    const char *extraCoresEnv = std::getenv("LILSMOKE_EXTRA_CORES_DIR");
    const std::string extraCores = extraCoresEnv ? extraCoresEnv : "/storage/emulators/libretro";
    const char *extraOdEnv = std::getenv("LILSMOKE_EXTRA_OD_DIR");
    const std::string extraOd = extraOdEnv ? extraOdEnv : "/storage/emulators/opendingux";
    std::vector<EmulatorProvider> result;
    const auto core = [&](const char *id, const char *systems, const char *name, const char *file) {
        std::error_code ec;
        const std::string path = std::filesystem::is_regular_file(cores + "/" + file, ec) ? cores + "/" + file : extraCores + "/" + file;
        result.push_back({std::string("ra:") + id, "retroarch", systems, name, path, file, ""});
    };
    core("fceumm", "nes", "FCEUmm", "fceumm_libretro.so");
    core("gambatte", "gb,gbc", "Gambatte", "gambatte_libretro.so");
    core("mgba", "gba", "mGBA", "mgba_libretro.so");
    core("snes9x2005", "snes", "Snes9x 2005", "snes9x_2005_libretro.so");
    core("genesis_plus_gx", "md,sms,gg", "Genesis Plus GX", "genesis_plus_gx_libretro.so");
    core("pce_fast", "pce", "Mednafen PCE Fast", "mednafen_pce_fast_libretro.so");
    core("handy", "lynx", "Handy", "handy_libretro.so");
    core("fbneo", "arcade", "FinalBurn Neo", "fbneo_libretro.so");
    core("parallel_n64", "n64", "ParaLLEl N64 (software)", "parallel_n64_libretro.so");
    core("pcsx_rearmed", "psx", "PCSX ReARMed", "pcsx_rearmed_libretro.so");
    core("quicknes", "nes", "QuickNES", "quicknes_libretro.so");
    core("snes9x2002", "snes", "Snes9x 2002", "snes9x_2002_libretro.so");
    core("picodrive", "md,sms,gg", "PicoDrive", "picodrive_libretro.so");
    core("gpsp", "gba", "gpSP (MIPS dynarec)", "gpsp_libretro.so");
    core("stella2014", "atari2600", "Stella 2014", "stella2014_libretro.so");
    core("prosystem", "atari7800", "ProSystem", "prosystem_libretro.so");
    core("atari800", "atari800", "Atari800", "atari800_libretro.so");
    core("race", "ngpc", "RACE", "race_libretro.so");
    core("pokemini", "pokemini", "PokeMini", "pokemini_libretro.so");
    core("wswan", "wswan", "Beetle WonderSwan", "mednafen_wswan_libretro.so");
    core("nxengine", "cavestory", "NXEngine", "nxengine_libretro.so");
    core("prboom", "doom", "PrBoom", "prboom_libretro.so");
    core("easyrpg", "easyrpg", "EasyRPG", "easyrpg_libretro.so");
    core("scummvm", "scummvm", "ScummVM (2D)", "scummvm_libretro.so");
    result.push_back({"od:pocketsnes", "opendingux", "snes", "PocketSNES", od + "/pocketsnes", "pocketsnes", ""});
    const auto native = [&](const char *id, const char *systems, const char *name) {
        std::error_code ec;
        const std::string path = std::filesystem::is_regular_file(od + "/" + id, ec) ? od + "/" + id : extraOd + "/" + id;
        result.push_back({std::string("od:") + id, "opendingux", systems, name, path, id, ""});
    };
    native("picodrive", "md,sms,gg", "PicoDrive");
    native("regba", "gba", "ReGBA");
    native("mgba", "gba", "mGBA (Native)");
    native("fceu320", "nes", "FCEUX");
    native("gnuboy", "gb,gbc", "Gnuboy");
    native("gambatte", "gb,gbc", "Gambatte");
    native("temper", "pce", "Temper");
    native("handy", "lynx", "Handy");
    native("mame4all", "arcade", "MAME4ALL (0.37b5)");
    native("fba", "arcade", "FBA SDL");
    native("stella", "atari2600", "Stella");
    native("prosystem", "atari7800", "ProSystem");
    native("atari800", "atari800", "Atari800");
    native("race", "ngpc", "RACE");
    native("oswan", "wswan", "Oswan");
    native("pokemini", "pokemini", "PokeMini");
    native("nxengine", "cavestory", "NXEngine");
    native("prboom", "doom", "PrBoom");
    native("easyrpg", "easyrpg", "EasyRPG");
    native("scummvm", "scummvm", "ScummVM (2D)");
    native("openbor", "openbor", "OpenBOR (Legacy 32-bit)");
    for (auto &p : result) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(p.path, ec) ||
            (p.backend == "opendingux" && access(p.path.c_str(), X_OK) != 0)) p.reason = "Not installed";
    }
    if (includePackages) {
        const char *helperEnv = std::getenv("LILSMOKE_OPENDINGUX_HELPER");
        const std::string helper = helperEnv ? helperEnv : "/usr/lib/lilsmoke/lilsmoke-opendingux";
        std::istringstream stream(readOpenDinguxCatalog(helper));
        std::string line;
        while (result.size() < 268 && std::getline(stream, line)) {
            const auto f = emulatorFields(line);
            if (f.size() != 5 || !emulatorToken(f[0]) || f[1].empty() || f[1].size() > 256 ||
                f[3].size() > 512 || (f[4] != "0" && f[4] != "1")) continue;
            if (f[0] == "od-b4f617046bce9188181455925915bbe3b02e2ba87f238e6bf8570eef67fa3cf1") continue;
            bool systemsValid = true;
            std::istringstream systems(f[2]); std::string system;
            while (std::getline(systems, system, ',')) {
                if (std::find(emulatorSystems().begin(), emulatorSystems().end(), system) == emulatorSystems().end())
                    systemsValid = false;
            }
            if (!systemsValid || f[1].find_first_of("\r\n") != std::string::npos) continue;
            result.push_back({"opk:" + f[0], "opendingux", f[2], f[1], helper, f[0], f[3], f[4] == "1"});
        }
        const char *appsEnv = std::getenv("LILSMOKE_APPS_HELPER");
        const std::string appsHelper = appsEnv ? appsEnv : "/usr/lib/lilsmoke/lilsmoke-apps";
        std::istringstream apps(readEmulatorHelper(appsHelper, {"emulators"}, 128 * 1024, 8));
        while (result.size() < 396 && std::getline(apps, line)) {
            const auto f = emulatorFields(line);
            if (f.size() != 4 || !emulatorToken(f[0]) || f[1].empty() || f[1].size() > 256 ||
                f[1].find_first_of("\r\n") != std::string::npos || f[2].empty()) continue;
            bool valid = true; std::istringstream ids(f[2]); std::string id;
            while (std::getline(ids, id, ',')) if (!emulatorSystemDefinition(id) || canonicalEmulatorSystem(id) != id) valid = false;
            if (!valid) continue;
            result.push_back({"app:" + f[0], "applications", f[2], f[1], appsHelper, f[0], ""});
        }
    }
    return result;
}

inline std::vector<std::string> configurableEmulatorSystems(const std::vector<EmulatorProvider> &providers) {
    std::vector<std::string> result;
    for (const auto &definition : emulatorSystemDefinitions()) {
        bool retroarch = false, opendingux = false, application = false;
        for (const auto &p : providers) if (p.available() && p.needsRom && p.supports(definition.id)) {
            retroarch |= p.backend == "retroarch";
            opendingux |= p.backend == "opendingux";
            application |= p.backend == "applications";
        }
        if (!definition.expansion || application || (retroarch && opendingux) ||
            (std::string(definition.id) == "openbor" && opendingux)) result.push_back(definition.id);
    }
    return result;
}

inline std::map<std::string, std::string> readEmulatorConfig(const std::string &path) {
    std::ifstream in(path);
    std::map<std::string, std::string> config;
    std::string line;
    size_t bytes = 0;
    while (std::getline(in, line) && (bytes += line.size()) < 128 * 1024) {
        if (line.empty() || line.front() == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string value = line.substr(eq + 1);
        if (!value.empty() && value.back() == '\r') value.pop_back();
        auto key = line.substr(0, eq);
        const auto trim = [](std::string s) {
            const auto first = s.find_first_not_of(" \t\r\n");
            return first == std::string::npos ? std::string() : s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
        };
        key = trim(key); value = trim(value);
        char quote = 0;
        for (size_t i = 0; i < value.size(); ++i) {
            if (value[i] == '\\' && quote == '"') { ++i; continue; }
            if (value[i] == quote) quote = 0;
            else if (!quote && (value[i] == '\'' || value[i] == '"')) quote = value[i];
            else if (!quote && value[i] == '#' && (!i || std::isspace(static_cast<unsigned char>(value[i - 1])))) {
                value = trim(value.substr(0, i)); break;
            }
        }
        if (value.size() >= 2 && value.front() == value.back() && (value.front() == '\'' || value.front() == '"'))
            value = value.substr(1, value.size() - 2);
        if (!key.empty()) config[key] = value;
    }
    return config;
}

inline std::string selectedEmulator(const std::string &system,
    const std::map<std::string, std::string> &config, const std::vector<EmulatorProvider> &providers) {
    const auto get = [&](const std::string &key) {
        auto it = config.find(key); return it == config.end() ? std::string() : it->second;
    };
    const auto override = get("EMULATOR_" + system);
    if (!override.empty()) return override == "od:pcsx4all" ? "app:org.lilsmoke.pcsx4all" : override;
    auto backend = get("EMULATOR_BACKEND");
    if (system == "psx" && (backend.empty() || backend == "opendingux")) for (const auto &p : providers)
        if (p.id == "app:org.lilsmoke.pcsx4all" && p.available()) return p.id;
    if (backend.empty()) {
        // Preserve the physically tested legacy choices until the user sets a global policy.
        if (system == "snes" && get("SNES_EMULATOR") == "pocketsnes") return "od:pocketsnes";
        backend = "retroarch";
    }
    for (const auto &p : providers)
        if (p.backend == backend && p.supports(system) && p.available()) return p.id;
    // OpenBOR has no libretro implementation. Keep the actual native engine
    // usable without inventing a RetroArch provider or changing other defaults.
    if (system == "openbor") for (const auto &p : providers)
        if (p.backend == "opendingux" && p.supports(system) && p.available()) return p.id;
    return {};
}

} // namespace lilsmoke
