#include "state-store.h"
#include <cassert>
#include <sys/mount.h>
#include <sys/stat.h>

// This tests filesystem guards, not emulation or the engine serializer.
PcsxSaveFuncs SaveFuncs{};
int SaveState(const char *) { std::abort(); }
int LoadState(const char *) { std::abort(); }
int CheckState(const char *, bool *, bool, uint16_t *) { return 0; }

int main() {
    const char *root = "/run/pcsx-readonly";
    assert(mkdir(root, 0700) == 0);
    assert(mount("tmpfs", root, "tmpfs", 0, "size=8m") == 0);
    setenv("LILSMOKE_STATE_ROOT", root, 1);
    PcsxStateStore existing, missing;
    existing.init("/readonly-fixture-existing.cue");
    missing.init("/readonly-fixture-missing.cue");
    std::filesystem::create_directories(existing.path(0).parent_path());
    gzFile file = gzopen(existing.path(0).c_str(), "wb1");
    assert(file);
    char zero[8192]{};
    for (int i = 0; i < 384; ++i)
        assert(gzwrite(file, zero, sizeof zero) == sizeof zero);
    assert(gzclose(file) == Z_OK);
    const auto before = std::filesystem::file_size(existing.path(0));
    assert(mount(nullptr, root, nullptr, MS_REMOUNT | MS_RDONLY, nullptr) == 0);
    std::string message;
    assert(!existing.save(0, message));
    assert(message == "Storage read-only; repair SD");
    assert(!missing.save(0, message));
    assert(message == "Storage read-only; repair SD");
    assert(!existing.load(0, message));
    assert(message == "Storage read-only; repair SD");
    assert(std::filesystem::file_size(existing.path(0)) == before);
    assert(mount(nullptr, root, nullptr, MS_REMOUNT, nullptr) == 0);
    assert(umount(root) == 0);
    std::puts("PCSX_RECOVERY_PASS scenario=readonly kernel_remount=1 serializer_calls=0 old_slot_preserved=1");
}
