#include <sodium.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <array>
#include <vector>
#include <stdexcept>
#include <cstdlib>
#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
static constexpr uint64_t MaxBytes = 128ULL * 1024 * 1024;
static std::vector<fs::path> members(const fs::path &root) {
    std::vector<fs::path> paths;
    uint64_t bytes = 0;
    for (const auto &item : fs::recursive_directory_iterator(root)) {
        if (item.is_symlink()) throw std::runtime_error("Symlinks are not supported in imported game data");
        if (item.is_regular_file()) {
            bytes += item.file_size();
            if (bytes > MaxBytes || paths.size() >= 10000) throw std::runtime_error("Game data exceeds import budget");
            paths.push_back(item.path());
        } else if (!item.is_directory()) throw std::runtime_error("Unsupported game data entry");
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}
static void hashFile(crypto_hash_sha256_state &state, const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read game data");
    std::array<char, 65536> block{};
    while (in) {
        in.read(block.data(), block.size());
        crypto_hash_sha256_update(&state, reinterpret_cast<const unsigned char *>(block.data()), in.gcount());
    }
    if (!in.eof()) throw std::runtime_error("Game data read failed");
}
static std::string signature(const fs::path &project, const fs::path &support) {
    crypto_hash_sha256_state state;
    crypto_hash_sha256_init(&state);
    hashFile(state, project / "Doukutsu.exe");
    for (const auto &root : {project / "data", support}) {
        for (const auto &path : members(root)) {
            const auto name = fs::relative(path, root).generic_string();
            crypto_hash_sha256_update(&state, reinterpret_cast<const unsigned char *>(name.data()), name.size() + 1);
            hashFile(state, path);
        }
    }
    unsigned char digest[crypto_hash_sha256_BYTES];
    crypto_hash_sha256_final(&state, digest);
    char hex[crypto_hash_sha256_BYTES * 2 + 1];
    sodium_bin2hex(hex, sizeof(hex), digest, sizeof(digest));
    return hex;
}
static void copyTree(const fs::path &from, const fs::path &to, bool replace) {
    fs::create_directories(to);
    for (const auto &path : members(from)) {
        const auto target = to / fs::relative(path, from);
        fs::create_directories(target.parent_path());
        fs::copy_file(path, target, replace ? fs::copy_options::overwrite_existing : fs::copy_options::skip_existing);
    }
}
static fs::path prepare(fs::path selected) {
    selected = fs::canonical(selected);
    if (fs::is_regular_file(selected)) selected = selected.parent_path();
    if (fs::is_regular_file(selected / "npc.tbl") && fs::is_regular_file(selected / "sprites.sif") &&
        fs::is_regular_file(selected.parent_path() / ".complete")) return selected;
    if (selected.filename() == "data" && fs::is_regular_file(selected.parent_path() / "Doukutsu.exe"))
        selected = selected.parent_path();
    if (!fs::is_regular_file(selected / "Doukutsu.exe") || !fs::is_regular_file(selected / "data/npc.tbl"))
        throw std::runtime_error("Cave Story requires Doukutsu.exe and the original data/ folder together");
    const char *extra = std::getenv("LILSMOKE_NX_SUPPORT");
    const fs::path support = fs::canonical(extra ? extra : "/storage/emulators/opendingux/data/share/nxengine/data");
    const char *setting = std::getenv("LILSMOKE_NX_CACHE");
    const fs::path root = setting ? setting : "/storage/.cache/lilsmoke/nxengine";
    fs::create_directories(root);
    const auto key = signature(selected, support);
    const auto target = fs::canonical(root) / key;
    if (fs::is_symlink(target)) throw std::runtime_error("Unsafe import cache");
    if (fs::is_regular_file(target / ".complete")) return target / "data";
    auto pattern = (fs::canonical(root) / ".import-XXXXXX").string();
    std::vector<char> temp(pattern.begin(), pattern.end()); temp.push_back(0);
    if (!mkdtemp(temp.data())) throw std::runtime_error("Cannot create import directory");
    const fs::path work(temp.data());
    try {
        std::cerr << "NXEngine: importing original data without changing the source folder\n";
        fs::copy_file(selected / "Doukutsu.exe", work / "Doukutsu.exe");
        copyTree(selected / "data", work / "data", true);
        const char *override = std::getenv("LILSMOKE_NX_EXTRACTOR");
        const std::string extractor = override ? override : "/storage/emulators/opendingux/nxextract";
        if (access(extractor.c_str(), X_OK)) throw std::runtime_error("NX asset extractor is not installed");
        const pid_t child = fork();
        if (child < 0) throw std::runtime_error("Cannot start asset extractor");
        if (!child) {
            if (chdir(work.c_str()) || dup2(STDERR_FILENO, STDOUT_FILENO) < 0) _exit(126);
            execl(extractor.c_str(), extractor.c_str(), static_cast<char *>(nullptr));
            _exit(127);
        }
        int status = 0;
        pid_t reaped;
        do { reaped = waitpid(child, &status, 0); } while (reaped < 0 && errno == EINTR);
        if (reaped != child || !WIFEXITED(status) || WEXITSTATUS(status))
            throw std::runtime_error("Unsupported or damaged Cave Story executable/data version");
        copyTree(support, work / "data", true);
        if (!fs::is_regular_file(work / "data/sprites.sif") || !fs::is_regular_file(work / "data/npc.tbl"))
            throw std::runtime_error("Engine support data is incomplete");
        if (signature(selected, support) != key) throw std::runtime_error("Source changed during import");
        std::ofstream marker(work / ".complete"); marker << "LilSmoke NX data v1\n"; marker.close();
        if (!marker) throw std::runtime_error("Cannot complete imported data");
        fs::remove(work / "Doukutsu.exe");
        if (fs::exists(target)) throw std::runtime_error("Another import already created this cache");
        fs::rename(work, target);
        return target / "data";
    } catch (...) { fs::remove_all(work); throw; }
}
int main(int argc, char **argv) {
    if (argc != 2) { std::cerr << "usage: lilsmoke-nx-data GAME_DIRECTORY_OR_MARKER\n"; return 2; }
    try { std::cout << prepare(argv[1]).string() << '\n'; return 0; }
    catch (const std::exception &error) { std::cerr << "NXEngine: " << error.what() << '\n'; return 1; }
}
