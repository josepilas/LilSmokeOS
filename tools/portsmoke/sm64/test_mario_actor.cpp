#include "mario_actor.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace portsmoke::sm64;
unsigned checks = 0;
void check(bool ok) { ++checks; if (!ok) throw std::runtime_error("Mario check " + std::to_string(checks)); }
template<class F> void rejects(F f) { bool bad = false; try { f(); } catch (const std::exception&) { bad = true; } check(bad); }
void put(std::vector<std::uint8_t>& data, unsigned at, unsigned value, unsigned bytes = 2) {
    for (unsigned i = 0; i < bytes; ++i) data[at+i] = std::uint8_t(value >> ((bytes-i-1)*8));
}
std::vector<std::uint8_t> fixture() {
    std::vector<std::uint8_t> data(282);
    put(data, 2, 189); put(data, 8, 2); put(data, 10, 20);
    put(data, 12, 276, 4); put(data, 16, 24, 4); put(data, 20, data.size(), 4);
    for (unsigned channel = 0; channel < 63; ++channel) {
        put(data, 24 + channel * 4, 1); put(data, 26 + channel * 4, 0);
    }
    put(data, 24 + 3 * 4, 2); put(data, 26 + 3 * 4, 1); // root X angles cross signed wrap.
    put(data, 278, 32760); put(data, 280, 32776);
    return data;
}
MarioActor actor(const MarioAnimation& clip) {
    MarioActor value{};
    for (unsigned i = 0; i < 20; ++i) value.joints[i] = {"synthetic", int(i)-1, {0, i ? 4.f : 0.f, 0}};
    value.animations = {clip, clip, clip};
    return value;
}
int main(int argc, char **argv) {
    try {
        auto raw = fixture();
        auto clip = decodeMarioAnimation(raw);
        check(clip.indices.size() == 126 && clip.values.size() == 3);
        auto value = actor(clip);
        auto pose = sampleMarioPose(value, MarioMotion::Walking, 0.5f);
        check(pose.animationId == 0x48 && pose.sampledFrame == 0.5f);
        check(std::abs(pose.jointMatrices[0][5] + 0.25f) < 0.00001f); // shortest path near pi, not through zero.
        check(std::abs(pose.jointMatrices[1][13] + 1.f) < 0.00001f);
        check(sampleMarioPose(value, MarioMotion::Walking, 2.f).sampledFrame == 0);
        value.animations[2].flags = 1;
        check(sampleMarioPose(value, MarioMotion::SingleJump, 999.f).sampledFrame == 1);
        rejects([&] { sampleMarioPose(value, MarioMotion::Idle, -1); });
        rejects([&] { sampleMarioPose(value, MarioMotion::Idle, std::numeric_limits<float>::quiet_NaN()); });
        rejects([&] { sampleMarioPose(value, MarioMotion::Idle, std::numeric_limits<float>::infinity()); });
        rejects([&] { sampleMarioPose(value, static_cast<MarioMotion>(99), 0); });
        value.joints[3].parent = 3;
        rejects([&] { sampleMarioPose(value, MarioMotion::Idle, 0); });
        value = actor(clip); value.joints[0].translation[0] = 99999;
        rejects([&] { sampleMarioPose(value, MarioMotion::Idle, 0); });
        value = actor(clip); value.animations[0].indices[0] = 0;
        rejects([&] { sampleMarioPose(value, MarioMotion::Idle, 0); });
        value = actor(clip); value.animations[0].translationDivisor = -1;
        rejects([&] { sampleMarioPose(value, MarioMotion::Idle, 0); });
        value.animations[0].translationDivisor = 1025;
        rejects([&] { sampleMarioPose(value, MarioMotion::Idle, 0); });
        for (auto size : {0u, 23u, 65537u}) rejects([&] { decodeMarioAnimation(std::vector<std::uint8_t>(size)); });
        for (auto mutation : {0u, 2u, 4u, 6u, 8u, 10u, 12u, 16u, 20u, 24u, 26u}) {
            auto bad = raw;
            put(bad, mutation, 65535);
            rejects([&] { decodeMarioAnimation(bad); });
        }
        auto bad = raw; put(bad, 12, 275, 4); rejects([&] { decodeMarioAnimation(bad); });
        bad = raw; put(bad, 16, 0xfffffff0, 4); rejects([&] { decodeMarioAnimation(bad); });
        bad = raw; put(bad, 12, 24, 4); rejects([&] { decodeMarioAnimation(bad); });
        if (argc == 2) {
            auto mario = loadMarioActor(argv[1]);
            check(mario.parts.size() == 15);
            std::size_t triangles = 0;
            for (const auto& part : mario.parts) {
                check(part.joint < 20 && !part.mesh.vertices.empty());
                triangles += part.mesh.vertices.size()/3;
            }
            check(triangles == 752);
            for (unsigned clipIndex = 0; clipIndex < 3; ++clipIndex) {
                auto motion = clipIndex == 0 ? MarioMotion::Idle : clipIndex == 1 ? MarioMotion::Walking : MarioMotion::SingleJump;
                const auto& animation = mario.animations[clipIndex];
                auto originalVertices = mario.parts[0].mesh.vertices.data();
                for (unsigned frame = 0; frame <= animation.frameCount; ++frame) {
                    auto p = sampleMarioPose(mario, motion, float(frame));
                    for (unsigned i = 0; i < 20; ++i) {
                        for (float f : p.jointMatrices[i]) check(std::isfinite(f));
                        check(std::abs(p.jointMatrices[i][15] - 1) < 0.00001f);
                    }
                    check(mario.parts[0].mesh.vertices.data() == originalVertices);
                }
                auto start = sampleMarioPose(mario, motion, 0);
                auto later = sampleMarioPose(mario, motion, animation.frameCount / 2.f);
                check(start.jointMatrices != later.jointMatrices);
                auto end = sampleMarioPose(mario, motion, animation.frameCount);
                auto expected = sampleMarioPose(mario, motion, animation.flags & 1 ? animation.frameCount - 1 : animation.loopStart);
                check(end.jointMatrices == expected.jointMatrices);
            }
            std::cout << "MarioParts=15 MarioTriangles=" << triangles << " OriginalAnimations=C5,48,4D Scale=0.25\n";
        }
        std::cout << "PASS " << checks << " Mario actor/animation checks\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
