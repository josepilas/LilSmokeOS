// SPDX-License-Identifier: MPL-2.0
#include "extraction.hpp"
#include "bob_scene.hpp"
#include "mario_actor.hpp"
#include "runtime_assets.hpp"
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace portsmoke::sm64 {
namespace {
#include "asset_manifest.inc"
#include "segment_manifest.inc"
    constexpr GameDefinition allAssets { "9bef1128717f958171a4afac3ed78ee2bb4e86ce", 8388608,
        manifestIdentity, assetSpecs, sizeof(assetSpecs) / sizeof(assetSpecs[0]) };
    constexpr GameDefinition allSegments { "9bef1128717f958171a4afac3ed78ee2bb4e86ce", 8388608,
        segmentIdentity, segmentSpecs, sizeof(segmentSpecs) / sizeof(segmentSpecs[0]) };
    struct Job {
        const char* name;
        const GameDefinition* definition;
    };
    constexpr std::array<Job, 4> jobs { { { "bob-us-v1.pdat", &bobDefinition }, { "mario-us-v1.pdat", &marioDefinition },
        { "sm64-us-v1.pdat", &allAssets }, { "segments-us-v1.pdat", &allSegments } } };
    void check(bool ok, const char* message)
    {
        if (!ok)
            throw std::runtime_error(message);
    }
    void cancelled(const ExtractionStatus& s) { check(!s.cancel.load(), "Extraction cancelled. Original ROM retained."); }
    std::uint64_t bytes(const GameDefinition& d)
    {
        std::uint64_t n = 0;
        for (std::size_t i = 0; i < d.count; ++i)
            n += d.assets[i].size;
        return n;
    }
    void syncDirectory(const std::filesystem::path& directory)
    {
        int fd = ::open(directory.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
        check(fd >= 0, "Cannot open extraction directory for sync.");
        int result = ::fsync(fd), saved = errno;
        ::close(fd);
        if (result && saved == EINVAL)
            ::sync();
        else
            check(result == 0, "Extraction directory sync failed.");
    }
    void removeOriginal(const std::filesystem::path& original, const std::filesystem::path& file,
        const struct stat& expected, ExtractionStatus& status)
    {
        cancelled(status);
        check(file.parent_path().lexically_normal() == original.lexically_normal(), "Refusing deletion outside original/.");
        // Revalidate the exact supported input after every durable pack commits.
        normalizedRom(file, bobDefinition);
        int directory = ::open(original.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
        check(directory >= 0, "Cannot pin original/ for safe removal.");
        struct stat current { };
        std::string name = file.filename().string();
        bool valid = ::fstatat(directory, name.c_str(), &current, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(current.st_mode) && current.st_nlink == 1 && current.st_dev == expected.st_dev && current.st_ino == expected.st_ino && current.st_size == expected.st_size && current.st_mtim.tv_sec == expected.st_mtim.tv_sec && current.st_mtim.tv_nsec == expected.st_mtim.tv_nsec;
        if (!valid) {
            ::close(directory);
            throw std::runtime_error("Original ROM changed during extraction; retained.");
        }
        if (status.cancel.load()) {
            ::close(directory);
            cancelled(status);
        }
        int result = ::unlinkat(directory, name.c_str(), 0);
        if (result) {
            ::close(directory);
            throw std::runtime_error("Assets verified, but original ROM removal failed.");
        }
        int synced = ::fsync(directory), saved = errno;
        ::close(directory);
        if (synced && saved == EINVAL)
            ::sync();
        else
            check(synced == 0, "ROM removal needs a directory sync; assets remain verified.");
        status.originalRemoved = true;
    }
}
const GameDefinition& allAssetDefinition() { return allAssets; }
const GameDefinition& allSegmentDefinition() { return allSegments; }
void verifyGameData(const std::filesystem::path& cache)
{
    for (const auto& job : jobs) {
        RuntimeAssets verified(cache / job.name, *job.definition);
    }
    // In addition to checksums and schema, validate the playable data decoders.
    (void)loadBobScene(cache / jobs[0].name);
    (void)loadMarioActor(cache / jobs[1].name);
}
void extractGameData(const std::filesystem::path& original, const std::filesystem::path& cache, ExtractionStatus& status)
{
    try {
        check(original.filename() == "original" && cache.filename() == ".sm64" && original.parent_path().lexically_normal() == cache.parent_path().lexically_normal(),
            "Extraction requires the app's sibling original/ and .sm64/ directories.");
        std::uint64_t total = 0;
        for (const auto& job : jobs)
            total += bytes(*job.definition);
        check(total <= 64 * 1024 * 1024, "Combined extraction budget exceeded.");
        status.total = total;
        cancelled(status);
        auto file = discoverRom(original, bobDefinition, true);
        if (file.empty()) {
            verifyGameData(cache);
            status.usedCache = true;
            status.completed = total;
        } else {
            struct stat before { };
            check(::lstat(file.c_str(), &before) == 0 && S_ISREG(before.st_mode) && before.st_nlink == 1,
                "Original ROM must be a single regular file, not a link.");
            std::uint64_t completed = 0, extracted = 0;
            for (const auto& job : jobs) {
                cancelled(status);
                status.phase = ExtractionPhase::Extracting;
                auto size = bytes(*job.definition);
                std::uint64_t wrote = 0;
                bool created = prepare(file, cache / job.name, *job.definition, [&](std::uint64_t done, std::uint64_t) {
                    cancelled(status);
                    wrote = done;
                    status.completed = completed + done;
                    status.extracted = extracted + done;
                });
                completed += size;
                if (created)
                    extracted += wrote;
                status.completed = completed;
                status.extracted = extracted;
            }
            cancelled(status);
            status.phase = ExtractionPhase::Verifying;
            verifyGameData(cache);
            syncDirectory(cache);
            cancelled(status);
            status.phase = ExtractionPhase::RemovingOriginal;
            removeOriginal(original, file, before, status);
        }
        status.phase = ExtractionPhase::Complete;
    } catch (const std::exception& e) {
        status.error = e.what();
    }
    status.done.store(true, std::memory_order_release);
}
}
