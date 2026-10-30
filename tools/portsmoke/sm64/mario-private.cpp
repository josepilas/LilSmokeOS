#include "mario_actor.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <sys/resource.h>
using namespace portsmoke::sm64;
int main(int argc, char **argv) {
    try {
        if (argc != 3) throw std::runtime_error("usage: mario-private ROM PRIVATE_PACK");
        prepare(argv[1], argv[2], marioDefinition);
        auto mario = loadMarioActor(argv[2]);
        std::size_t triangles = 0, textures = 0, cacheBytes = sizeof(mario);
        cacheBytes += mario.parts.capacity() * sizeof(ActorPart);
        for (const auto& part : mario.parts) {
            triangles += part.mesh.vertices.size()/3; textures += part.mesh.textures.size();
            cacheBytes += part.mesh.vertices.capacity() * sizeof(SceneVertex) +
                          part.mesh.batches.capacity() * sizeof(SceneBatch) +
                          part.mesh.textures.capacity() * sizeof(SceneTexture);
            for (const auto& texture : part.mesh.textures) cacheBytes += texture.image.rgba.capacity();
        }
        for (const auto& animation : mario.animations)
            cacheBytes += 2 * (animation.indices.capacity() + animation.values.capacity());
        std::cout << "MarioParts=" << mario.parts.size() << " MarioTriangles=" << triangles << " MarioTextures=" << textures << '\n';
        std::cout << "ActorCachedBufferBytes=" << cacheBytes << " PoseBytes=" << sizeof(MarioPose) << '\n';
        for (auto motion : {MarioMotion::Idle, MarioMotion::Walking, MarioMotion::SingleJump}) {
            auto pose = sampleMarioPose(mario, motion, 0);
            std::cout << "OriginalAnimation=" << pose.animationId << " Frame=" << pose.sampledFrame << '\n';
            std::array<float, 3> lo, hi;
            lo.fill(std::numeric_limits<float>::infinity()); hi.fill(-std::numeric_limits<float>::infinity());
            for (const auto& part : mario.parts) {
                const auto& m = pose.jointMatrices[part.joint];
                for (const auto& v : part.mesh.vertices) for (unsigned axis = 0; axis < 3; ++axis) {
                    float f = m[axis] * v.x + m[4+axis] * v.y + m[8+axis] * v.z + m[12+axis];
                    lo[axis] = std::min(lo[axis], f); hi[axis] = std::max(hi[axis], f);
                }
            }
            std::cout << "LevelBoundsMin=" << lo[0] << ',' << lo[1] << ',' << lo[2]
                      << " LevelBoundsMax=" << hi[0] << ',' << hi[1] << ',' << hi[2]
                      << " Height=" << hi[1] - lo[1] << '\n';
        }
        // Includes inherited process/QEMU high-water state, NOT M8 guest RSS.
        rusage r{}; if (!getrusage(RUSAGE_SELF, &r)) std::cout << "ProcessHighWaterRSSKB=" << r.ru_maxrss << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
