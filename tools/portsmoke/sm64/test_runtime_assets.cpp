#include "runtime_assets.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace portsmoke::sm64;
unsigned checks = 0;
void check(bool value) {
    ++checks;
    if (!value) throw std::runtime_error("texture check " + std::to_string(checks) + " failed");
}
template<class F> void rejects(F action) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    check(rejected);
}

int main() {
    try {
        for (unsigned word = 0; word < 65536; ++word) {
            std::vector<std::uint8_t> bytes{std::uint8_t(word >> 8), std::uint8_t(word)};
            auto t = convertTexture(TextureFormat::Rgba16, 1, 1, bytes);
            check(t.width == 1 && t.height == 1 && t.rgba == std::vector<std::uint8_t>({
                std::uint8_t(((word >> 11) & 31) * 255 / 31),
                std::uint8_t(((word >> 6) & 31) * 255 / 31),
                std::uint8_t(((word >> 1) & 31) * 255 / 31),
                std::uint8_t((word & 1) ? 255 : 0)}));
            t = convertTexture(TextureFormat::Ia16, 1, 1, bytes);
            check(t.rgba == std::vector<std::uint8_t>({bytes[0], bytes[0], bytes[0], bytes[1]}));
        }
        for (unsigned byte = 0; byte < 256; ++byte) {
            auto t = convertTexture(TextureFormat::Ia8, 1, 1, {std::uint8_t(byte)});
            auto intensity = std::uint8_t((byte >> 4) * 17);
            check(t.rgba == std::vector<std::uint8_t>({intensity, intensity, intensity, std::uint8_t((byte & 15) * 17)}));
            t = convertTexture(TextureFormat::Ia4, 2, 1, {std::uint8_t(byte)});
            for (unsigned i = 0; i < 2; ++i) {
                unsigned nibble = (byte >> (i ? 0 : 4)) & 15;
                check(t.rgba[4 * i] == (nibble >> 1) * 36 &&
                      t.rgba[4 * i + 3] == ((nibble & 1) ? 255 : 0));
            }
            t = convertTexture(TextureFormat::Ia1, 8, 1, {std::uint8_t(byte)});
            for (unsigned i = 0; i < 8; ++i)
                for (unsigned channel = 0; channel < 4; ++channel)
                    check(t.rgba[i * 4 + channel] == ((byte & (1 << (7 - i))) ? 255 : 0));
        }
        check(convertTexture(TextureFormat::Ia4, 3, 1, {0xf1, 0x80}).rgba.size() == 12);
        check(convertTexture(TextureFormat::Ia1, 13, 14, std::vector<std::uint8_t>(23)).rgba.size() == 728);
        for (unsigned fmt = 0; fmt < 256; ++fmt) {
            if (fmt >= 1 && fmt <= 5) continue;
            rejects([&] { convertTexture(static_cast<TextureFormat>(fmt), 1, 1, {0}); });
        }
        rejects([] { convertTexture(TextureFormat::Ia8, 0, 1, {}); });
        rejects([] { convertTexture(TextureFormat::Ia8, 1, 0, {}); });
        rejects([] { convertTexture(TextureFormat::Rgba16, 1, 1, {0}); });
        rejects([] { convertTexture(TextureFormat::Ia8, 1, 1, {0, 0}); });
        rejects([] { convertTexture(TextureFormat::Ia8, 65535, 65535, {}); });
        rejects([] { convertTexture(TextureFormat::Ia8, 1, 1, {0}, 0); });
        rejects([] { convertTexture(TextureFormat::Ia8, 1, 1, {0}, maxTextureBudget + 1); });
        rejects([] { convertTexture(TextureFormat::Rgba16, 1, 1, {0, 0}, 5); });
        check(convertTexture(TextureFormat::Rgba16, 1, 1, {0, 0}, 6).rgba.size() == 4);
        std::cout << "PASS " << checks << " typed texture checks\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
