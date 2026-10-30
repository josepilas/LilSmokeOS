#include "mario_actor.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace portsmoke::sm64 {
namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
std::uint16_t half(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    require(at <= bytes.size() && bytes.size() - at >= 2, "truncated Mario animation halfword");
    return (std::uint16_t(bytes[at]) << 8) | bytes[at + 1];
}
std::uint32_t word(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    require(at <= bytes.size() && bytes.size() - at >= 4, "truncated Mario animation word");
    return (std::uint32_t(half(bytes, at)) << 16) | half(bytes, at + 2);
}
std::int16_t signedHalf(std::uint16_t value) {
    return static_cast<std::int16_t>(value < 32768 ? int(value) : int(value) - 65536);
}

// Original GEO_ANIMATED_PART traversal, including the transform-only joints.
constexpr std::array<MarioJoint, marioJointCount> rig{{
    {"root", -1, {0, 0, 0}}, {"butt", 0, {0, 0, 0}}, {"torso", 1, {68, 0, 0}},
    {"head", 2, {87, 0, 0}}, {"left_shoulder", 2, {67, -10, 79}},
    {"left_arm", 4, {0, 0, 0}}, {"left_forearm", 5, {65, 0, 0}}, {"left_hand", 6, {60, 0, 0}},
    {"right_shoulder", 2, {68, -10, -79}}, {"right_arm", 8, {0, 0, 0}},
    {"right_forearm", 9, {65, 0, 0}}, {"right_hand", 10, {60, 0, 0}},
    {"left_hip", 1, {13, -8, 42}}, {"left_thigh", 12, {0, 0, 0}},
    {"left_leg", 13, {89, 0, 0}}, {"left_foot", 14, {67, 0, 0}},
    {"right_hip", 1, {13, -8, -42}}, {"right_thigh", 16, {0, 0, 0}},
    {"right_leg", 17, {89, 0, 0}}, {"right_foot", 18, {67, 0, 0}},
}};

struct PartSpec { const char *name; unsigned joint; std::uint32_t root, ambient; };
// Original model display-list addresses and light-group offsets, not game data.
constexpr PartSpec parts[] = {
    {"butt", 1, 0x0400cc98, 0x04000000}, {"torso", 2, 0x04010370, 0x04000000},
    {"head_cap_eyes_front", 3, 0x040119a0, 0x04000018},
    {"left_arm", 5, 0x0400d1d8, 0x04000018}, {"left_forearm", 6, 0x0400d2f8, 0x04000018},
    {"left_hand", 7, 0x0400d8f0, 0x04000030}, {"right_arm", 9, 0x0400dde8, 0x04000018},
    {"right_forearm", 10, 0x0400df08, 0x04000018}, {"right_hand", 11, 0x0400e458, 0x04000030},
    {"left_thigh", 13, 0x0400e7b0, 0x04000000}, {"left_leg", 14, 0x0400e918, 0x04000000},
    {"left_foot", 15, 0x0400eca0, 0x04000048}, {"right_thigh", 17, 0x0400efb8, 0x04000000},
    {"right_leg", 18, 0x0400f1d8, 0x04000000}, {"right_foot", 19, 0x0400f400, 0x04000048},
};

std::vector<std::uint8_t> prologue(const PartSpec& part) {
    std::vector<std::uint8_t> bytes;
    auto emit = [&](std::uint32_t value) {
        for (unsigned shift : {24u, 16u, 8u, 0u}) bytes.push_back(std::uint8_t(value >> shift));
    };
    auto cmd = [&](std::uint32_t a, std::uint32_t b) { emit(a); emit(b); };
    cmd(0xb6000000, 0xffffffff); cmd(0xb7000000, 0x00022205);
    cmd(0xbb000000, 0xffffffff); // Explicit texture off; inherited state must not leak.
    cmd(0x03860010, part.ambient + 8); cmd(0x03880010, part.ambient);
    cmd(0x06000000, part.root); cmd(0xb8000000, 0);
    return bytes;
}

ActorMatrix multiply(const ActorMatrix& a, const ActorMatrix& b) {
    ActorMatrix out{};
    for (unsigned column = 0; column < 4; ++column)
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned k = 0; k < 4; ++k) out[column * 4 + row] += a[k * 4 + row] * b[column * 4 + k];
    return out;
}
ActorMatrix localTransform(const std::array<float, 3>& translation, const std::array<float, 3>& angles) {
    // XYZ rotation order used by upstream mtxf_rotate_xyz_and_translate.
    float sx = std::sin(angles[0]), cx = std::cos(angles[0]);
    float sy = std::sin(angles[1]), cy = std::cos(angles[1]);
    float sz = std::sin(angles[2]), cz = std::cos(angles[2]);
    return {cy*cz, cy*sz, -sy, 0,
            sx*sy*cz-cx*sz, sx*sy*sz+cx*cz, sx*cy, 0,
            cx*sy*cz+sx*sz, cx*sy*sz-sx*cz, cx*cy, 0,
            translation[0], translation[1], translation[2], 1};
}
int channel(const MarioAnimation& animation, unsigned index, unsigned frame) {
    require(index < (marioJointCount + 1) * 3 && animation.indices.size() == (marioJointCount + 1) * 6,
            "invalid Mario animation channel table");
    unsigned count = animation.indices[index * 2], offset = animation.indices[index * 2 + 1];
    require(count && offset <= animation.values.size() && count <= animation.values.size() - offset,
            "Mario animation channel exceeds values");
    return animation.values[offset + std::min(frame, count - 1)];
}
}

MarioAnimation decodeMarioAnimation(const std::vector<std::uint8_t>& raw) {
    require(raw.size() >= 24 && raw.size() <= 65536, "invalid Mario animation size");
    MarioAnimation out;
    out.flags = half(raw, 0); out.translationDivisor = signedHalf(half(raw, 2));
    out.startFrame = half(raw, 4); out.loopStart = half(raw, 6); out.frameCount = half(raw, 8);
    require(out.flags <= 1 && out.translationDivisor >= 0 && out.translationDivisor <= 1024 &&
            half(raw, 10) == marioJointCount && out.frameCount && out.frameCount <= 1024 &&
            out.startFrame < out.frameCount && out.loopStart < out.frameCount, "unsupported/malformed Mario animation header");
    auto values = word(raw, 12), indices = word(raw, 16), bytes = word(raw, 20);
    constexpr unsigned indexBytes = (marioJointCount + 1) * 6 * 2;
    require(bytes == raw.size() && indices >= 24 && indices % 2 == 0 && values % 2 == 0 &&
            indices <= raw.size() && indexBytes <= raw.size() - indices &&
            values >= indices + indexBytes && values < raw.size() && (raw.size() - values) % 2 == 0,
            "invalid Mario animation offsets/length");
    for (unsigned i = 0; i < indexBytes / 2; ++i) out.indices.push_back(half(raw, indices + i * 2));
    for (unsigned at = values; at < raw.size(); at += 2) out.values.push_back(signedHalf(half(raw, at)));
    for (unsigned i = 0; i < (marioJointCount + 1) * 3; ++i) (void)channel(out, i, 0);
    return out;
}

MarioActor loadMarioActor(const std::filesystem::path& path) {
    RuntimeAssets data(path, marioDefinition);
    MarioActor actor; actor.joints = rig;
    const char *hashes[] = {"babdc6a96c5acf4c8ab61bb0401fce4d375888e8",
                           "5f2e90c5d64b6628b071e3e6373856ec3b133d05",
                           "c6c46456fa9b89a9fec242882831f32974ff5a60",
                           "d9c3cc4d3ebe3c73c6452f82bcdf58ee240ccfe3"};
    std::vector<std::uint8_t> segment;
    for (unsigned i = 0; i < 4; ++i) {
        auto bytes = data.pack().load(marioAssets[i].name);
        require(sha1(bytes) == hashes[i], "Mario asset differs from supported original US ROM");
        if (i == 2) segment = std::move(bytes);
        else actor.animations[i == 0 ? 0 : i == 3 ? 1 : 2] = decodeMarioAnimation(bytes);
    }
    SegmentMemory memory; memory.bind(4, std::move(segment));
    for (const auto& part : parts) {
        // ROM lights are loaded into a small synthetic state prologue, preserving
        // colors that the original geo inherits from a parent material.
        // Right-foot geometry DL excludes only unsupported fade/environment
        // cleanup in its wrapper; the ROM mesh and brown light remain genuine.
        memory.bind(14, prologue(part));
        auto mesh = interpretF3d(memory, {0x0e000000});
        require(mesh.vertices.size() <= 4096 * 3 && mesh.textures.size() <= 8, "Mario part exceeds mesh budget");
        actor.parts.push_back({part.name, part.joint, std::move(mesh)});
    }
    return actor;
}

MarioPose sampleMarioPose(const MarioActor& actor, MarioMotion motion, float frame) {
    require(std::isfinite(frame) && frame >= 0 && frame <= 1000000, "invalid Mario animation frame");
    unsigned clip = motion == MarioMotion::Idle ? 0 : motion == MarioMotion::Walking ? 1 : motion == MarioMotion::SingleJump ? 2 : 3;
    require(clip < 3, "invalid Mario motion");
    const auto& animation = actor.animations[clip];
    require(animation.flags <= 1 && animation.translationDivisor >= 0 && animation.translationDivisor <= 1024 &&
            animation.frameCount && animation.frameCount <= 1024 &&
            animation.startFrame < animation.frameCount && animation.loopStart < animation.frameCount,
            "invalid sampled Mario animation");
    if (animation.flags & 1) frame = std::clamp(frame, float(animation.startFrame), float(animation.frameCount - 1));
    else if (frame >= animation.frameCount)
        frame = animation.loopStart + std::fmod(frame - animation.loopStart, float(animation.frameCount - animation.loopStart));
    else frame = std::max(frame, float(animation.startFrame));
    unsigned first = unsigned(frame), next = first + 1;
    float fraction = frame - first;
    if (next >= animation.frameCount) next = animation.flags & 1 ? first : animation.loopStart;
    auto sample = [&](unsigned index, bool angle) {
        int a = channel(animation, index, first), b = channel(animation, index, next), delta = b - a;
        if (angle) { if (delta > 32767) delta -= 65536; if (delta < -32768) delta += 65536; }
        return float(a) + fraction * delta;
    };
    MarioPose pose; pose.motion = motion; pose.animationId = clip == 0 ? 0xc5 : clip == 1 ? 0x48 : 0x4d;
    pose.sampledFrame = frame;
    ActorMatrix scale{marioModelScale, 0, 0, 0, 0, marioModelScale, 0, 0, 0, 0, marioModelScale, 0, 0, 0, 0, 1};
    float rootMultiplier = animation.translationDivisor ? 189.f / animation.translationDivisor : 1.f;
    for (unsigned joint = 0; joint < marioJointCount; ++joint) {
        const auto& spec = actor.joints[joint];
        require(spec.parent >= -1 && spec.parent < int(joint) && (joint != 0 || spec.parent == -1), "invalid Mario joint hierarchy");
        auto translation = spec.translation;
        for (float value : translation) require(std::isfinite(value) && std::abs(value) <= 2048, "invalid Mario joint translation");
        if (!joint) for (unsigned axis = 0; axis < 3; ++axis) translation[axis] += sample(axis, false) * rootMultiplier;
        std::array<float, 3> angles{};
        for (unsigned axis = 0; axis < 3; ++axis) angles[axis] = sample(3 + joint * 3 + axis, true) * (6.28318530718f / 65536.f);
        auto local = localTransform(translation, angles);
        pose.jointMatrices[joint] = multiply(spec.parent < 0 ? scale : pose.jointMatrices[spec.parent], local);
    }
    return pose;
}
}
