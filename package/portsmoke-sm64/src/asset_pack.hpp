#pragma once
#include <cstdint>
#include <filesystem>
#include <cstdio>
#include <memory>
#include <functional>
#include <string>
#include <vector>

namespace portsmoke::sm64 {

struct AssetSpec {
    const char *name;
    std::int32_t mio0Block;
    std::uint32_t offset, size;
    std::uint16_t width, height;
    std::uint8_t format;
};

struct GameDefinition {
    const char *romSha1;
    std::uint32_t romBytes;
    const char *manifestSha1;
    const AssetSpec *assets;
    std::size_t count;
};

struct AssetEntry {
    std::string name;
    std::uint32_t offset, size;
    std::uint16_t width, height;
    std::uint8_t format;
};

std::string sha1(const std::vector<std::uint8_t>& data);
std::vector<std::uint8_t> decodeMio0(const std::vector<std::uint8_t>& rom, std::size_t base);
std::vector<std::uint8_t> normalizedRom(const std::filesystem::path& rom, const GameDefinition& definition);
std::filesystem::path discoverRom(const std::filesystem::path& original, const GameDefinition& definition,
                                 bool allowMissing = false);

// A file-backed, endian-neutral private cache. Never bundle it in a .lilpck.
class AssetPack {
public:
    explicit AssetPack(const std::filesystem::path& path);
    const std::vector<AssetEntry>& entries() const { return entries_; }
    const std::string& romSha1() const { return romSha1_; }
    const std::string& manifestSha1() const { return manifestSha1_; }
    const AssetEntry& entry(const std::string& name) const;
    void read(const std::string& name, std::uint32_t offset, void *destination, std::size_t bytes) const;
    std::vector<std::uint8_t> load(const std::string& name) const;
private:
    // Keep the verified inode open, even if a later extraction replaces its path.
    std::shared_ptr<std::FILE> file_;
    std::string romSha1_, manifestSha1_;
    std::vector<AssetEntry> entries_;
};

// True when extracted, false for an already verified cache. Throws on failure.
bool prepare(const std::filesystem::path& rom, const std::filesystem::path& output,
             const GameDefinition& definition,
             const std::function<void(std::uint64_t, std::uint64_t)>& progress = {});

}
