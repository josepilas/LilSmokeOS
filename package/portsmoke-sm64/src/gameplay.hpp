#pragma once
#include "bob_scene.hpp"
#include <limits>

namespace portsmoke::sm64 {
struct Position {
    float x = 0, y = 0, z = 0;
};
struct CollisionSurface {
    std::array<Position, 3> points;
    Position normal;
    float origin;
    std::uint16_t type;
};
struct FloorHit {
    float height = -11000;
    const CollisionSurface* surface = nullptr;
};
class CollisionWorld {
public:
    explicit CollisionWorld(const std::vector<std::uint8_t>& commands);
    FloorHit floor(float x, float y, float z) const;
    float ceiling(float x, float y, float z) const;
    unsigned walls(Position& position, float offsetY, float radius = 50) const;
    float cameraDistance(Position origin, Position direction, float maximum) const;
    std::size_t size() const { return surfaces_.size(); }
    std::size_t references() const { return references_; }

private:
    std::vector<CollisionSurface> surfaces_;
    std::array<std::vector<std::uint16_t>, 256> cells_;
    std::size_t references_ = 0;
    const std::vector<std::uint16_t>& cell(float x, float z) const;
};
CollisionWorld loadBobCollision(const std::filesystem::path& pack);
enum class PlayerAction { Idle,
    Walking,
    Jumping,
    Falling };
struct PlayerInput {
    float right = 0, forward = 0, cameraYaw = 0;
    bool jumpPressed = false, jumpHeld = false;
};
struct PlayerState {
    Position position { -6558, 0, 6464 };
    float yaw = 2.3561944902f, speed = 0, velocityY = 0, animationFrame = 0;
    PlayerAction action = PlayerAction::Idle;
    bool grounded = true;
    unsigned jumps = 0, landings = 0, wallHits = 0, respawns = 0;
};
void resetPlayer(PlayerState& player, const CollisionWorld& world);
// One 30 Hz simulation tick; the renderer never changes simulation speed.
void stepPlayer(PlayerState& player, const PlayerInput& input, const CollisionWorld& world);
}
