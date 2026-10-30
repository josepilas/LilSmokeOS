#include "ROMScanner.h"

#include <array>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;
using lilsmoke::ROM;
using lilsmoke::ROMScanner;

struct Fixture {
    fs::path base, root, cache;
    Fixture() {
        char pattern[] = "/tmp/lilsmoke-scanner-XXXXXX";
        const char *created = mkdtemp(pattern);
        assert(created);
        base = created;
        root = base / "roms";
        cache = base / "cache/library.cache";
        fs::create_directories(root);
    }
    ~Fixture() { std::error_code ec; fs::remove_all(base, ec); }
    fs::path put(const std::string &relative, const std::string &data = "fixture") {
        const fs::path path = root / relative;
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        out << data;
        assert(out);
        return path;
    }
};

static std::string readFile(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

static std::array<std::string, 13> fields(const ROM &rom) {
    return {rom.system, rom.name, rom.path, rom.coverPath, rom.description,
            rom.releaseDate, rom.developer, rom.publisher, rom.genre,
            rom.players, rom.rating, "", ""};
}

static void writeCache(const Fixture &f, const std::vector<std::array<std::string, 13>> &records,
                       unsigned version = 1, size_t count = SIZE_MAX) {
    fs::create_directories(f.cache.parent_path());
    std::ofstream out(f.cache, std::ios::binary);
    out << "LILSMOKE_ROM_CACHE " << version << " 13 "
        << (count == SIZE_MAX ? records.size() : count) << '\n'
        << std::quoted(fs::canonical(f.root).string()) << '\n';
    for (const auto &record : records) {
        for (const auto &field : record) out << std::quoted(field) << ' ';
        out << '\n';
    }
    assert(out);
}

static void testArchivesAndAliases() {
    Fixture f;
    f.put("GBA/English/PKFR.zip");
    f.put("GENESIS/Regions/Sonic2.ZIP");
    f.put("SFC/OldGame.7z");
    f.put("md/unpacked.bin");
    f.put("standalone.gba");
    f.put("ambiguous.bin"); f.put("ambiguous.zip"); f.put("ambiguous.7z");
    f.put("unknown/game.gba"); f.put("GBA/.hidden/game.zip"); f.put("GBA/.hidden.zip");
    fs::create_directory_symlink(f.root / "SFC", f.root / "snes");
    fs::create_directory_symlink(f.root, f.root / "GENESIS/loop");
    fs::create_symlink(f.root / "SFC/OldGame.7z", f.root / "GBA/linked.zip");
    ROMScanner scanner;
    assert(scanner.scan(f.root.string()) == 5);
    assert(scanner.bySystem("gba").size() == 2);
    assert(scanner.bySystem("md").size() == 2);
    assert(scanner.bySystem("snes").size() == 1);

    Fixture archives;
    const std::vector<std::string> systems = {"nes", "snes", "n64", "gb", "gbc", "gba",
                                            "md", "sms", "gg", "pce", "lynx", "psx"};
    const std::vector<std::string> suffixes = {".zip", ".7z", ".rar", ".tar", ".tgz", ".gz",
                                             ".bz2", ".xz", ".tbz2", ".txz", ".tar.gz"};
    for (const auto &system : systems)
        for (size_t i = 0; i < suffixes.size(); ++i)
            archives.put(system + "/region/Game" + std::to_string(i) + suffixes[i]);
    archives.put("arcade/Valid.zip"); archives.put("arcade/NotSupported.rar");
    assert(scanner.scan(archives.root.string()) == static_cast<int>(systems.size() * suffixes.size() + 1));
    assert(scanner.bySystem("arcade").size() == 1);
    std::cout << "ROMSCANNER_ARCHIVES_ALIASES_OK\n";
}

static void testDepthAndPSX() {
    Fixture f;
    fs::path depth = "GBA";
    for (int i = 0; i < 8; ++i) depth /= "nested";
    f.put((depth / "bounded.zip").generic_string());
    f.put((depth / "too-deep/omitted.zip").generic_string());
    assert(ROMScanner().scan(f.root.string()) == 1);

    Fixture psx;
    psx.put("PSX/Disc One.BIN"); psx.put("PSX/Disc Two.bin");
    psx.put("PSX/Disc.cue", "FILE \"Disc One.BIN\" BINARY\nFILE \"Disc Two.bin\" BINARY\n");
    psx.put("PSX/Collection.M3U", "# Disc set\nDisc.cue\n");
    psx.put("PSX/Standalone.bin");
    psx.put("PSX/Missing.cue", "FILE \"missing.bin\" BINARY\n");
    ROMScanner scanner;
    assert(scanner.scan(psx.root.string()) == 3);
    assert(scanner.saveCache(psx.cache.string()));
    ROMScanner loaded;
    assert(loaded.loadCache(psx.cache.string(), psx.root.string()));
    assert(loaded.roms().size() == 3);
    std::cout << "ROMSCANNER_DEPTH_PSX_OK\n";
}

static void testMetadataAndCacheRoundTrip() {
    Fixture f;
    f.put("GBA/English/PKFR.zip"); f.put("GBA/Other/PKFR.zip"); f.put("GBA/media/cover.webp");
    f.put("GBA/gamelist.xml", "<gameList><game><path>./English/PKFR.zip</path>"
          "<name>PKFR &quot;Special&quot;</name><desc>Line one\nLine two with \\slash.</desc>"
          "<releasedate>20040129000000</releasedate><developer>Developer</developer>"
          "<publisher>Publisher</publisher><genre>Adventure</genre><players>1</players>"
          "<rating>0.9</rating><image>./media/cover.webp</image></game></gameList>");
    ROMScanner scanner;
    assert(scanner.scan(f.root.string()) == 2);
    assert(scanner.roms()[0].name == "PKFR");
    assert(scanner.roms()[1].name == "PKFR \"Special\"");
    assert(scanner.saveCache(f.cache.string()));
    ROMScanner loaded;
    assert(loaded.loadCache(f.cache.string(), f.root.string()));
    assert(loaded.roms().size() == scanner.roms().size());
    for (size_t i = 0; i < scanner.roms().size(); ++i)
        assert(fields(loaded.roms()[i]) == fields(scanner.roms()[i]));
    std::istringstream wire(readFile(f.cache));
    std::string magic, root; unsigned version, fieldCount; size_t count;
    wire >> magic >> version >> fieldCount >> count >> std::quoted(root);
    assert(version == 1 && fieldCount == 13 && count == 2);
    for (size_t i = 0; i < count; ++i) {
        std::array<std::string, 13> record;
        for (auto &field : record) wire >> std::quoted(field);
        assert(wire && record[11].empty() && record[12].empty());
    }
    wire >> std::ws;
    assert(wire.eof());
    assert(scanner.saveCache(f.cache.string()));
    for (const auto &entry : fs::directory_iterator(f.cache.parent_path()))
        assert(entry.path().filename().string().find(".tmp.") == std::string::npos);
    std::cout << "ROMSCANNER_QUOTED_CACHE_METADATA_OK\n";
}

static void testFailClosedCache() {
    Fixture f;
    const fs::path game = f.put("gba/game.zip");
    ROMScanner scanner;
    assert(scanner.scan(f.root.string()) == 1);
    assert(scanner.saveCache(f.cache.string()));
    const auto valid = fields(scanner.roms()[0]);
    auto fail = [&](const std::vector<std::array<std::string, 13>> &records) {
        writeCache(f, records);
        assert(!scanner.loadCache(f.cache.string(), f.root.string()));
        assert(scanner.roms().empty());
    };
    auto record = valid;
    record[2] = (f.base / "outside.zip").string();
    std::ofstream(record[2]) << "outside";
    fail({valid, record});
    record = valid; record[0] = "md"; fail({record});
    record = valid; record[2] = (f.root / "ambiguous.bin").string(); record[0] = "md";
    f.put("ambiguous.bin"); fail({record});
    record = valid; record[11] = "unknown extension"; fail({record});
    record = valid; record[4] = std::string(65537, 'x'); fail({record});
    record = valid; record[4] = std::string("nul\0field", 9); fail({record});
    fail({valid, valid});
    writeCache(f, {valid}, 2);
    assert(!scanner.loadCache(f.cache.string(), f.root.string()));
    writeCache(f, {}, 1, 20001);
    assert(!scanner.loadCache(f.cache.string(), f.root.string()));
    writeCache(f, {valid});
    std::ofstream(f.cache, std::ios::app) << "trailing injection";
    assert(!scanner.loadCache(f.cache.string(), f.root.string()));
    writeCache(f, {valid});
    fs::resize_file(f.cache, 4 * 1024 * 1024 + 1);
    assert(!scanner.loadCache(f.cache.string(), f.root.string()));
    writeCache(f, {valid});
    fs::remove(game);
    assert(!scanner.loadCache(f.cache.string(), f.root.string()));
    assert(scanner.scan(f.root.string()) == 0);
    f.put("gba/game.zip");
    writeCache(f, {valid});
    const fs::path otherRoot = f.base / "other-roms";
    fs::create_directory(otherRoot);
    assert(!scanner.loadCache(f.cache.string(), otherRoot.string()));
    fs::rename(f.root / "gba", f.root / "moved");
    fs::create_directory_symlink(f.root / "moved", f.root / "gba");
    assert(!scanner.loadCache(f.cache.string(), f.root.string()));
    std::cout << "ROMSCANNER_CACHE_FAIL_CLOSED_OK\n";
}

static void testAtomicSaveAndEmptyCache() {
    Fixture f;
    ROMScanner scanner;
    assert(scanner.scan(f.root.string()) == 0);
    assert(scanner.saveCache(f.cache.string()));
    assert(scanner.loadCache(f.cache.string(), f.root.string()));
    assert(scanner.roms().empty());
    assert(scanner.scan(std::string("invalid\0root", 12)) == 0);
    const fs::path preserved = f.base / "preserved";
    std::ofstream(preserved) << "keep";
    fs::remove(f.cache);
    fs::create_symlink(preserved, f.cache);
    assert(!scanner.saveCache(f.cache.string()));
    assert(!scanner.loadCache(f.cache.string(), f.root.string()));
    assert(readFile(preserved) == "keep");
    fs::remove(f.cache);
    fs::create_directory(f.cache);
    assert(!scanner.saveCache(f.cache.string()));
    std::cout << "ROMSCANNER_ATOMIC_EMPTY_CACHE_OK\n";
}

static void testPortSmokeRevalidation() {
    Fixture f;
    const fs::path ports = f.base / "portsmoke/ports/demo";
    const fs::path engines = f.base / "portsmoke/engines";
    fs::create_directories(ports); fs::create_directories(engines);
    std::ofstream(ports / "run.sh") << "#!/bin/sh\n";
    std::ofstream(engines / "demo.peng") << "engine fixture";
    auto metadata = [&](const std::string &name) {
        std::ofstream(ports / "portsmoke.conf") << "[PortSmoke]\nName=" << name
            << "\nEngineFile=demo.peng\nScript=run.sh\nABI=o32-hard-float\nLibc=musl\n";
    };
    metadata("Original");
    ROMScanner scanner;
    assert(scanner.scan(f.root.string()) == 1);
    const fs::path aliasStorage = f.base / "storage";
    fs::create_directory(aliasStorage);
    fs::create_directory_symlink(f.root, aliasStorage / "roms");
    fs::create_directory_symlink(f.base / "portsmoke", aliasStorage / "portsmoke");
    assert(scanner.scan((aliasStorage / "roms").string()) == 1);
    assert(scanner.saveCache(f.cache.string()));
    auto record = fields(scanner.roms()[0]);
    record[2] = "/untrusted/cached-script.sh";
    writeCache(f, {record});
    metadata("Revalidated");
    assert(scanner.loadCache(f.cache.string(), f.root.string()));
    assert(scanner.roms().size() == 1 && scanner.roms()[0].name == "Revalidated");
    assert(scanner.roms()[0].path == (ports / "run.sh").string());
    assert(scanner.loadCache(f.cache.string(), (aliasStorage / "roms").string()));
    assert(scanner.roms().size() == 1);
    fs::remove(engines / "demo.peng");
    assert(scanner.loadCache(f.cache.string(), f.root.string()));
    assert(scanner.roms().empty());
    fs::rename(ports, ports.parent_path() / ".moved-demo");
    fs::create_directory_symlink(ports.parent_path() / ".moved-demo", ports);
    std::ofstream(engines / "demo.peng") << "engine fixture";
    assert(scanner.loadCache(f.cache.string(), f.root.string()));
    assert(scanner.roms().empty());
    std::cout << "ROMSCANNER_PORTSMOKE_REVALIDATION_OK\n";
}

int main() {
    testArchivesAndAliases(); testDepthAndPSX(); testMetadataAndCacheRoundTrip();
    testFailClosedCache(); testAtomicSaveAndEmptyCache(); testPortSmokeRevalidation();
    std::cout << "ROMSCANNER_SCAN_CACHE_OK\n";
}
