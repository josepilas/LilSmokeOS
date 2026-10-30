#include "bob_scene.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace portsmoke::sm64 {
namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
std::uint32_t word(const std::uint8_t *p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}
int half(const std::uint8_t *p) {
    unsigned value = (unsigned(p[0]) << 8) | p[1];
    return value >= 32768 ? int(value) - 65536 : int(value);
}
int signedByte(std::uint8_t value) { return value >= 128 ? int(value) - 256 : value; }

class Interpreter {
public:
    explicit Interpreter(const SegmentMemory& memory) : memory_(memory) {}
    SceneMesh run(const std::vector<std::uint32_t>& roots) {
        require(!roots.empty() && roots.size() <= 16, "invalid scene root count");
        for (auto address : roots) {
            valid_.fill(false);
            walk(address);
        }
        require(!mesh_.vertices.empty(), "display lists contain no triangles");
        return std::move(mesh_);
    }
private:
    int texture() {
        if (!textured_) return -1;
        require(imageAddress_ && width_ && height_, "texture used before image/tile setup");
        for (std::size_t i = 0; i < mesh_.textures.size(); ++i) {
            const auto& value = mesh_.textures[i];
            if (value.address == imageAddress_ && value.image.width == width_ && value.image.height == height_)
                return int(i);
        }
        require(mesh_.textures.size() < 64, "scene texture budget exceeded");
        require(imageFormat_ == 0 || imageFormat_ == 3, "unsupported F3D texture format");
        require(imageSize_ == 2, "only checked RGBA16/IA16 scene textures are supported");
        auto bytes = std::size_t(width_) * height_ * 2;
        const auto *raw = memory_.view(imageAddress_, bytes, 2);
        auto converted = convertTexture(imageFormat_ == 0 ? TextureFormat::Rgba16 : TextureFormat::Ia16,
                                        width_, height_, {raw, raw + bytes}, 32768);
        mesh_.textures.push_back({imageAddress_, std::move(converted)});
        return int(mesh_.textures.size() - 1);
    }
    void triangle(std::uint32_t value) {
        require(mesh_.vertices.size() <= 32768 * 3 - 3, "scene triangle budget exceeded");
        int textureIndex = texture();
        auto first = static_cast<std::uint32_t>(mesh_.vertices.size());
        for (unsigned shift : {16u, 8u, 0u}) {
            unsigned encoded = (value >> shift) & 255;
            require(encoded % 10 == 0 && encoded / 10 < 16 && valid_[encoded / 10],
                    "F3D triangle references an unloaded/out-of-range vertex");
            auto vertex = cache_[encoded / 10];
            if (textureIndex >= 0) {
                vertex.s *= scaleS_ / (65536.f * 32.f * width_);
                vertex.t *= scaleT_ / (65536.f * 32.f * height_);
            }
            mesh_.vertices.push_back(vertex);
        }
        bool cull = (geometry_ & 0x2000) != 0;
        if (!mesh_.batches.empty() && mesh_.batches.back().texture == textureIndex &&
            mesh_.batches.back().cullBack == cull) mesh_.batches.back().count += 3;
        else mesh_.batches.push_back({first, 3, textureIndex, cull});
    }
    void vertices(std::uint32_t a, std::uint32_t address) {
        unsigned parameters = (a >> 16) & 255, count = (parameters >> 4) + 1, start = parameters & 15;
        require(start + count <= 16 && (a & 65535) == count * 16, "invalid old F3D vertex DMA");
        const auto *raw = memory_.view(address, count * 16, 8);
        ++mesh_.vertexLoads;
        for (unsigned i = 0; i < count; ++i) {
            const auto *p = raw + 16 * i;
            SceneVertex v{float(half(p)), float(half(p + 2)), float(half(p + 4)),
                          float(half(p + 8)), float(half(p + 10)), {p[12], p[13], p[14], p[15]}};
            if (geometry_ & 0x20000) {
                float light = 0;
                for (unsigned axis = 0; axis < 3; ++axis) light += signedByte(p[12 + axis]) * direction_[axis] / 127.f;
                light = std::clamp(light, 0.f, 1.f);
                for (unsigned channel = 0; channel < 3; ++channel)
                    v.color[channel] = std::uint8_t(std::min(255.f, ambient_[channel] + diffuse_[channel] * light));
            }
            cache_[start + i] = v; valid_[start + i] = true;
        }
    }
    void light(std::uint32_t a, std::uint32_t address) {
        unsigned index = (a >> 16) & 255;
        require((a & 65535) == 16 && (index == 0x86 || index == 0x88), "unsupported F3D move-memory");
        const auto *p = memory_.view(address, 16, 8);
        for (unsigned i = 0; i < 3; ++i) {
            if (index == 0x88) ambient_[i] = p[i];
            else { diffuse_[i] = p[i]; direction_[i] = signedByte(p[8 + i]); }
        }
        if (index == 0x86) {
            float length = std::sqrt(direction_[0] * direction_[0] + direction_[1] * direction_[1] + direction_[2] * direction_[2]);
            require(length > 0, "zero scene light direction");
            for (auto& axis : direction_) axis /= length;
        }
    }
    void walk(std::uint32_t address) {
        require(active_.size() < 32 && std::find(active_.begin(), active_.end(), address) == active_.end(),
                "cyclic/deep F3D display list");
        active_.push_back(address);
        for (;;) {
            require(++mesh_.commands <= 65536, "F3D command budget exceeded");
            const auto *command = memory_.view(address, 8, 8);
            std::uint32_t a = word(command), b = word(command + 4);
            require((address & 0xffffff) <= 0xfffff7, "F3D address overflow");
            address += 8;
            switch (a >> 24) {
                case 0x04: vertices(a, b); break;
                case 0xbf: triangle(b); break;
                case 0x06:
                    require(((a >> 16) & 255) <= 1, "invalid F3D branch mode");
                    walk(b);
                    if (a & 0x10000) { active_.pop_back(); return; }
                    break;
                case 0xb8: active_.pop_back(); return;
                case 0xb6: geometry_ &= ~b; break;
                case 0xb7: geometry_ |= b; break;
                case 0x03: light(a, b); break;
                case 0xbb:
                    require((a & 255) <= 1 && ((a >> 8) & 7) == 0, "unsupported F3D texture tile");
                    textured_ = (a & 1) != 0; scaleS_ = b >> 16; scaleT_ = b & 65535;
                    break;
                case 0xfd:
                    imageAddress_ = b; imageFormat_ = (a >> 21) & 7; imageSize_ = (a >> 19) & 3;
                    memory_.view(b, 2, 2); break;
                case 0xf2: {
                    require((b >> 24) == 0, "unsupported F3D render tile");
                    unsigned left = (a >> 12) & 4095, top = a & 4095, right = (b >> 12) & 4095, bottom = b & 4095;
                    require(left == 0 && top == 0 && right % 4 == 0 && bottom % 4 == 0,
                            "unsupported F3D tile origin/fraction");
                    width_ = right / 4 + 1; height_ = bottom / 4 + 1;
                    require(width_ <= 64 && height_ <= 64, "F3D texture dimensions exceed budget");
                    break;
                }
                case 0xf3: {
                    require(((b >> 24) & 7) == 7 && width_ && height_, "unsupported F3D texture load");
                    auto count = ((b >> 12) & 4095) + 1;
                    require(count == unsigned(width_) * height_, "F3D load-block size differs from render tile");
                    memory_.view(imageAddress_, count * 2, 2);
                    break;
                }
                case 0xbc:
                    // Fog parameters and number-of-lights only; segment remaps
                    // would change address meaning and are deliberately refused.
                    require((a & 255) == 8 || (a & 255) == 2, "unsupported F3D move-word");
                    break;
                case 0xf5: case 0xb9: case 0xba: case 0xf8: case 0xfc:
                case 0xe6: case 0xe7: case 0xe8:
                    // Known Bob material/sync commands. Approximate opaque
                    // texture*shade; not full RDP combiners, fog or blending.
                    break;
                default: throw std::runtime_error("unsupported F3D opcode: " + std::to_string(a >> 24));
            }
        }
    }
    const SegmentMemory& memory_;
    SceneMesh mesh_;
    std::array<SceneVertex, 16> cache_{};
    std::array<bool, 16> valid_{};
    std::vector<std::uint32_t> active_;
    std::uint32_t geometry_ = 0x20000, imageAddress_ = 0;
    unsigned imageFormat_ = 0, imageSize_ = 2;
    std::uint16_t width_ = 0, height_ = 0, scaleS_ = 65535, scaleT_ = 65535;
    bool textured_ = false;
    std::array<float, 3> ambient_{102, 102, 102}, diffuse_{255, 255, 255};
    std::array<float, 3> direction_{0.57735f, 0.57735f, 0.57735f};
};
}

void SegmentMemory::bind(unsigned number, std::vector<std::uint8_t> bytes) {
    require(number > 0 && number < segments_.size() && !bytes.empty() && bytes.size() <= 256 * 1024,
            "invalid scene segment binding");
    segments_[number] = std::move(bytes);
}
const std::uint8_t *SegmentMemory::view(std::uint32_t address, std::size_t bytes, unsigned alignment) const {
    unsigned number = address >> 24, offset = address & 0xffffff;
    require(number > 0 && number < segments_.size() && alignment && offset % alignment == 0,
            "invalid/unbound/unaligned segmented address");
    const auto& data = segments_[number];
    require(!data.empty() && offset <= data.size() && bytes <= data.size() - offset, "scene segment range exceeded");
    return data.data() + offset;
}
SceneMesh interpretF3d(const SegmentMemory& segments, const std::vector<std::uint32_t>& roots) {
    return Interpreter(segments).run(roots);
}
SceneMesh loadBobScene(const std::filesystem::path& path) {
    RuntimeAssets assets(path, bobDefinition);
    auto bob = assets.pack().load(bobSegments[0].name), generic = assets.pack().load(bobSegments[1].name);
    require(sha1(bob) == "d43df030da8aed4db31a27cb1a5c3b2d1f30c98e" &&
            sha1(generic) == "a3164ccab085c1c5275bb6a5eb1ae7a96f8cf7e5", "scene segment differs from supported US ROM");
    SegmentMemory memory; memory.bind(7, std::move(bob)); memory.bind(9, std::move(generic));
    return interpretF3d(memory, {bobRoots.begin(), bobRoots.end()});
}
}
