#pragma once
#include "runtime_assets.hpp"
#include <array>

namespace portsmoke::sm64 {
// Offset/size metadata only, from pinned n64decomp US symbols/assets.json.
inline constexpr AssetSpec bobSegments[] = {
    {"scene/bob/segment7.bin", 4178608, 0, 71618, 0, 0, 0},
    {"scene/bob/segment9.bin", 3330160, 0, 47104, 0, 0, 0},
};
inline constexpr GameDefinition bobDefinition{
    "9bef1128717f958171a4afac3ed78ee2bb4e86ce", 8388608,
    "e79154b211e620cd8c57104c93f5f233cfd6c8e7", bobSegments, 2};
inline constexpr std::array<std::uint32_t, 6> bobRoots{
    0x07004390, 0x07009D80, 0x0700a470, 0x0700a920, 0x0700dd18, 0x0700e338};

struct SceneVertex {
    float x, y, z, s, t;
    std::array<std::uint8_t, 4> color;
};
struct SceneTexture {
    std::uint32_t address;
    Texture image;
};
struct SceneBatch {
    std::uint32_t first, count;
    int texture; // -1 means vertex-color only.
    bool cullBack;
};
struct SceneMesh {
    std::vector<SceneVertex> vertices;
    std::vector<SceneBatch> batches;
    std::vector<SceneTexture> textures;
    std::uint32_t commands = 0, vertexLoads = 0;
};

// Small segment table, not an N64 emulator or arbitrary native-pointer reader.
class SegmentMemory {
public:
    void bind(unsigned number, std::vector<std::uint8_t> bytes);
    const std::uint8_t *view(std::uint32_t address, std::size_t bytes, unsigned alignment = 1) const;
private:
    std::array<std::vector<std::uint8_t>, 16> segments_;
};

SceneMesh interpretF3d(const SegmentMemory& segments, const std::vector<std::uint32_t>& roots);
SceneMesh loadBobScene(const std::filesystem::path& packPath);
}
