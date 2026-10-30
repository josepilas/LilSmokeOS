#include "runtime_assets.hpp"
#include <cstring>
#include <stdexcept>

namespace portsmoke::sm64 {
namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

std::size_t textureBytes(TextureFormat format, std::uint16_t width, std::uint16_t height,
                         std::size_t budget) {
    require(width && height && budget && budget <= maxTextureBudget, "invalid texture dimensions/budget");
    const std::size_t pixels = std::size_t(width) * height;
    unsigned bits = 0;
    switch (format) {
        case TextureFormat::Rgba16: case TextureFormat::Ia16: bits = 16; break;
        case TextureFormat::Ia1: bits = 1; break;
        case TextureFormat::Ia4: bits = 4; break;
        case TextureFormat::Ia8: bits = 8; break;
        default: throw std::runtime_error("not a supported linear texture (raw/skybox/cake excluded)");
    }
    // Check divisions before multiplication on o32. Pixels never exceed budget/4.
    require(pixels <= budget / 4, "decoded texture exceeds memory budget");
    auto rawBytes = (pixels * bits + 7) / 8;
    require(rawBytes <= budget - pixels * 4, "texture input+output exceed memory budget");
    return rawBytes;
}
}

Texture convertTexture(TextureFormat format, std::uint16_t width, std::uint16_t height,
                       const std::vector<std::uint8_t>& raw, std::size_t budget) {
    require(raw.size() == textureBytes(format, width, height, budget), "texture payload size mismatch");
    Texture result{width, height, std::vector<std::uint8_t>(std::size_t(width) * height * 4)};
    for (std::size_t i = 0; i < std::size_t(width) * height; ++i) {
        auto *pixel = result.rgba.data() + i * 4;
        unsigned intensity = 0, alpha = 0;
        switch (format) {
            case TextureFormat::Rgba16: {
                unsigned word = (unsigned(raw[2 * i]) << 8) | raw[2 * i + 1];
                pixel[0] = ((word >> 11) & 31) * 255 / 31;
                pixel[1] = ((word >> 6) & 31) * 255 / 31;
                pixel[2] = ((word >> 1) & 31) * 255 / 31;
                pixel[3] = (word & 1) ? 255 : 0;
                continue;
            }
            case TextureFormat::Ia1:
                intensity = alpha = ((raw[i / 8] >> (7 - i % 8)) & 1) ? 255 : 0;
                break;
            case TextureFormat::Ia4: {
                unsigned nibble = (raw[i / 2] >> (i % 2 ? 0 : 4)) & 15;
                // Match pinned n64graphics.c: 3-bit intensity expands by 36 (max 252).
                intensity = (nibble >> 1) * 36; alpha = (nibble & 1) ? 255 : 0;
                break;
            }
            case TextureFormat::Ia8:
                intensity = (raw[i] >> 4) * 17; alpha = (raw[i] & 15) * 17;
                break;
            case TextureFormat::Ia16:
                intensity = raw[2 * i]; alpha = raw[2 * i + 1];
                break;
            default: throw std::runtime_error("unsupported texture format");
        }
        pixel[0] = pixel[1] = pixel[2] = intensity; pixel[3] = alpha;
    }
    return result;
}

RuntimeAssets::RuntimeAssets(const std::filesystem::path& path, const GameDefinition& definition)
    : pack_(path) {
    require(definition.assets && pack_.romSha1() == definition.romSha1 &&
            pack_.manifestSha1() == definition.manifestSha1 &&
            pack_.entries().size() == definition.count, "runtime pack belongs to another game version");
    // The digest field is not authority for its index: match every pinned descriptor.
    for (std::size_t i = 0; i < definition.count; ++i) {
        const auto& expected = definition.assets[i];
        const auto& actual = pack_.entries()[i];
        require(actual.name == expected.name && actual.size == expected.size &&
                actual.width == expected.width && actual.height == expected.height &&
                actual.format == expected.format, "runtime pack descriptor differs from pinned manifest");
    }
}

Texture RuntimeAssets::texture(const std::string& name, std::size_t budget) const {
    const auto& asset = pack_.entry(name);
    auto format = static_cast<TextureFormat>(asset.format);
    require(asset.size == textureBytes(format, asset.width, asset.height, budget),
            "runtime texture payload size mismatch");
    return convertTexture(format, asset.width, asset.height, pack_.load(name), budget);
}
}
