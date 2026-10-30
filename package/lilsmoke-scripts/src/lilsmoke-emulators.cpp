#include "EmulatorRegistry.h"
#include <iostream>
#include <cstring>

static int run(const std::string &path, const std::vector<std::string> &args) {
    std::vector<char *> argv;
    for (const auto &arg : args) argv.push_back(const_cast<char *>(arg.c_str()));
    argv.push_back(nullptr);
    execv(path.c_str(), argv.data());
    std::cerr << "Cannot execute " << path << ": " << std::strerror(errno) << '\n';
    return 127;
}

int main(int argc, char **argv) {
    using namespace lilsmoke;
    if (argc == 3 && std::string(argv[1]) == "menu") {
        if (access("/run/lilsmoke-storage/exporting", F_OK) == 0) { std::cerr << "Eject LILDRIVE first\n"; return 1; }
        const auto providers = emulatorProviders();
        const auto p = std::find_if(providers.begin(), providers.end(), [&](const auto &p) { return p.id == argv[2]; });
        if (p == providers.end() || p->backend != "opendingux" || !p->available()) return 3;
        if (p->id == "od:pocketsnes") return run("/usr/lib/lilsmoke/od-launch", {"od-launch", "pocketsnes"});
        if (p->id.rfind("od:", 0) == 0)
            return run("/usr/lib/lilsmoke/lilsmoke-od-launch", {"lilsmoke-od-launch", p->argument});
        return run(p->path, {"lilsmoke-opendingux", "menu", p->argument});
    }
    const bool explicitProvider = argc == 5 && std::string(argv[1]) == "run-provider";
    if (!explicitProvider && (argc < 3 || (std::string(argv[1]) != "resolve" && std::string(argv[1]) != "run"))) {
        std::cerr << "usage: lilsmoke-emulators resolve SYSTEM | run SYSTEM ROM | run-provider ID SYSTEM ROM\n"; return 2;
    }
    const auto system = canonicalEmulatorSystem(argv[explicitProvider ? 3 : 2]);
    if (std::find(emulatorSystems().begin(), emulatorSystems().end(), system) == emulatorSystems().end()) return 2;
    const char *path = std::getenv("LILSMOKE_SYSTEM_CONFIG");
    if (!path) path = std::getenv("LILSMOKE_CONFIG");
    const auto config = readEmulatorConfig(path ? path : "/persist/config/lilsmoke.conf");
    const auto providers = emulatorProviders();
    const auto id = explicitProvider ? std::string(argv[2]) : selectedEmulator(system, config, providers);
    const auto p = std::find_if(providers.begin(), providers.end(), [&](const auto &p) { return p.id == id; });
    if (p == providers.end() || !p->available() ||
        (!p->supports(system) && !(explicitProvider && p->id.rfind("opk:", 0) == 0 && p->systems.empty()))) {
        std::cerr << "No installed compatible emulator for " << system << " under the selected policy. "
                     "Choose an installed engine in Settings > Emulator Core Settings.\n";
        return 3;
    }
    if (std::string(argv[1]) == "resolve") { std::cout << p->id << '\n'; return 0; }
    if (!explicitProvider && argc != 4) return 2;
    const char *rom = argv[explicitProvider ? 4 : 3];
    if (access(rom, R_OK) != 0) { std::cerr << "ROM missing or unreadable\n"; return 4; }
    if (access("/run/lilsmoke-storage/exporting", F_OK) == 0) { std::cerr << "Eject LILDRIVE first\n"; return 1; }
    if (p->backend == "applications")
        return run(p->path, {"lilsmoke-apps", "run-core", p->argument, system, rom});
    if (p->backend == "retroarch")
        return run("/usr/lib/lilsmoke/lilsmoke-retroarch", {"lilsmoke-retroarch", "run", system, rom, p->path});
    if (p->id == "od:pocketsnes")
        return run("/usr/lib/lilsmoke/od-launch", {"od-launch", "pocketsnes", rom});
    if (p->id.rfind("od:", 0) == 0)
        return run("/usr/lib/lilsmoke/lilsmoke-od-launch", {"lilsmoke-od-launch", p->argument, system, rom});
    const char *resolverEnv = std::getenv("LILSMOKE_ROM_RESOLVER");
    auto resolved = readEmulatorHelper(resolverEnv ? resolverEnv : "/usr/lib/lilsmoke/lilsmoke-rom",
                                      {system, rom}, 4096, 300, false);
    if (!resolved.empty() && resolved.back() == '\n') resolved.pop_back();
    if (resolved.empty() || resolved.find_first_of("\r\n") != std::string::npos || access(resolved.c_str(), R_OK) != 0) {
        std::cerr << "Cannot resolve OpenDingux ROM content\n"; return 4;
    }
    return run(p->path, {"lilsmoke-opendingux", "run", p->argument, resolved});
}
