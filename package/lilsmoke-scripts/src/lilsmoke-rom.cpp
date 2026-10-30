#include <archive.h>
#include <archive_entry.h>
#include <sodium.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/resource.h>
#include <unistd.h>
#include "EmulatorSystems.h"

namespace fs = std::filesystem;
constexpr uint64_t MaxExpanded = 2ULL * 1024 * 1024 * 1024;
constexpr unsigned MaxEntries = 20000;
static std::string lower(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
static bool ends(const std::string &s, const std::string &suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}
static bool compressed(const fs::path &p) {
    const std::string name = lower(p.filename().string());
    for (const char *suffix : {".zip", ".7z", ".rar", ".tar", ".gz", ".bz2", ".xz", ".tgz", ".tbz2", ".txz"})
        if (ends(name, suffix)) return true;
    return false;
}
static const std::map<std::string, std::vector<std::string>> extensions = [] {
    std::map<std::string, std::vector<std::string>> result = {
    {"nes", {".nes", ".unif", ".unf", ".fds"}}, {"snes", {".sfc", ".smc", ".fig", ".bs"}},
    {"gb", {".gb"}}, {"gbc", {".gbc", ".gb"}}, {"gba", {".gba", ".agb"}},
    {"md", {".md", ".gen", ".smd", ".bin"}}, {"sms", {".sms"}}, {"gg", {".gg"}},
    {"pce", {".pce"}}, {"lynx", {".lnx", ".lyx"}}, {"n64", {".z64", ".n64", ".v64"}},
    {"psx", {".cue", ".pbp", ".iso", ".img", ".bin", ".ecm", ".chd"}}
    };
    for (const auto &definition : lilsmoke::emulatorSystemDefinitions())
        if (definition.expansion) result[definition.id] = definition.extensions;
    return result;
}();
static std::string canonicalSystem(std::string s) {
    s = lower(s);
    if (s == "sfc") return "snes";
    if (s == "genesis" || s == "megadrive") return "md";
    if (s == "ps1" || s == "playstation") return "psx";
    if (s == "fbneo") return "arcade";
    if (s == "famicom" || s == "fds") return "nes";
    if (s == "mastersystem") return "sms";
    if (s == "gamegear") return "gg";
    if (s == "pcengine" || s == "tg16") return "pce";
    return lilsmoke::canonicalEmulatorSystem(s);
}
static std::string archiveStem(const fs::path &source) {
    std::string stem = source.filename().string();
    for (const char *suffix : {".tar.gz", ".tar.bz2", ".tar.xz", ".zip", ".7z", ".rar", ".tar", ".gz", ".bz2", ".xz", ".tgz", ".tbz2", ".txz"}) {
        if (ends(lower(stem), suffix)) { stem.resize(stem.size() - std::string(suffix).size()); break; }
    }
    if (stem.empty()) stem = "rom";
    return stem;
}
static fs::path safeMember(const std::string &name) {
    if (name.empty() || name.size() > 4096 || name.front() == '/' || name.find('\\') != std::string::npos)
        throw std::runtime_error("unsafe archive pathname");
    for (unsigned char c : name) if (c < 32 || c == 127 || c == ':') throw std::runtime_error("unsafe archive character");
    fs::path result;
    for (const auto &part : fs::path(name)) {
        if (part == "..") throw std::runtime_error("archive traversal refused");
        if (part != "." && !part.empty()) result /= part;
    }
    return result;
}
static std::string digest(const fs::path &source) {
    crypto_hash_sha256_state state;
    crypto_hash_sha256_init(&state);
    std::ifstream input(source, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read archive");
    char block[65536];
    while (input) {
        input.read(block, sizeof(block));
        crypto_hash_sha256_update(&state, reinterpret_cast<unsigned char *>(block), input.gcount());
    }
    if (!input.eof()) throw std::runtime_error("archive read failed");
    unsigned char hash[crypto_hash_sha256_BYTES];
    crypto_hash_sha256_final(&state, hash);
    char text[crypto_hash_sha256_BYTES * 2 + 1];
    sodium_bin2hex(text, sizeof(text), hash, sizeof(hash));
    return text;
}
static void extract(const fs::path &source, const fs::path &destination) {
    struct archive *reader = archive_read_new(), *writer = archive_write_disk_new();
    if (!reader || !writer) throw std::runtime_error("archive allocation failed");
    archive_read_support_filter_all(reader);
    archive_read_support_format_all(reader);
    archive_read_support_format_raw(reader);
    archive_write_disk_set_options(writer, ARCHIVE_EXTRACT_SECURE_NODOTDOT |
        ARCHIVE_EXTRACT_SECURE_SYMLINKS | ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS);
    const fs::path previous = fs::current_path();
    try {
        if (archive_read_open_filename(reader, source.c_str(), 65536) != ARCHIVE_OK)
            throw std::runtime_error(archive_error_string(reader) ? archive_error_string(reader) : "archive open failed");
        fs::current_path(destination);
        struct archive_entry *entry;
        unsigned count = 0;
        uint64_t total = 0;
        int status;
        while ((status = archive_read_next_header(reader, &entry)) != ARCHIVE_EOF) {
            if (status < ARCHIVE_WARN || ++count > MaxEntries) throw std::runtime_error("invalid or oversized archive");
            if (archive_entry_symlink(entry) || archive_entry_hardlink(entry)) throw std::runtime_error("archive links refused");
            const auto type = archive_entry_filetype(entry);
            if (type != AE_IFREG && type != AE_IFDIR) throw std::runtime_error("archive special file refused");
            const char *name = archive_entry_pathname(entry);
            std::string rawName = name ? name : "";
            if (archive_format(reader) == ARCHIVE_FORMAT_RAW) rawName = archiveStem(source);
            const fs::path relative = safeMember(rawName);
            if (relative.empty()) { archive_read_data_skip(reader); continue; }
            const std::string relativeName = relative.generic_string();
            archive_entry_set_pathname(entry, relativeName.c_str());
            archive_entry_set_perm(entry, type == AE_IFDIR ? 0755 : 0644);
            if (archive_entry_size(entry) < 0 || static_cast<uint64_t>(archive_entry_size(entry)) > MaxExpanded)
                throw std::runtime_error("archive member exceeds disk budget");
            if (archive_write_header(writer, entry) < ARCHIVE_WARN) throw std::runtime_error("cannot create archive member");
            uint64_t logical = 0;
            const void *data;
            size_t size;
            la_int64_t offset;
            while ((status = archive_read_data_block(reader, &data, &size, &offset)) != ARCHIVE_EOF) {
                if (status < ARCHIVE_WARN || offset < 0 || static_cast<uint64_t>(offset) > MaxExpanded ||
                    size > MaxExpanded - static_cast<uint64_t>(offset)) throw std::runtime_error("invalid archive data span");
                logical = std::max(logical, static_cast<uint64_t>(offset) + size);
                if (logical > MaxExpanded - total) throw std::runtime_error("archive exceeds disk budget");
                if (archive_write_data_block(writer, data, size, offset) != ARCHIVE_OK) throw std::runtime_error("archive write failed");
            }
            total += logical;
            if (archive_write_finish_entry(writer) < ARCHIVE_WARN) throw std::runtime_error("archive member incomplete");
        }
        fs::current_path(previous);
        archive_read_free(reader); archive_write_free(writer);
    } catch (...) {
        fs::current_path(previous);
        archive_read_free(reader); archive_write_free(writer);
        throw;
    }
}
static std::vector<fs::path> files(const fs::path &root) {
    std::vector<fs::path> result;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
         it != end && !ec; it.increment(ec)) {
        if (it.depth() > 16 || it->path().filename().string().front() == '.') { it.disable_recursion_pending(); continue; }
        if (it->is_symlink(ec)) throw std::runtime_error("cache symlink refused");
        if (it->is_regular_file(ec)) result.push_back(it->path());
        if (result.size() > MaxEntries) throw std::runtime_error("too many cached files");
    }
    if (ec) throw std::runtime_error("cannot read extracted files");
    std::sort(result.begin(), result.end());
    return result;
}
static fs::path arcadeZip(const fs::path &cache, const fs::path &source) {
    const fs::path target = cache / (archiveStem(source) + ".zip");
    const auto members = files(cache);
    struct archive *writer = archive_write_new();
    archive_write_set_format_zip(writer);
    archive_write_set_options(writer, "zip:compression=store");
    if (archive_write_open_filename(writer, target.c_str()) != ARCHIVE_OK) {
        archive_write_free(writer); throw std::runtime_error("cannot create arcade ROM set");
    }
    try {
        char block[65536];
        for (const auto &member : members) {
            if (member == target) continue;
            struct archive_entry *entry = archive_entry_new();
            const std::string name = fs::relative(member, cache).generic_string();
            archive_entry_set_pathname(entry, name.c_str());
            archive_entry_set_filetype(entry, AE_IFREG); archive_entry_set_perm(entry, 0644);
            archive_entry_set_size(entry, fs::file_size(member));
            if (archive_write_header(writer, entry) != ARCHIVE_OK) { archive_entry_free(entry); throw std::runtime_error("arcade ZIP header failed"); }
            archive_entry_free(entry);
            std::ifstream input(member, std::ios::binary);
            while (input) {
                input.read(block, sizeof(block));
                if (input.gcount() && archive_write_data(writer, block, input.gcount()) != input.gcount())
                    throw std::runtime_error("arcade ZIP write failed");
            }
            if (!input.eof()) throw std::runtime_error("arcade member read failed");
        }
        if (archive_write_close(writer) != ARCHIVE_OK) throw std::runtime_error("arcade ZIP close failed");
        archive_write_free(writer);
    } catch (...) { archive_write_free(writer); throw; }
    return target;
}
static fs::path selectRom(const fs::path &cache, const std::string &system) {
    const auto list = files(cache);
    auto found = extensions.find(system);
    if (found == extensions.end()) throw std::runtime_error("unsupported emulator system");
    for (const auto &extension : found->second)
        for (const auto &file : list) if (lower(file.extension().string()) == extension &&
            lilsmoke::emulatorContentMarker(system, file.filename().string())) return file;
    throw std::runtime_error("no supported ROM found inside archive");
}
static fs::path resolve(const std::string &requested, const fs::path &path) {
    const std::string system = canonicalSystem(requested);
    const fs::path source = fs::canonical(path);
    if (!fs::is_regular_file(source)) throw std::runtime_error("ROM is not a regular file");
    if (!compressed(source)) return source;
    // ZIP is the common native/libretro arcade format; normalize other containers.
    if (system == "arcade" && ends(lower(source.string()), ".zip")) return source;
    if (system != "arcade" && !extensions.count(system)) throw std::runtime_error("unsupported emulator system");
    const char *setting = std::getenv("LILSMOKE_ARCHIVE_CACHE");
    const fs::path root = setting && *setting ? setting : "/storage/.cache/lilsmoke/archives";
    fs::create_directories(root / system);
    const std::string key = digest(source);
    const fs::path cache = fs::canonical(root / system) / key;
    if (fs::is_symlink(cache)) throw std::runtime_error("archive cache symlink refused");
    if (fs::is_regular_file(cache / ".complete")) {
        if (system != "arcade") return selectRom(cache, system);
        const fs::path zip = cache / (archiveStem(source) + ".zip");
        if (fs::is_regular_file(zip)) return zip;
    }
    std::string pattern = (cache.parent_path() / ".extract-XXXXXX").string();
    std::vector<char> temp(pattern.begin(), pattern.end()); temp.push_back(0);
    if (!mkdtemp(temp.data())) throw std::runtime_error("cannot create private extraction directory");
    const fs::path working(temp.data());
    try {
        std::cerr << "ROM archive: extracting " << source << '\n';
        extract(source, working);
        fs::path chosen = system == "arcade" ? arcadeZip(working, source) : selectRom(working, system);
        const fs::path relative = fs::relative(chosen, working);
        if (digest(source) != key) throw std::runtime_error("archive changed during extraction");
        std::ofstream marker(working / ".complete"); marker << "LilSmoke ROM archive v1\n"; marker.close();
        if (!marker) throw std::runtime_error("cannot finish archive cache");
        if (fs::exists(cache)) fs::remove_all(cache);
        fs::rename(working, cache);
        return cache / relative;
    } catch (...) { fs::remove_all(working); throw; }
}
int main(int argc, char **argv) {
    if (argc != 3) { std::cerr << "usage: lilsmoke-rom SYSTEM ROM\n"; return 64; }
    // Extraction runs before the emulator starts and never allocates a game
    // image in RAM. Oversized decoder dictionaries fail rather than OOMing UI.
    struct rlimit limit = {96U * 1024U * 1024U, 96U * 1024U * 1024U};
    (void)setrlimit(RLIMIT_AS, &limit);
    try { std::cout << resolve(argv[1], argv[2]).string() << '\n'; return 0; }
    catch (const std::exception &error) { std::cerr << "ROM archive: " << error.what() << '\n'; return 1; }
}
