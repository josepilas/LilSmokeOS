#include "bob_scene.hpp"
#include <iostream>
#include <stdexcept>

using namespace portsmoke::sm64;
unsigned checks = 0;
void check(bool value) { ++checks; if (!value) throw std::runtime_error("scene check " + std::to_string(checks)); }
template<class F> void rejects(F f) { bool bad = false; try { f(); } catch (...) { bad = true; } check(bad); }
void word(std::vector<std::uint8_t>& b, unsigned at, unsigned value) {
    for (unsigned i = 0; i < 4; ++i) b[at + i] = std::uint8_t(value >> (24 - i * 8));
}
void command(std::vector<std::uint8_t>& b, unsigned at, unsigned a, unsigned c) {
    word(b, at, a); word(b, at + 4, c);
}
int main() {
    try {
        SegmentMemory memory;
        rejects([&] { memory.view(0x07000000, 8); });
        rejects([&] { memory.bind(0, {1}); });
        rejects([&] { memory.bind(16, {1}); });
        rejects([&] { memory.bind(7, std::vector<std::uint8_t>(256 * 1024 + 1)); });
        std::vector<std::uint8_t> data(256);
        command(data, 0, 0x04000010, 0x07000080); // one vertex.
        command(data, 8, 0xBF000000, 0);
        command(data, 16, 0xB8000000, 0);
        data[140] = 0; data[141] = 127; data[142] = 0; data[143] = 255;
        memory.bind(7, data);
        check(memory.view(0x07000100, 0) != nullptr);
        rejects([&] { memory.view(0x07000100, 1); });
        rejects([&] { memory.view(0x07000001, 8, 8); });
        rejects([&] { memory.view(0x17000000, 8); });
        rejects([&] { memory.view(0x07fffffc, 8); });
        auto mesh = interpretF3d(memory, {0x07000000});
        check(mesh.vertices.size() == 3 && mesh.commands == 3 && mesh.vertexLoads == 1);
        rejects([&] { interpretF3d(memory, {}); });
        auto bad = data; command(bad, 8, 0xBF000000, 0x0a0000); memory.bind(7, bad);
        rejects([&] { interpretF3d(memory, {0x07000000}); });
        bad = data; command(bad, 0, 0x04ff0100, 0x07000080); memory.bind(7, bad);
        rejects([&] { interpretF3d(memory, {0x07000000}); });
        bad = data; command(bad, 0, 0x04000010, 0x070000f8); memory.bind(7, bad);
        rejects([&] { interpretF3d(memory, {0x07000000}); });
        bad = data; command(bad, 0, 0x06000000, 0x07000000); memory.bind(7, bad);
        rejects([&] { interpretF3d(memory, {0x07000000}); });
        bad = data; command(bad, 0, 0x06000000, 0x07000018); command(bad, 24, 0x06000000, 0x07000000);
        memory.bind(7, bad); rejects([&] { interpretF3d(memory, {0x07000000}); });
        bad = data; command(bad, 0, 0x01000000, 0); memory.bind(7, bad);
        rejects([&] { interpretF3d(memory, {0x07000000}); }); // matrices not silently ignored.
        bad = data; command(bad, 0, 0xBC000006, 0x07000000); memory.bind(7, bad);
        rejects([&] { interpretF3d(memory, {0x07000000}); }); // segment remap refused.
        bad = data; command(bad, 0, 0xFD100000, 0x09000000); memory.bind(7, bad);
        rejects([&] { interpretF3d(memory, {0x07000000}); });
        bad = data; command(bad, 0, 0xF2000000, 0x00ffc0fc); memory.bind(7, bad);
        rejects([&] { interpretF3d(memory, {0x07000000}); }); // texture over 64x64.
        bad = data; command(bad, 0, 0xF2001000, 0x0007c07c); memory.bind(7, bad);
        rejects([&] { interpretF3d(memory, {0x07000000}); });
        bad = data; command(bad, 0, 0x06010000, 0x07000008); command(bad, 8, 0x04000010, 0x07000080);
        command(bad, 16, 0xBF000000, 0); command(bad, 24, 0xB8000000, 0); memory.bind(7, bad);
        check(interpretF3d(memory, {0x07000000}).vertices.size() == 3);
        std::cout << "PASS " << checks << " checked segmented/F3D scene guards\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
