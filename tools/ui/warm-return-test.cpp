#include "UiWarmReturn.h"
#include "ROMScanner.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using namespace lilsmoke;

int main() {
    char pattern[] = "/tmp/lilsmoke-warm-test-XXXXXX";
    const char *created = mkdtemp(pattern);
    assert(created);
    const fs::path base = created, receipt = base / "receipt", state = base / "state";
    for (bool force : {false, true}) for (bool boot : {false, true})
        for (bool warm : {false, true}) for (bool dirty : {false, true})
            assert(warmReturnMayLoadCache(force, boot, warm, dirty) ==
                   (!force && !dirty && (warm || !boot)));
    unsetenv("LILSMOKE_STOCK_SUPERVISOR");
    setenv("LILSMOKE_UI_WARM_RETURN", "1", 1);
    setenv("LILSMOKE_UI_SUPERVISOR_TOKEN", "42:1", 1);
    UiWarmReturn untrusted;
    assert(!untrusted.warm && !untrusted.recordHandoff(receipt));
    assert(!getenv("LILSMOKE_UI_WARM_RETURN") && !getenv("LILSMOKE_UI_SUPERVISOR_TOKEN"));
    setenv("LILSMOKE_STOCK_SUPERVISOR", "1", 1);
    for (const auto *token : {"", ":1", "42:", "42:1:2", "42:../1", "42:x"}) {
        setenv("LILSMOKE_UI_WARM_RETURN", "1", 1);
        setenv("LILSMOKE_UI_SUPERVISOR_TOKEN", token, 1);
        assert(!UiWarmReturn().warm);
    }
    setenv("LILSMOKE_UI_SUPERVISOR_TOKEN", "42:7", 1);
    setenv("LILSMOKE_UI_WARM_RETURN", "1", 1);
    UiWarmReturn trusted;
    assert(trusted.warm && trusted.token == "42:7");
    assert(!UiWarmReturn().warm);
    assert(trusted.recordHandoff(receipt));
    assert(!trusted.recordHandoff(receipt));
    fs::remove(receipt);
    fs::create_symlink(state, receipt);
    assert(!trusted.recordHandoff(receipt) && !fs::exists(state));
    fs::remove(receipt);
    std::cout << "WARM_HINT_POLICY_RECEIPT_OK\n";

    fs::create_directories(base / "roms/nes");
    std::ofstream(base / "roms/nes/one.nes") << "fixture";
    ROMScanner scanner;
    assert(scanner.scan((base / "roms").string()) == 1);
    assert(scanner.saveCache((base / "cache").string()));
    assert(scanner.loadCache((base / "cache").string(), (base / "roms").string()));
    auto snapshot = [&]() {
        UiPlaylistSnapshot s;
        s.file((base / "cache").string());
        s.file((base / "playlist").string());
        s.file((base / "config").string());
        s.file((base / "core").string());
        return s.data();
    };
    assert(!unchangedUiPlaylists(snapshot(), state));
    assert(durableReplace(state, snapshot()));
    assert(unchangedUiPlaylists(snapshot(), state));
    for (const auto *name : {"playlist", "config", "core", "cache"}) {
        const auto before = snapshot();
        std::ofstream(base / name, std::ios::app) << "changed";
        assert(snapshot() != before && !unchangedUiPlaylists(snapshot(), state));
        assert(durableReplace(state, snapshot()));
    }
    assert(!scanner.loadCache((base / "cache").string(), (base / "roms").string()));
    assert(scanner.scan((base / "roms").string()) == 1);
    assert(scanner.saveCache((base / "cache").string()));
    fs::remove(base / "roms/nes/one.nes");
    assert(!scanner.loadCache((base / "cache").string(), (base / "roms").string()));
    assert(scanner.scan((base / "roms").string()) == 0);
    fs::remove(state);
    fs::create_symlink(base / "cache", state);
    assert(!unchangedUiPlaylists(snapshot(), state));
    std::cout << "WARM_CACHE_INVALIDATION_PLAYLIST_WITNESS_OK\n";
    fs::remove_all(base);
}
