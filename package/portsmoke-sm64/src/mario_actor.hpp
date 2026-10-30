#pragma once
#include "bob_scene.hpp"
#include <array>

namespace portsmoke::sm64 {

// Offset/size metadata only. n64decomp/sm64 9921382a68bb0c865e5e45eb594d9c64db59b1af.
// Game geometry, texture pixels and animation samples are extracted privately.
inline constexpr AssetSpec marioAssets[] = {
    {"actor/mario/idle-c5.be", -1, 5715020, 2218, 0, 0, 0},
    {"actor/mario/jump-4d.be", -1, 5408288, 2756, 0, 0, 0},
    {"actor/mario/segment4.bin", 1132368, 0, 217976, 0, 0, 0},
    {"actor/mario/walk-48.be", -1, 5397832, 5374, 0, 0, 0},
};
inline constexpr GameDefinition marioDefinition{
    "9bef1128717f958171a4afac3ed78ee2bb4e86ce", 8388608,
    "90f6111e403b93fc1566ede1083229b1e2d3e5d4", marioAssets, 4};

inline constexpr std::size_t marioJointCount = 20;
inline constexpr float marioModelScale = 0.25f; // GEO_SCALE(16384).

enum class MarioMotion { Idle, Walking, SingleJump };
using ActorMatrix = std::array<float, 16>; // Column-major for glMultMatrixf.

struct MarioJoint {
    const char *name;
    int parent;
    std::array<float, 3> translation; // Original geo/model units, unscaled.
};
struct ActorPart {
    const char *name;
    std::size_t joint;
    SceneMesh mesh; // Cached model-local ROM mesh, never rebuilt by pose sampling.
};
struct MarioAnimation {
    std::uint16_t flags = 0;
    std::int16_t translationDivisor = 0;
    std::uint16_t startFrame = 0, loopStart = 0, frameCount = 0;
    std::vector<std::uint16_t> indices; // Three (count,offset) channels per joint + root translation.
    std::vector<std::int16_t> values;   // Private original signed big-endian ROM samples.
};
struct MarioActor {
    std::array<MarioJoint, marioJointCount> joints;
    std::vector<ActorPart> parts;
    std::array<MarioAnimation, 3> animations; // Idle C5, Walking 48, SingleJump 4D.
};
struct MarioPose {
    std::array<ActorMatrix, marioJointCount> jointMatrices;
    MarioMotion motion = MarioMotion::Idle;
    std::uint16_t animationId = 0;
    float sampledFrame = 0;
};

MarioActor loadMarioActor(const std::filesystem::path& privatePackPath);

// Original ROM animations. animationFrame is a frame number (not radians);
// integer samples match upstream, fractional frames interpolate shortest angles.
// Looping clips wrap at their loop bounds; NOLOOP idle/jump hold the final frame.
// Matrices already include the 0.25 geo scale and original root translation,
// in LEVEL units. Caller applies world position/yaw, then glMultMatrixf with
// pose.jointMatrices[part.joint]. Do not additionally scale the actor by 0.25.
MarioPose sampleMarioPose(const MarioActor&, MarioMotion, float animationFrame);

// Checked decoding entry point also permits isolated malformed/synthetic tests.
MarioAnimation decodeMarioAnimation(const std::vector<std::uint8_t>& raw);

}
