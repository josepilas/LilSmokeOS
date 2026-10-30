// LilSmokeUI — ROMScanner.cpp

#include "ROMScanner.h"
#include "Log.h"
#include "FilePersistence.h"
#include "EmulatorSystems.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace lilsmoke {

using SysMap = std::vector<std::pair<std::string, std::vector<std::string>>>;

const SysMap &ROMScanner::systemExts() {
    static const SysMap m = [] {
        SysMap entries = {
        {"nes",      {".nes", ".unif", ".unf", ".fds", ".fig", ".smc", ".7z", ".zip"}},
        {"snes",     {".sfc", ".smc", ".fig", ".bs", ".7z", ".zip"}},
        {"n64",      {".z64", ".v64", ".n64", ".7z", ".zip"}},
        {"gb",       {".gb", ".7z", ".zip"}},
        {"gbc",      {".gbc", ".7z", ".zip"}},
        {"gba",      {".gba", ".7z", ".zip"}},
        {"md",       {".md", ".gen", ".smd", ".bin", ".7z", ".zip"}},
        {"sms",      {".sms", ".7z", ".zip"}},
        {"gg",       {".gg", ".7z", ".zip"}},
        {"pce",      {".pce", ".7z", ".zip"}},
        {"arcade",   {".zip", ".7z"}},
        {"lynx",     {".lyx", ".lnx", ".7z", ".zip"}},
        {"psx",      {".bin", ".img", ".pbp", ".ecm", ".7z", ".zip", ".cue", ".m3u"}},
        };
        for (const auto &definition : emulatorSystemDefinitions())
            if (definition.expansion) entries.push_back({definition.id, definition.extensions});
        static const char *archives[] = {".zip", ".7z", ".rar", ".tar", ".tgz", ".gz",
                                         ".bz2", ".xz", ".tbz2", ".txz"};
        for (auto &entry : entries) {
            if (entry.first == "arcade") continue;
            for (const char *extension : archives) {
                if (std::find(entry.second.begin(), entry.second.end(), extension) == entry.second.end())
                    entry.second.emplace_back(extension);
            }
        }
        return entries;
    }();
    return m;
}

ROMScanner::ROMScanner() = default;

namespace fs = std::filesystem;

static constexpr size_t MAX_ROMS = 20000;
static constexpr size_t MAX_CACHE_BYTES = 4 * 1024 * 1024;
static constexpr size_t MAX_CACHE_FIELD = 64 * 1024;
static constexpr size_t MAX_SCAN_ENTRIES = 100000;
static constexpr size_t MAX_SCAN_DIRECTORIES = 4096;
static constexpr unsigned MAX_SCAN_DEPTH = 8;

static bool plainFile(const fs::path &path, size_t maxBytes = SIZE_MAX) {
    std::error_code ec;
    const fs::file_status status = fs::symlink_status(path, ec);
    if (ec || !fs::is_regular_file(status)) return false;
    const auto size = fs::file_size(path, ec);
    return !ec && size <= maxBytes;
}

static bool plainDirectory(const fs::path &path) {
    std::error_code ec;
    return fs::is_directory(fs::symlink_status(path, ec)) && !ec;
}

static fs::path canonicalRoot(const std::string &root) {
    if (root.empty() || root.find('\0') != std::string::npos) return {};
    std::error_code ec;
    fs::path result = fs::canonical(root, ec);
    return !ec && plainDirectory(result) ? result : fs::path();
}

static bool regularUnderRoot(const fs::path &path, const fs::path &root) {
    if (!path.is_absolute() || path != path.lexically_normal()) return false;
    const fs::path relative = path.lexically_relative(root);
    if (relative.empty() || relative.is_absolute()) return false;
    fs::path current = root;
    size_t components = 0;
    for (const auto &part : relative) {
        const std::string name = part.string();
        if (name.empty() || name.front() == '.' || name.find('\0') != std::string::npos ||
            ++components > MAX_SCAN_DEPTH + 2) return false;
        current /= part;
        if (current == path) return plainFile(current);
        if (!plainDirectory(current)) return false;
    }
    return false;
}

static std::string trim(const std::string &s) {
    size_t first = 0;
    while (first < s.size() &&
           std::isspace(static_cast<unsigned char>(s[first]))) {
        first++;
    }
    size_t last = s.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(s[last - 1]))) {
        last--;
    }
    return s.substr(first, last - first);
}

static bool safePortComponent(const std::string &value) {
    if (value.empty() || value == "." || value == "..") return false;
    if (value.find("..") != std::string::npos) return false;
    for (unsigned char c : value) {
        if (!(std::isalnum(c) || c == '.' || c == '_' || c == '-')) return false;
    }
    return true;
}

static bool endsWith(const std::string &value, const std::string &suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static std::string iniValue(const fs::path &path, const std::string &key) {
    if (!plainFile(path, MAX_CACHE_FIELD)) return "";
    std::ifstream in(path);
    if (!in) return "";

    bool inPortSmokeSection = false;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        if (line.front() == '[' && line.back() == ']') {
            inPortSmokeSection = (trim(line.substr(1, line.size() - 2)) == "PortSmoke");
            continue;
        }

        if (!inPortSmokeSection) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string k = trim(line.substr(0, eq));
        if (k == key) return trim(line.substr(eq + 1));
    }

    return "";
}

std::vector<GameEntry> scanPorts(const std::string &portsRoot) {
    std::vector<GameEntry> out;
    fs::path root = canonicalRoot(portsRoot);

    std::error_code ec;
    if (root.empty()) {
        LOG_WARN("PortSmoke: ports directory not found: %s", portsRoot.c_str());
        return out;
    }

    fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    fs::directory_iterator end;
    size_t examined = 0;
    for (; !ec && it != end && examined++ < MAX_SCAN_DIRECTORIES; it.increment(ec)) {
        const fs::directory_entry &entry = *it;
        if (entry.path().filename().string().front() == '.') continue;
        std::error_code entryEc;
        if (entry.is_symlink(entryEc)) continue;
        if (!entry.is_directory(entryEc)) continue;

        fs::path portDir = entry.path();
        fs::path confPath = portDir / "portsmoke.conf";
        if (!plainFile(confPath, MAX_CACHE_FIELD)) continue;

        std::string name = iniValue(confPath, "Name");
        std::string engine = iniValue(confPath, "EngineFile");
        std::string script = iniValue(confPath, "Script");
        std::string cover = iniValue(confPath, "Cover");
        std::string abi = iniValue(confPath, "ABI");
        std::string libc = iniValue(confPath, "Libc");

        if (name.empty()) name = portDir.filename().string();
        if (!safePortComponent(engine) || !endsWith(engine, ".peng")) {
            LOG_WARN("PortSmoke: invalid EngineFile in %s", confPath.string().c_str());
            continue;
        }
        if (!safePortComponent(script) || !endsWith(script, ".sh")) {
            LOG_WARN("PortSmoke: invalid Script in %s", confPath.string().c_str());
            continue;
        }
        if (!abi.empty() && abi != "o32-hard-float") {
            LOG_WARN("PortSmoke: incompatible ABI '%s' in %s", abi.c_str(), confPath.string().c_str());
            continue;
        }
        if (!libc.empty() && libc != "musl") {
            LOG_WARN("PortSmoke: incompatible libc '%s' in %s", libc.c_str(), confPath.string().c_str());
            continue;
        }
        if (cover.empty()) cover = "cover.webp";
        if (!safePortComponent(cover)) cover = "cover.webp";

        fs::path scriptPath = portDir / script;
        fs::path enginePath = root.parent_path() / "engines" / engine;
        if (!plainFile(scriptPath)) {
            LOG_WARN("PortSmoke: script missing: %s", scriptPath.string().c_str());
            continue;
        }
        if (!plainDirectory(enginePath.parent_path()) || !plainFile(enginePath)) {
            LOG_WARN("PortSmoke: engine missing: %s", enginePath.string().c_str());
            continue;
        }

        if (name.size() > 96) name.resize(96);

        GameEntry game;
        game.name = name;
        game.scriptPath = scriptPath.string();
        game.coverPath = (portDir / cover).string();
        out.push_back(std::move(game));
    }

    std::sort(out.begin(), out.end(), [](const GameEntry &a, const GameEntry &b) {
        return a.name < b.name;
    });

    LOG_INFO("PortSmoke: found %d port(s) in %s", (int)out.size(), portsRoot.c_str());
    return out;
}

static std::string toLower(const std::string &s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return std::tolower(c); });
    return r;
}

static bool hasExt(const std::string &path, const std::vector<std::string> &exts) {
    std::string lp = toLower(path);
    for (const auto &e : exts) {
        if (lp.size() >= e.size() && lp.compare(lp.size() - e.size(), e.size(), e) == 0)
            return true;
    }
    return false;
}

static std::string canonicalSystem(const std::string &directory) {
    static const std::pair<const char *, const char *> aliases[] = {
        {"fc", "nes"}, {"famicom", "nes"}, {"sfc", "snes"},
        {"superfamicom", "snes"}, {"super nintendo", "snes"},
        {"nintendo64", "n64"}, {"nintendo 64", "n64"},
        {"gameboy", "gb"}, {"game boy", "gb"},
        {"gameboycolor", "gbc"}, {"game boy color", "gbc"},
        {"gameboyadvance", "gba"}, {"game boy advance", "gba"},
        {"genesis", "md"}, {"megadrive", "md"}, {"mega drive", "md"},
        {"mastersystem", "sms"}, {"master system", "sms"},
        {"gamegear", "gg"}, {"game gear", "gg"},
        {"pcengine", "pce"}, {"pc engine", "pce"}, {"tg16", "pce"},
        {"turbografx16", "pce"}, {"turbografx-16", "pce"},
        {"mame", "arcade"}, {"fbneo", "arcade"}, {"fba", "arcade"},
        {"atarilynx", "lynx"}, {"atari lynx", "lynx"},
        {"ps", "psx"}, {"ps1", "psx"}, {"playstation", "psx"}, {"playstation1", "psx"},
    };
    const std::string lower = toLower(directory);
    for (const auto &alias : aliases) if (lower == alias.first) return alias.second;
    return canonicalEmulatorSystem(lower);
}

static const std::vector<std::string> *extensionsFor(const SysMap &systems, const std::string &system) {
    for (const auto &entry : systems) if (entry.first == system) return &entry.second;
    return nullptr;
}

static std::string rootFileSystem(const SysMap &systems, const std::string &name) {
    std::string result;
    for (const auto &entry : systems) {
        if (!hasExt(name, entry.second)) continue;
        if (!result.empty()) return ""; // Root archives and BIN files have no unique system.
        result = entry.first;
    }
    return result;
}

static std::string basenameNoExt(const std::string &path) {
    size_t slash = path.find_last_of('/');
    std::string fname = (slash != std::string::npos) ? path.substr(slash + 1) : path;
    size_t dot = fname.find_last_of('.');
    if (dot != std::string::npos) fname.erase(dot);
    if (endsWith(toLower(fname), ".tar")) fname.resize(fname.size() - 4);
    return fname;
}

static bool isFile(const std::string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static std::string coverPathFor(const std::string &system, const std::string &name) {
    const std::string base = "/persist/scrape/covers/" + system + "/" + name;
    static const char *exts[] = {".png", ".webp", ".jpg", ".jpeg"};
    for (const char *ext : exts) {
        std::string candidate = base + ext;
        if (isFile(candidate)) return candidate;
    }
    return base + ".png";
}

static std::string decodeXml(std::string value) {
    const std::pair<const char *, const char *> entities[] = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"},
        {"&quot;", "\""}, {"&apos;", "'"},
    };
    for (const auto &entity : entities) {
        size_t pos = 0;
        while ((pos = value.find(entity.first, pos)) != std::string::npos) {
            value.replace(pos, std::strlen(entity.first), entity.second);
            pos += std::strlen(entity.second);
        }
    }
    return value;
}

static std::string xmlField(const std::string &entry, const std::string &tag) {
    const std::string opening = "<" + tag;
    size_t begin = entry.find(opening);
    if (begin == std::string::npos) return "";
    const size_t nameEnd = begin + opening.size();
    if (nameEnd >= entry.size() ||
        (entry[nameEnd] != '>' &&
         !std::isspace(static_cast<unsigned char>(entry[nameEnd])))) {
        return "";
    }
    begin = entry.find('>', nameEnd);
    if (begin == std::string::npos) return "";
    const std::string closing = "</" + tag + ">";
    size_t end = entry.find(closing, begin + 1);
    if (end == std::string::npos) return "";
    return decodeXml(trim(entry.substr(begin + 1, end - begin - 1)));
}

static std::string normalizedPath(const fs::path &path) {
    return toLower(path.lexically_normal().generic_string());
}

static std::string metadataDate(std::string value) {
    if (value.size() >= 8 &&
        std::all_of(value.begin(), value.begin() + 8,
                    [](unsigned char c) { return std::isdigit(c); })) {
        return value.substr(0, 4) + "-" + value.substr(4, 2) + "-" +
               value.substr(6, 2);
    }
    return value;
}

static void applyGameMetadata(ROM &rom,
                              const std::string &entry,
                              const fs::path &metadataDir) {
    const std::string displayName = xmlField(entry, "name");
    if (!displayName.empty()) rom.name = displayName;
    rom.description = xmlField(entry, "desc");
    rom.releaseDate = metadataDate(xmlField(entry, "releasedate"));
    rom.developer = xmlField(entry, "developer");
    rom.publisher = xmlField(entry, "publisher");
    rom.genre = xmlField(entry, "genre");
    rom.players = xmlField(entry, "players");
    rom.rating = xmlField(entry, "rating");

    for (const char *field : {"image", "thumbnail", "marquee"}) {
        std::string asset = xmlField(entry, field);
        if (asset.empty()) continue;
        fs::path resolved(asset);
        if (resolved.is_relative()) resolved = metadataDir / resolved;
        std::error_code ec;
        if (fs::is_regular_file(resolved, ec)) {
            rom.coverPath = resolved.lexically_normal().string();
            break;
        }
    }
}

static void loadGamelistMetadata(const fs::path &systemDir,
                                 std::vector<ROM> &roms,
                                 size_t firstRom) {
    const fs::path gamelist = systemDir / "gamelist.xml";
    if (!plainFile(gamelist, MAX_CACHE_BYTES)) return;
    std::ifstream input(gamelist);
    if (!input) return;

    std::unordered_map<std::string, size_t> byPath;
    std::unordered_map<std::string, size_t> byFilename;
    for (size_t index = firstRom; index < roms.size(); ++index) {
        byPath[normalizedPath(roms[index].path)] = index;
        const std::string filename = toLower(fs::path(roms[index].path).filename().string());
        const auto inserted = byFilename.emplace(filename, index);
        if (!inserted.second) inserted.first->second = roms.size();
    }

    std::string line;
    std::string entry;
    bool insideGame = false;
    size_t applied = 0;
    while (std::getline(input, line)) {
        size_t cursor = 0;
        while (cursor < line.size()) {
            if (!insideGame) {
                size_t start = line.find("<game", cursor);
                if (start == std::string::npos) break;
                size_t after = start + 5;
                if (after < line.size() && line[after] != '>' &&
                    !std::isspace(static_cast<unsigned char>(line[after]))) {
                    cursor = after;
                    continue;
                }
                insideGame = true;
                entry.clear();
                cursor = start;
            }

            size_t finish = line.find("</game>", cursor);
            if (finish == std::string::npos) {
                entry.append(line, cursor, std::string::npos);
                entry.push_back('\n');
                if (entry.size() > 256 * 1024) {
                    entry.clear();
                    insideGame = false;
                }
                break;
            }

            entry.append(line, cursor, finish + 7 - cursor);
            const std::string listedPath = xmlField(entry, "path");
            if (!listedPath.empty()) {
                fs::path resolved(listedPath);
                if (resolved.is_relative()) resolved = systemDir / resolved;
                auto found = byPath.find(normalizedPath(resolved));
                if (found == byPath.end()) {
                    found = byPath.find(normalizedPath(systemDir /
                        fs::path(listedPath).filename()));
                }
                size_t romIndex = roms.size();
                if (found != byPath.end()) {
                    romIndex = found->second;
                } else {
                    auto filename = byFilename.find(
                        toLower(fs::path(listedPath).filename().string()));
                    if (filename != byFilename.end()) romIndex = filename->second;
                }
                if (romIndex < roms.size()) {
                    applyGameMetadata(roms[romIndex], entry, systemDir);
                    ++applied;
                }
            }
            entry.clear();
            insideGame = false;
            cursor = finish + 7;
        }
    }
    LOG_INFO("ROMScanner: applied %d gamelist entries from %s",
             static_cast<int>(applied), gamelist.string().c_str());
}

static void sortROMs(std::vector<ROM> &roms) {
    std::sort(roms.begin(), roms.end(), [](const ROM &a, const ROM &b) {
        if (a.system != b.system) return a.system < b.system;
        if (a.name != b.name) return a.name < b.name;
        return a.path < b.path;
    });
}

static void appendROM(std::vector<ROM> &roms, const fs::path &path, const std::string &system) {
    if (roms.size() >= MAX_ROMS) return;
    ROM rom;
    rom.system = system;
    rom.name = basenameNoExt(path.filename().string());
    if (system == "cavestory" || system == "easyrpg") {
        const auto parent = path.parent_path().filename().string();
        rom.name = canonicalEmulatorSystem(parent) == system ? emulatorSystemName(system) : parent;
    }
    rom.path = path.string();
    rom.coverPath = coverPathFor(system, rom.name);
    roms.push_back(std::move(rom));
}

struct ScanBudget {
    size_t entries = 0;
    size_t directories = 0;
};

static void collectROMs(const fs::path &directory, const std::string &system,
                        const std::vector<std::string> &exts, unsigned depth,
                        ScanBudget &budget, std::vector<ROM> &roms) {
    if (budget.directories >= MAX_SCAN_DIRECTORIES || budget.entries >= MAX_SCAN_ENTRIES ||
        roms.size() >= MAX_ROMS) return;
    ++budget.directories;
    std::error_code ec;
    fs::directory_iterator it(directory, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end && budget.entries < MAX_SCAN_ENTRIES && roms.size() < MAX_ROMS; it.increment(ec)) {
        ++budget.entries;
        const auto &entry = *it;
        const std::string name = entry.path().filename().string();
        if (name.empty() || name.front() == '.') continue;
        std::error_code entryEc;
        const auto status = entry.symlink_status(entryEc);
        if (entryEc || fs::is_symlink(status)) continue;
        if (fs::is_directory(status) && depth < MAX_SCAN_DEPTH) {
            collectROMs(entry.path(), system, exts, depth + 1, budget, roms);
        } else if (fs::is_regular_file(status) && hasExt(name, exts) &&
                   (hasExt(name, std::vector<std::string>{".zip", ".7z", ".rar", ".tar", ".tgz", ".gz", ".bz2", ".xz", ".tbz2", ".txz"}) ||
                    emulatorContentMarker(system, name))) {
            appendROM(roms, entry.path(), system);
        }
    }
}

static void removeReferencedPSX(std::vector<ROM> &roms, const fs::path &root) {
    std::unordered_set<std::string> referenced;
    size_t bytesRead = 0;
    for (const auto &rom : roms) {
        const fs::path path(rom.path);
        const std::string ext = toLower(path.extension().string());
        if (rom.system != "psx" || (ext != ".cue" && ext != ".m3u") ||
            !plainFile(path, 256 * 1024) || bytesRead >= MAX_CACHE_BYTES) continue;
        std::error_code ec;
        const size_t size = fs::file_size(path, ec);
        if (ec || size > MAX_CACHE_BYTES - bytesRead) continue;
        bytesRead += size;
        std::ifstream input(path);
        std::string line;
        while (std::getline(input, line)) {
            std::string target;
            if (ext == ".cue") {
                std::istringstream fields(line);
                std::string keyword;
                fields >> keyword;
                if (toLower(keyword) != "file") continue;
                fields >> std::quoted(target);
                if (!fields) continue;
            } else {
                target = trim(line);
                if (target.empty() || target.front() == '#') continue;
            }
            std::replace(target.begin(), target.end(), '\\', '/');
            fs::path dependency(target);
            if (dependency.is_relative()) dependency = path.parent_path() / dependency;
            dependency = dependency.lexically_normal();
            if (!regularUnderRoot(dependency, root)) continue;
            const std::string dependencyExt = toLower(dependency.extension().string());
            if (dependencyExt == ".bin" || dependencyExt == ".img" ||
                (ext == ".m3u" && (dependencyExt == ".cue" || dependencyExt == ".pbp" || dependencyExt == ".ecm")))
                referenced.insert(dependency.string());
        }
    }
    roms.erase(std::remove_if(roms.begin(), roms.end(), [&](const ROM &rom) {
        return rom.system == "psx" && referenced.count(rom.path);
    }), roms.end());
}

[[maybe_unused]] static void appendPorts(std::vector<ROM> &roms, const fs::path &root) {
    const fs::path ports = root.parent_path() / "portsmoke/ports";
    for (const GameEntry &game : scanPorts(ports.string())) {
        if (roms.size() >= MAX_ROMS) break;
        ROM rom;
        rom.system = "portsmoke";
        rom.name = game.name;
        rom.path = game.scriptPath;
        rom.coverPath = game.coverPath;
        roms.push_back(std::move(rom));
    }
}

int ROMScanner::scan(const std::string &rootDir) {
    roms_.clear();
    scanRoot_.clear();
    if (rootDir.empty() || rootDir.find('\0') != std::string::npos) return 0;
    LOG_INFO("ROMScanner: scanning %s", rootDir.c_str());
    const fs::path root = canonicalRoot(rootDir);
    ScanBudget budget;
    if (root.empty()) {
        LOG_WARN("ROMScanner: cannot open %s", rootDir.c_str());
    } else {
        scanRoot_ = root.string();
        std::error_code ec;
        fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
        std::vector<std::pair<std::string, fs::path>> directories;
        for (; !ec && it != end && budget.entries < MAX_SCAN_ENTRIES; it.increment(ec)) {
            ++budget.entries;
            const auto &entry = *it;
            const std::string name = entry.path().filename().string();
            if (name.empty() || name.front() == '.') continue;
            std::error_code entryEc;
            const auto status = entry.symlink_status(entryEc);
            if (entryEc || fs::is_symlink(status)) continue;
            if (fs::is_directory(status)) {
                const std::string system = canonicalSystem(name);
                if (extensionsFor(systemExts(), system) && directories.size() < MAX_SCAN_DIRECTORIES)
                    directories.emplace_back(system, entry.path());
            } else if (fs::is_regular_file(status)) {
                const std::string system = rootFileSystem(systemExts(), name);
                if (!system.empty()) appendROM(roms_, entry.path(), system);
            }
        }
        std::sort(directories.begin(), directories.end());
        for (const auto &directory : directories) {
            const size_t first = roms_.size();
            collectROMs(directory.second, directory.first, *extensionsFor(systemExts(), directory.first), 0, budget, roms_);
            loadGamelistMetadata(directory.second, roms_, first);
        }
        removeReferencedPSX(roms_, root);
    }
    std::error_code ec;
    const fs::path portBase = fs::absolute(rootDir, ec).lexically_normal();
    // Independent applications are discovered by their own package catalog.
    sortROMs(roms_);
    LOG_INFO("ROMScanner: found %d item(s); examined %zu entries, %zu directories",
             static_cast<int>(roms_.size()), budget.entries, budget.directories);
    return static_cast<int>(roms_.size());
}

// The ROM model currently has eleven strings; reserve two empty quoted slots
// so the versioned disk format has exactly thirteen fields per record.
static std::array<std::string, 13> cacheFields(const ROM &rom) {
    return {rom.system, rom.name, rom.path, rom.coverPath, rom.description,
            rom.releaseDate, rom.developer, rom.publisher, rom.genre,
            rom.players, rom.rating, "", ""};
}

static bool validField(const std::string &field) {
    return field.size() <= MAX_CACHE_FIELD && field.find('\0') == std::string::npos;
}

static bool readQuoted(std::istream &input, std::string &field) {
    input >> std::ws;
    if (input.peek() != '"') return false;
    input >> std::quoted(field);
    return input && validField(field);
}

static bool readCache(const std::string &path, std::string &contents) {
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return false;
    struct stat st;
    bool ok = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size >= 0 &&
              static_cast<uint64_t>(st.st_size) <= MAX_CACHE_BYTES;
    if (ok) {
        contents.resize(static_cast<size_t>(st.st_size));
        size_t offset = 0;
        while (offset < contents.size()) {
            const ssize_t count = read(fd, &contents[offset], contents.size() - offset);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) { ok = false; break; }
            offset += static_cast<size_t>(count);
        }
        char extra;
        if (ok && read(fd, &extra, 1) != 0) ok = false;
    }
    close(fd);
    return ok;
}

bool ROMScanner::saveCache(const std::string &cachePath) const {
    if (scanRoot_.empty() || roms_.size() > MAX_ROMS || cachePath.empty() ||
        cachePath.find('\0') != std::string::npos) return false;
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "LILSMOKE_ROM_CACHE 1 13 " << roms_.size() << '\n' << std::quoted(scanRoot_) << '\n';
    for (const ROM &rom : roms_) {
        for (const auto &field : cacheFields(rom)) {
            if (!validField(field)) return false;
            output << std::quoted(field) << ' ';
        }
        output << '\n';
        if (output.tellp() > static_cast<std::streamoff>(MAX_CACHE_BYTES)) return false;
    }
    if (!output) return false;
    const std::string contents = output.str();
    if (contents.size() > MAX_CACHE_BYTES) return false;
    std::error_code ec;
    fs::path destination = fs::absolute(cachePath, ec);
    if (ec) return false;
    const auto status = fs::symlink_status(destination, ec);
    if (ec && ec != std::errc::no_such_file_or_directory) return false;
    if (!ec && fs::exists(status) && !fs::is_regular_file(status)) return false;
    ec.clear();
    fs::create_directories(destination.parent_path(), ec);
    if (ec) return false;
    std::string temporary = destination.string() + ".tmp.XXXXXX";
    std::vector<char> filename(temporary.begin(), temporary.end());
    filename.push_back('\0');
    const int fd = mkstemp(filename.data());
    if (fd < 0) return false;
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    size_t offset = 0;
    bool ok = true;
    while (offset < contents.size()) {
        const ssize_t count = write(fd, contents.data() + offset, contents.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { ok = false; break; }
        offset += static_cast<size_t>(count);
    }
    if (ok && fsync(fd) != 0) ok = false;
    if (close(fd) != 0) ok = false;
    if (ok && rename(filename.data(), destination.c_str()) != 0) ok = false;
    if (!ok) unlink(filename.data());
    if(ok && !syncDirectory(destination.parent_path()))ok=false;
    return ok;
}

bool ROMScanner::loadCache(const std::string &cachePath, const std::string &rootDir) {
    roms_.clear();
    scanRoot_.clear();
    const fs::path root = canonicalRoot(rootDir);
    if (root.empty() || cachePath.empty() || cachePath.find('\0') != std::string::npos) return false;
    std::string contents;
    if (!readCache(cachePath, contents)) return false;
    std::istringstream input(contents);
    input.imbue(std::locale::classic());
    std::string magic, cachedRoot;
    unsigned version = 0, fieldCount = 0;
    size_t count = 0;
    if (!(input >> magic >> version >> fieldCount >> count) || magic != "LILSMOKE_ROM_CACHE" ||
        version != 1 || fieldCount != 13 || count > MAX_ROMS || !readQuoted(input, cachedRoot) ||
        cachedRoot != root.string()) return false;
    std::vector<ROM> loaded;
    std::unordered_set<std::string> paths;
    for (size_t index = 0; index < count; ++index) {
        std::array<std::string, 13> fields;
        for (auto &field : fields) if (!readQuoted(input, field)) return false;
        if (!fields[11].empty() || !fields[12].empty()) return false;
        if (fields[0] == "portsmoke") continue; // Never trust a cached executable or port metadata.
        const auto *exts = extensionsFor(systemExts(), fields[0]);
        const fs::path path(fields[2]);
        if (!exts || fields[1].empty() || !hasExt(path.filename().string(), *exts) ||
            !regularUnderRoot(path, root) || !paths.insert(path.string()).second) return false;
        const fs::path relative = path.lexically_relative(root);
        const std::string classified = relative.has_parent_path()
            ? canonicalSystem(relative.begin()->string()) : rootFileSystem(systemExts(), path.filename().string());
        if (classified != fields[0]) return false;
        ROM rom;
        rom.system = std::move(fields[0]); rom.name = std::move(fields[1]);
        rom.path = std::move(fields[2]); rom.coverPath = std::move(fields[3]);
        rom.description = std::move(fields[4]); rom.releaseDate = std::move(fields[5]);
        rom.developer = std::move(fields[6]); rom.publisher = std::move(fields[7]);
        rom.genre = std::move(fields[8]); rom.players = std::move(fields[9]);
        rom.rating = std::move(fields[10]);
        loaded.push_back(std::move(rom));
    }
    input >> std::ws;
    if (!input.eof()) return false;
    removeReferencedPSX(loaded, root);
    std::error_code ec;
    const fs::path portBase = fs::absolute(rootDir, ec).lexically_normal();
    if (ec) return false;
    // Do not restore obsolete PortSmoke entries from the ROM cache.
    sortROMs(loaded);
    roms_ = std::move(loaded);
    scanRoot_ = root.string();
    LOG_INFO("ROMScanner: loaded %d validated cache item(s)", static_cast<int>(roms_.size()));
    return true;
}

std::vector<const ROM *> ROMScanner::bySystem(const std::string &system) const {
    std::vector<const ROM *> out;
    for (const auto &r : roms_) {
        if (r.system == system) out.push_back(&r);
    }
    return out;
}

std::vector<std::string> ROMScanner::systems() const {
    std::vector<std::string> out;
    std::string last;
    for (const auto &r : roms_) {
        if (r.system != last) { out.push_back(r.system); last = r.system; }
    }
    return out;
}

}  // namespace lilsmoke
