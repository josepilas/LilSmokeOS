#include "gameplay.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace portsmoke::sm64;
namespace {
unsigned checks = 0;
void expect(bool ok, const char* message)
{
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
template <class F>
void reject(F operation)
{
    bool caught = false;
    try {
        operation();
    } catch (const std::exception&) {
        caught = true;
    }
    expect(caught, "invalid collision accepted");
}
void word(std::vector<std::uint8_t>& data, int value)
{
    data.push_back((unsigned(value) >> 8) & 255);
    data.push_back(unsigned(value) & 255);
}
std::vector<std::uint8_t> fixture()
{
    std::vector<std::uint8_t> data;
    word(data, 0x40);
    word(data, 7);
    for (auto p : std::array<Position, 7> { { { -8191, 0, -8191 }, { -8191, 0, 8191 }, { 8191, 0, -8191 }, { 8191, 0, 8191 },
             { 0, 0, -1000 }, { 0, 1000, -1000 }, { 0, 0, 1000 } } }) {
        word(data, int(p.x));
        word(data, int(p.y));
        word(data, int(p.z));
    }
    word(data, 0);
    word(data, 3);
    for (int index : { 0, 1, 2, 2, 1, 3, 4, 5, 6 })
        word(data, index);
    word(data, 0x41);
    return data;
}
void movement(const CollisionWorld& world)
{
    PlayerState p;
    resetPlayer(p, world);
    auto start = p.position;
    expect(p.grounded, "spawn not grounded");
    PlayerInput input;
    input.cameraYaw = p.yaw + 3.1415926536f;
    input.forward = 1;
    for (unsigned i = 0; i < 25; ++i)
        stepPlayer(p, input, world);
    expect(std::hypot(p.position.x - start.x, p.position.z - start.z) > 150, "held movement did not move player");
    expect(p.speed > 20, "movement acceleration");
    input.forward = 0;
    for (unsigned i = 0; i < 35; ++i)
        stepPlayer(p, input, world);
    expect(p.speed == 0, "release did not stop movement");
    start = p.position;
    input.jumpPressed = true;
    input.jumpHeld = true;
    stepPlayer(p, input, world);
    expect(!p.grounded && p.jumps == 1, "jump edge did not trigger");
    input.jumpPressed = false;
    float peak = p.position.y;
    for (unsigned i = 0; i < 55; ++i) {
        stepPlayer(p, input, world);
        peak = std::max(peak, p.position.y);
        expect(std::isfinite(p.position.y), "nonfinite physics");
    }
    expect(peak > start.y + 150, "held jump too short");
    expect(p.grounded && p.landings == 1, "jump did not land");
    expect(std::abs(p.position.y - start.y) < 1, "landing missed floor");
    PlayerState shortJump;
    resetPlayer(shortJump, world);
    input.jumpPressed = true;
    input.jumpHeld = true;
    stepPlayer(shortJump, input, world);
    input.jumpPressed = false;
    input.jumpHeld = false;
    float shortPeak = shortJump.position.y;
    for (unsigned i = 0; i < 55; ++i) {
        stepPlayer(shortJump, input, world);
        shortPeak = std::max(shortPeak, shortJump.position.y);
    }
    expect(shortPeak < peak - 50, "variable jump height not applied");
    input.jumpPressed = false;
    input.jumpHeld = false;
    unsigned jumps = p.jumps;
    for (unsigned i = 0; i < 40; ++i)
        stepPlayer(p, input, world);
    expect(p.jumps == jumps, "jump retriggered without new press");
    p.position.y = -12000;
    stepPlayer(p, input, world);
    expect(p.respawns == 1 && p.grounded, "fall recovery");
}
}
int main(int argc, char** argv)
{
    try {
        auto bytes = fixture();
        CollisionWorld world(bytes);
        expect(world.size() == 3, "surface decode count");
        expect(world.references() <= 65536, "grid memory bound");
        expect(world.floor(-6558, 0, 6464).surface && world.floor(-6558, 0, 6464).height == 0, "spawn floor lookup");
        expect(!world.floor(9000, 0, 0).surface, "world bounds");
        expect(!world.floor(NAN, 0, 0).surface, "NaN floor");
        Position wall { 25, 0, 0 };
        expect(world.walls(wall, 100) == 1 && std::abs(wall.x - 50) < 0.01f, "wall push");
        Position far { 80, 0, 0 };
        expect(world.walls(far, 100) == 0 && far.x == 80, "distant wall push");
        expect(std::abs(world.cameraDistance({ 25, 100, 0 }, { -1, 0, 0 }, 100) - 25) < 0.01f, "camera ray did not stop at wall");
        reject([&] { world.walls(wall, 100, 51); });
        reject([&] { world.walls(wall, NAN); });
        for (std::size_t n = 0; n < bytes.size(); ++n)
            reject([&] { CollisionWorld bad({ bytes.begin(), bytes.begin() + n }); });
        auto bad = bytes;
        bad[0] = 0xff;
        reject([&] { CollisionWorld wrong(bad); });
        bad = bytes;
        bad[6 + 7 * 6] = 0xff;
        bad[7 + 7 * 6] = 0xff;
        reject([&] { CollisionWorld wrong(bad); });
        movement(world);
        bool original = argc == 4 && std::string(argv[1]) == "--original";
        if (original)
            prepare(discoverRom(argv[2], bobDefinition), argv[3], bobDefinition);
        if (original || (argc == 3 && std::string(argv[1]) == "--pack")) {
            auto bob = loadBobCollision(argv[original ? 3 : 2]);
            movement(bob);
            // Longer real-user hold reaches the stone platform, unlike the
            // short smoke test. Verify ascent/landing never leaves feet in it.
            PlayerState p;
            resetPlayer(p, bob);
            PlayerInput input;
            input.cameraYaw = p.yaw + 3.1415926536f;
            for (unsigned tick = 0; tick < 180; ++tick) {
                input.forward = tick < 42 ? 1 : 0;
                input.jumpPressed = tick >= 51 && p.grounded && p.jumps == 0;
                input.jumpHeld = tick >= 51 && tick < 110;
                stepPlayer(p, input, bob);
                auto floor = bob.floor(p.position.x, p.position.y, p.position.z);
                expect(!floor.surface || p.position.y >= floor.height - 0.1f,
                    "long hold/jump put player inside terrain");
                // Bob has real below-ground cannon pits. Below spawn height is
                // not itself penetration; the referenced collision floor is.
            }
            std::cout << "LongHoldPosition=" << p.position.x << ',' << p.position.y << ',' << p.position.z
                      << " Grounded=" << p.grounded << " Jumps=" << p.jumps << " Respawns=" << p.respawns << std::endl;
            expect(p.jumps == 1 && p.grounded, "long hold/jump failed to settle");
            PlayerState spawn;
            resetPlayer(spawn, bob);
            std::cout << "BobCollisionSurfaces=" << bob.size() << " SpatialReferences=" << bob.references()
                      << " Spawn=" << spawn.position.x << ',' << spawn.position.y << ',' << spawn.position.z << '\n';
        } else if (argc != 1)
            throw std::runtime_error("usage: test-gameplay [--pack private-bob.pdat | --original directory pack]");
        std::cout << "PASS " << checks << " collision/movement/jump/landing/guard checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL gameplay: " << e.what() << '\n';
        return 1;
    }
}
