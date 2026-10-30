#pragma once
#include "asset_pack.hpp"

namespace portsmoke::sm64 {

// Values are the existing v1 pack wire tags, not libultra enum values.
enum class TextureFormat : std::uint8_t {
    Rgba16 = 1, Ia1 = 2, Ia4 = 3, Ia8 = 4, Ia16 = 5
};

struct Texture {
    std::uint16_t width, height;
    // Byte-addressed R,G,B,A. No native-endian uint32_t pixels or PNG decoding.
    std::vector<std::uint8_t> rgba;
};

constexpr std::size_t defaultTextureBudget = 512 * 1024;
constexpr std::size_t maxTextureBudget = 1024 * 1024;

// Budget covers input plus output allocations, not the pack index/caller memory.
Texture convertTexture(TextureFormat format, std::uint16_t width, std::uint16_t height,
                       const std::vector<std::uint8_t>& raw,
                       std::size_t budget = defaultTextureBudget);

class RuntimeAssets {
public:
    RuntimeAssets(const std::filesystem::path& path, const GameDefinition& definition);
    const AssetPack& pack() const { return pack_; }
    Texture texture(const std::string& name, std::size_t budget = defaultTextureBudget) const;
    // Opaque sequences/BE soundbanks/demo bytes stay bounded and file-backed.
    void read(const std::string& name, std::uint32_t offset, void *out, std::size_t bytes) const {
        pack_.read(name, offset, out, bytes);
    }
private:
    AssetPack pack_;
};

}
