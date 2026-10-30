#include "asset_pack.hpp"
#include "runtime_assets.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <sys/wait.h>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

using namespace portsmoke::sm64;
namespace fs = std::filesystem;
unsigned checks = 0;
void check(bool condition) {
    ++checks;
    if (!condition) throw std::runtime_error("check " + std::to_string(checks) + " failed");
}
template<class F> void rejects(F action) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    check(rejected);
}
void save(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    if (!stream) throw std::runtime_error("fixture write failed");
}
std::vector<std::uint8_t> read(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
void be(std::vector<std::uint8_t>& data, unsigned offset, unsigned value) {
    for (unsigned i = 0; i < 4; ++i) data[offset + i] = (value >> (24 - i * 8)) & 255;
}

int main() {
    std::string temporary = std::string(std::getenv("SM64_TEST_TMPDIR") ? std::getenv("SM64_TEST_TMPDIR") : "/tmp") +
                            "/portsmoke-sm64-unit-XXXXXX";
    if (!mkdtemp(temporary.data())) return 1;
    fs::path dir(temporary);
    try {
        check(sha1({}) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
        check(sha1({'a', 'b', 'c'}) == "a9993e364706816aba3e25717850c26c9cd0d89d");
        std::string longText = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        check(sha1({longText.begin(), longText.end()}) == "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
        check(sha1(std::vector<std::uint8_t>(1000000, 'a')) == "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
        std::vector<std::uint8_t> rom(64);
        rom[0] = 0x80; rom[1] = 0x37; rom[2] = 0x12; rom[3] = 0x40;
        rom[4] = 1; rom[5] = 2; rom[6] = 3; rom[7] = 4;
        std::copy_n("MIO0", 4, rom.begin() + 16);
        be(rom, 20, 9); be(rom, 24, 17); be(rom, 28, 19);
        rom[32] = 0xe0; rom[33] = 0x30; rom[34] = 2;
        rom[35] = 'A'; rom[36] = 'B'; rom[37] = 'C';
        auto decoded = decodeMio0(rom, 16);
        check(std::string(decoded.begin(), decoded.end()) == "ABCABCABC");
        auto bad = rom;
        bad[16] = 'X'; rejects([&] { decodeMio0(bad, 16); });
        bad = rom; be(bad, 20, 5 * 1024 * 1024); rejects([&] { decodeMio0(bad, 16); });
        bad = rom; be(bad, 24, 1); rejects([&] { decodeMio0(bad, 16); });
        bad = rom; be(bad, 28, 1000); rejects([&] { decodeMio0(bad, 16); });
        bad = rom; bad[32] = 0; rejects([&] { decodeMio0(bad, 16); });
        bad = rom; bad[33] = 0xf0; rejects([&] { decodeMio0(bad, 16); });
        rejects([&] { decodeMio0(rom, 63); });
        rejects([&] { decodeMio0({}, 0); });
        auto hash = sha1(rom), manifest = sha1({'u', 'n', 'i', 't'});
        AssetSpec specs[] = {{"assets/copy.bin", 16, 0, 9, 0, 0, 0}, {"assets/raw.bin", -1, 4, 4, 2, 1, 1}};
        GameDefinition definition{hash.c_str(), 64, manifest.c_str(), specs, 2};
        auto source = dir / "test.z64", packPath = dir / "data.pdat";
        save(source, rom);
        fs::create_directory(dir / "original");
        save(dir / "original/anything with no extension", rom);
        save(dir / "original/notes.txt", {'n', 'o', 't', 'e'});
        check(discoverRom(dir / "original", definition) == dir / "original/anything with no extension");
        save(dir / "original/duplicate.any", rom);
        rejects([&] { discoverRom(dir / "original", definition); });
        fs::remove(dir / "original/duplicate.any");
        auto unsupported = rom; unsupported[4] ^= 1;
        save(dir / "original/anything with no extension", unsupported);
        rejects([&] { discoverRom(dir / "original", definition); });
        save(dir / "original/anything with no extension", rom);
        if (!std::getenv("SM64_TEST_FAT")) {
            fs::create_symlink(source, dir / "original/link");
            rejects([&] { discoverRom(dir / "original", definition); });
            fs::remove(dir / "original/link");
        }
        rejects([&] { prepare(source, dir / "./test.z64", definition); });
        check(read(source) == rom);
        check(prepare(source, packPath, definition));
        AssetPack pack(packPath);
        check(pack.entries().size() == 2);
        check(pack.romSha1() == hash && pack.manifestSha1() == manifest);
        check(pack.load("assets/copy.bin") == decoded);
        check(pack.load("assets/raw.bin") == std::vector<std::uint8_t>({1, 2, 3, 4}));
        RuntimeAssets runtime(packPath, definition);
        check(runtime.texture("assets/raw.bin").rgba.size() == 8);
        rejects([&] { runtime.texture("assets/copy.bin"); });
        specs[1].width = 3;
        rejects([&] { RuntimeAssets wrong(packPath, definition); });
        specs[1].width = 2;
        std::uint8_t range[2]{};
        pack.read("assets/raw.bin", 1, range, sizeof(range));
        check(range[0] == 2 && range[1] == 3);
        pack.read("assets/raw.bin", 4, nullptr, 0);
        rejects([&] { pack.read("assets/raw.bin", 4, range, 1); });
        rejects([&] { pack.read("assets/raw.bin", UINT32_MAX, range, 1); });
        rejects([&] { pack.read("assets/raw.bin", 0, nullptr, 1); });
        rejects([&] { pack.load("missing"); });
        auto originalPack = read(packPath);
        check(!prepare(source, packPath, definition));
        check(read(packPath) == originalPack && read(source) == rom);
        auto swapped = rom;
        for (unsigned i = 0; i < swapped.size(); i += 2) std::swap(swapped[i], swapped[i + 1]);
        save(dir / "test.v64", swapped);
        check(prepare(dir / "test.v64", dir / "v64.pdat", definition));
        check(read(dir / "v64.pdat") == originalPack);
        swapped = rom;
        for (unsigned i = 0; i < swapped.size(); i += 4) std::reverse(swapped.begin() + i, swapped.begin() + i + 4);
        save(dir / "test.n64", swapped);
        check(prepare(dir / "test.n64", dir / "n64.pdat", definition));
        check(read(dir / "n64.pdat") == originalPack);
        bad = rom; bad[6] = 99; save(dir / "bad.z64", bad);
        rejects([&] { prepare(dir / "bad.z64", packPath, definition); });
        check(read(packPath) == originalPack);
        bad.resize(63); save(dir / "short.z64", bad);
        rejects([&] { prepare(dir / "short.z64", packPath, definition); });
        auto corrupt = originalPack; corrupt.back() ^= 1; save(packPath, corrupt);
        rejects([&] { AssetPack p(packPath); });
        check(prepare(source, packPath, definition));
        check(read(packPath) == originalPack);
        save(dir / "stale.pdat.tmp", {'x'});
        rejects([&] { prepare(source, dir / "stale.pdat", definition); });
        check(read(dir / "stale.pdat.tmp") == std::vector<std::uint8_t>({'x'}));
        for (const auto *point : {"header-synced", "before-rename"}) {
            auto path = dir / (std::string(point) + ".pdat");
            auto child = fork(); check(child >= 0);
            if (!child) {
                setenv("SM64_TEST_FAILPOINT", point, 1);
                try { prepare(source, path, definition); } catch (...) { _exit(87); }
                _exit(88);
            }
            int status = 0; check(waitpid(child, &status, 0) == child);
            check(WIFEXITED(status) && WEXITSTATUS(status) == 86);
            check(!fs::exists(path) && fs::exists(path.string() + ".tmp"));
            check(prepare(source, path, definition));
            check(read(path) == originalPack && !fs::exists(path.string() + ".tmp"));
            // A recognized interrupted rebuild must also be cleaned on cache reuse.
            save(path.string() + ".tmp", std::vector<std::uint8_t>(originalPack.begin(), originalPack.begin() + 64));
            check(!prepare(source, path, definition));
            check(read(path) == originalPack && !fs::exists(path.string() + ".tmp"));
        }
        // Keep a verified inode, not a path that can now point at unrelated bytes.
        fs::rename(packPath, dir / "old.pdat");
        save(packPath, {'u', 's', 'e', 'r'});
        check(pack.load("assets/copy.bin") == decoded);
        check(prepare(source, packPath, definition));
        if (!std::getenv("SM64_TEST_FAT")) {
        fs::create_symlink(source, dir / "link.z64");
        rejects([&] { prepare(dir / "link.z64", dir / "link.pdat", definition); });
        fs::create_symlink(packPath, dir / "link.pdat");
        rejects([&] { prepare(source, dir / "link.pdat", definition); });
        fs::create_directory(dir / "cache"); fs::create_directory_symlink(dir / "cache", dir / "cache-link");
        rejects([&] { prepare(source, dir / "cache-link/data.pdat", definition); });
        fs::create_symlink(source, dir / "unsafe.pdat.lock.d");
        rejects([&] { prepare(source, dir / "unsafe.pdat", definition); });
        fs::create_symlink(source, dir / "unsafe-stage.pdat.tmp");
        rejects([&] { prepare(source, dir / "unsafe-stage.pdat", definition); });
        fs::create_hard_link(source, dir / "hard-lock.pdat.lock.d");
        rejects([&] { prepare(source, dir / "hard-lock.pdat", definition); });
        fs::create_hard_link(source, dir / "hard-output.pdat");
        rejects([&] { prepare(source, dir / "hard-output.pdat", definition); });
        fs::create_directory(dir / "ancestor");
        fs::create_directory_symlink(dir / "ancestor", dir / "linked-ancestor");
        fs::create_directory(dir / "ancestor/child");
        rejects([&] { prepare(source, dir / "linked-ancestor/child/data.pdat", definition); });
        rejects([&] { prepare(source, dir / "linked-ancestor/../escape-data.pdat", definition); });
        }
        check(read(source) == rom);
        auto lockDir = dir / "busy.pdat.lock.d";
        fs::create_directory(lockDir);
        auto pid = std::to_string(getpid());
        std::ifstream proc("/proc/" + pid + "/task/" + pid + "/stat");
        std::string line, start, boot = "unknown"; std::getline(proc, line);
        std::istringstream fields(line.substr(line.rfind(')') + 1));
        for (unsigned i = 3; i <= 22; ++i) fields >> start;
        std::ifstream bootFile("/proc/sys/kernel/random/boot_id");
        bootFile >> boot;
        auto owner = "SM64LOCK1 " + std::to_string(getpid()) + " " + start + " " + boot + "\n";
        save(lockDir / "owner", {owner.begin(), owner.end()});
        check(fs::is_regular_file(lockDir / "owner"));
        auto child = fork(); check(child >= 0);
        if (!child) {
            try { prepare(source, dir / "busy.pdat", definition); } catch (...) { _exit(0); }
            std::cerr << "unexpected live lock acceptance\n";
            _exit(1);
        }
        int status = 0; check(waitpid(child, &status, 0) == child);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            std::cerr << "busy child status=" << status << '\n';
        check(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        fs::remove(lockDir / "owner"); fs::remove(lockDir);
        check(prepare(source, dir / "busy.pdat", definition));
        check(!fs::exists(lockDir));
        fs::create_directory(dir / "unknown.pdat.lock.d");
        rejects([&] { prepare(source, dir / "unknown.pdat", definition); });
        check(fs::is_directory(dir / "unknown.pdat.lock.d"));
        auto stale = dir / "dead.pdat.lock.d"; fs::create_directory(stale);
        std::string dead = "SM64LOCK1 99999999 0 unknown\n";
        save(stale / "owner", {dead.begin(), dead.end()});
        save(stale / "user-data", {'p', 'r', 'i', 'v', 'a', 't', 'e'});
        rejects([&] { prepare(source, dir / "dead.pdat", definition); });
        check(fs::exists(stale / "owner") && read(stale / "user-data").size() == 7);
        fs::remove(stale / "user-data");
        check(prepare(source, dir / "dead.pdat", definition) && !fs::exists(stale));
        specs[0].offset = 100;
        rejects([&] { prepare(source, dir / "range.pdat", definition); });
        check(!fs::exists(dir / "range.pdat.tmp") && !fs::exists(dir / "range.pdat"));
        specs[0].offset = 0; specs[0].name = "../escape.bin";
        rejects([&] { prepare(source, dir / "escape.pdat", definition); });
        check(!fs::exists(dir / "escape.pdat.tmp"));
        fs::remove_all(dir);
        std::cout << "PASS " << checks << " SM64 data checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; fs::remove_all(dir); return 1;
    }
}
