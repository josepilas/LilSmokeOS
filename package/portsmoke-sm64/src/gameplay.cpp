// SPDX-License-Identifier: MPL-2.0
// Native bounded adaptation of the CC0 n64decomp surface/quarter-step algorithms
// at 9921382a68bb0c865e5e45eb594d9c64db59b1af. This is the basic movement subset,
// not the original complete Mario action machine, object engine or camera.
#include "gameplay.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace portsmoke::sm64 {
namespace {
    void check(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
    }
    bool finite(Position p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
    float edge(float ax, float ay, float bx, float by, float x, float y)
    {
        return (bx - ax) * (y - ay) - (by - ay) * (x - ax);
    }
    bool triangle(const CollisionSurface& s, float x, float y, unsigned projection)
    {
        auto projected = [projection](Position p) -> std::array<float, 2> {
            if (projection == 0)
                return { p.x, p.z };
            if (projection == 1)
                return { p.y, p.z };
            return { p.x, p.y };
        };
        auto a = projected(s.points[0]), b = projected(s.points[1]), c = projected(s.points[2]);
        float e0 = edge(a[0], a[1], b[0], b[1], x, y), e1 = edge(b[0], b[1], c[0], c[1], x, y);
        float e2 = edge(c[0], c[1], a[0], a[1], x, y);
        return (e0 >= -0.01f && e1 >= -0.01f && e2 >= -0.01f) || (e0 <= 0.01f && e1 <= 0.01f && e2 <= 0.01f);
    }
    int coordinate(float value) { return std::clamp(int(std::floor((value + 8192) / 1024)), 0, 15); }
    bool collisionType(unsigned type) { return type < 0x40 || (type >= 0x65 && type <= 0xff); }
    bool force(unsigned type)
    {
        return type == 4 || type == 0x0e || type == 0x24 || type == 0x25 || type == 0x27 || type == 0x2c || type == 0x2d;
    }
    bool tangible(const CollisionSurface& s) { return s.type != 0x12 && s.type != 0x72; }
    float approach(float current, float target, float delta)
    {
        return current < target ? std::min(current + delta, target) : std::max(current - delta, target);
    }
}

CollisionWorld::CollisionWorld(const std::vector<std::uint8_t>& commands)
{
    check(commands.size() <= 128 * 1024 && commands.size() % 2 == 0, "collision command budget/alignment");
    std::size_t offset = 0;
    auto word = [&]() -> unsigned {
        check(offset + 2 <= commands.size(), "truncated collision stream");
        unsigned value = (unsigned(commands[offset]) << 8) | commands[offset + 1];
        offset += 2;
        return value;
    };
    auto signedWord = [&]() -> float { unsigned value=word(); return float(value<32768?int(value):int(value)-65536); };
    check(word() == 0x40, "collision vertices missing");
    unsigned count = word();
    check(count > 0 && count <= 4096, "collision vertex count");
    std::vector<Position> vertices;
    vertices.reserve(count);
    for (unsigned i = 0; i < count; ++i)
        vertices.push_back({ signedWord(), signedWord(), signedWord() });
    for (;;) {
        unsigned type = word();
        if (type == 0x41)
            break; // Stop before the separately handled special-object list.
        check(collisionType(type), "unsupported collision command/surface");
        unsigned number = word();
        check(number && surfaces_.size() + number <= 8192, "collision surface budget");
        for (unsigned i = 0; i < number; ++i) {
            unsigned a = word(), b = word(), c = word();
            check(a < count && b < count && c < count, "collision vertex index");
            if (force(type))
                (void)word();
            Position p = vertices[a], q = vertices[b], r = vertices[c];
            Position n { (q.y - p.y) * (r.z - p.z) - (q.z - p.z) * (r.y - p.y),
                (q.z - p.z) * (r.x - p.x) - (q.x - p.x) * (r.z - p.z),
                (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x) };
            float length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            if (length < 0.0001f)
                continue;
            n.x /= length;
            n.y /= length;
            n.z /= length;
            surfaces_.push_back({ { p, q, r }, n, -(n.x * p.x + n.y * p.y + n.z * p.z), std::uint16_t(type) });
        }
    }
    check(!surfaces_.empty(), "collision stream contains no usable surfaces");
    for (std::size_t i = 0; i < surfaces_.size(); ++i) {
        const auto& p = surfaces_[i].points;
        int x0 = coordinate(std::min({ p[0].x, p[1].x, p[2].x }) - 50), x1 = coordinate(std::max({ p[0].x, p[1].x, p[2].x }) + 50);
        int z0 = coordinate(std::min({ p[0].z, p[1].z, p[2].z }) - 50), z1 = coordinate(std::max({ p[0].z, p[1].z, p[2].z }) + 50);
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                check(++references_ <= 65536, "collision spatial-index budget");
                cells_[z * 16 + x].push_back(std::uint16_t(i));
            }
    }
}
const std::vector<std::uint16_t>& CollisionWorld::cell(float x, float z) const
{
    static const std::vector<std::uint16_t> empty;
    if (!std::isfinite(x) || !std::isfinite(z) || x <= -8192 || x >= 8192 || z <= -8192 || z >= 8192)
        return empty;
    return cells_[coordinate(z) * 16 + coordinate(x)];
}
FloorHit CollisionWorld::floor(float x, float y, float z) const
{
    FloorHit hit;
    if (!std::isfinite(y))
        return hit;
    for (auto index : cell(x, z)) {
        const auto& s = surfaces_[index];
        if (!tangible(s) || s.normal.y <= 0.01f || !triangle(s, x, z, 0))
            continue;
        float height = -(s.normal.x * x + s.normal.z * z + s.origin) / s.normal.y;
        if (height <= y + 78 && (!hit.surface || height > hit.height))
            hit = { height, &s };
    }
    return hit;
}
float CollisionWorld::ceiling(float x, float y, float z) const
{
    float height = 20000;
    if (!std::isfinite(y))
        return height;
    for (auto index : cell(x, z)) {
        const auto& s = surfaces_[index];
        if (!tangible(s) || s.normal.y >= -0.01f || !triangle(s, x, z, 0))
            continue;
        float level = -(s.normal.x * x + s.normal.z * z + s.origin) / s.normal.y;
        if (level >= y - 78)
            height = std::min(height, level);
    }
    return height;
}
unsigned CollisionWorld::walls(Position& position, float offsetY, float radius) const
{
    check(finite(position) && std::isfinite(offsetY) && std::isfinite(radius) && radius > 0 && radius <= 50,
        "invalid wall query");
    Position point = position;
    point.y += offsetY;
    unsigned hits = 0;
    for (auto index : cell(point.x, point.z)) {
        const auto& s = surfaces_[index];
        if (!tangible(s) || std::abs(s.normal.y) > 0.01f)
            continue;
        float lo = std::min({ s.points[0].y, s.points[1].y, s.points[2].y }) - 5;
        float hi = std::max({ s.points[0].y, s.points[1].y, s.points[2].y }) + 5;
        if (point.y < lo || point.y > hi)
            continue;
        float distance = s.normal.x * point.x + s.normal.y * point.y + s.normal.z * point.z + s.origin;
        if (distance < -radius || distance > radius)
            continue;
        bool inside = std::abs(s.normal.x) > std::abs(s.normal.z) ? triangle(s, point.y, point.z, 1) : triangle(s, point.x, point.y, 2);
        if (!inside)
            continue;
        position.x += s.normal.x * (radius - distance);
        position.z += s.normal.z * (radius - distance);
        ++hits;
    }
    return hits;
}
float CollisionWorld::cameraDistance(Position origin, Position direction, float maximum) const
{
    check(finite(origin) && finite(direction) && std::isfinite(maximum) && maximum > 0,
        "invalid camera ray");
    auto subtract = [](Position a, Position b) { return Position { a.x - b.x, a.y - b.y, a.z - b.z }; };
    auto cross = [](Position a, Position b) { return Position { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; };
    auto dot = [](Position a, Position b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
    float nearest = maximum;
    for (const auto& s : surfaces_) {
        if (s.type == 0x12 || s.type == 0x76 || s.type == 0x77 || s.type == 0x78 || s.type == 0x7a)
            continue;
        auto e1 = subtract(s.points[1], s.points[0]), e2 = subtract(s.points[2], s.points[0]);
        auto p = cross(direction, e2);
        float determinant = dot(e1, p);
        if (std::abs(determinant) < 0.00001f)
            continue;
        float inverse = 1 / determinant;
        auto offset = subtract(origin, s.points[0]);
        float u = dot(offset, p) * inverse;
        if (u < 0 || u > 1)
            continue;
        auto q = cross(offset, e1);
        float v = dot(direction, q) * inverse;
        if (v < 0 || u + v > 1)
            continue;
        float distance = dot(e2, q) * inverse;
        if (distance > 0.01f && distance < nearest)
            nearest = distance;
    }
    return nearest;
}
CollisionWorld loadBobCollision(const std::filesystem::path& pack)
{
    RuntimeAssets assets(pack, bobDefinition);
    auto segment = assets.pack().load(bobSegments[0].name);
    check(sha1(segment) == "d43df030da8aed4db31a27cb1a5c3b2d1f30c98e", "unsupported Bob collision segment");
    constexpr std::size_t begin = 0xe958, end = 0x1104c;
    check(segment.size() >= end, "Bob collision block missing");
    return CollisionWorld({ segment.begin() + begin, segment.begin() + end });
}
void resetPlayer(PlayerState& player, const CollisionWorld& world)
{
    unsigned respawns = player.respawns, jumps = player.jumps, landings = player.landings, walls = player.wallHits;
    player = PlayerState {};
    auto hit = world.floor(player.position.x, player.position.y, player.position.z);
    check(hit.surface && hit.surface->type != 0x0a, "Bob spawn has no safe floor");
    player.position.y = hit.height;
    player.respawns = respawns;
    player.jumps = jumps;
    player.landings = landings;
    player.wallHits = walls;
}
void stepPlayer(PlayerState& p, const PlayerInput& input, const CollisionWorld& world)
{
    check(finite(p.position) && std::isfinite(input.right) && std::isfinite(input.forward) && std::isfinite(input.cameraYaw), "non-finite player/input state");
    auto oldAction = p.action;
    float right = std::clamp(input.right, -1.f, 1.f), forward = std::clamp(input.forward, -1.f, 1.f);
    float magnitude = std::sqrt(right * right + forward * forward);
    if (magnitude > 1) {
        right /= magnitude;
        forward /= magnitude;
        magnitude = 1;
    }
    float target = magnitude * 32;
    p.speed = approach(p.speed, target, p.grounded ? 1.1f : 0.35f);
    if (magnitude > 0.05f) {
        float x = std::cos(input.cameraYaw) * right - std::sin(input.cameraYaw) * forward;
        float z = -std::sin(input.cameraYaw) * right - std::cos(input.cameraYaw) * forward;
        float wanted = std::atan2(x, z), difference = std::remainder(wanted - p.yaw, 6.2831853072f);
        p.yaw += std::clamp(difference, -0.1963495408f, 0.1963495408f);
    }
    if (p.grounded && input.jumpPressed) {
        p.grounded = false;
        p.velocityY = 42 + p.speed * 0.25f;
        p.action = PlayerAction::Jumping;
        ++p.jumps;
    }
    for (unsigned quarter = 0; quarter < 4; ++quarter) {
        Position next = p.position;
        auto oldFloor = world.floor(next.x, next.y, next.z);
        float slope = p.grounded && oldFloor.surface ? oldFloor.surface->normal.y : 1;
        next.x += std::sin(p.yaw) * p.speed * slope / 4;
        next.z += std::cos(p.yaw) * p.speed * slope / 4;
        if (!p.grounded)
            next.y += p.velocityY / 4;
        if (p.grounded) {
            p.wallHits += world.walls(next, 30, 24);
            p.wallHits += world.walls(next, 60, 50);
        } else {
            p.wallHits += world.walls(next, 150, 50);
            p.wallHits += world.walls(next, 30, 50);
        }
        auto floor = world.floor(next.x, next.y, next.z);
        float ceiling = world.ceiling(next.x, next.y + 80, next.z);
        if (p.grounded) {
            if (!floor.surface || floor.height - next.y > 78 || ceiling - floor.height < 160) {
                p.speed = 0;
                break;
            }
            if (next.y - floor.height > 100) {
                p.grounded = false;
                p.velocityY = 0;
                p.action = PlayerAction::Falling;
            } else
                next.y = floor.height;
        } else {
            if (floor.surface && next.y <= floor.height) {
                next.y = floor.height;
                p.velocityY = 0;
                p.grounded = true;
                ++p.landings;
            } else if (ceiling < next.y + 160) {
                // The original ascent check cancels the quarter-step; it never
                // teleports Mario down to ceiling-minus-height through a floor.
                if (p.velocityY >= 0) {
                    p.velocityY = 0;
                    continue;
                }
                next.x = p.position.x;
                next.z = p.position.z;
            }
        }
        p.position = next;
        if (floor.surface && floor.surface->type == 0x0a && next.y <= floor.height + 0.1f) {
            ++p.respawns;
            resetPlayer(p, world);
            break;
        }
    }
    if (!p.grounded) {
        // Upstream single-jump release and normal gravity/terminal velocity.
        if (!input.jumpHeld && p.velocityY > 20)
            p.velocityY /= 4;
        else
            p.velocityY = std::max(p.velocityY - 4, -75.f);
        p.action = p.velocityY > 0 ? PlayerAction::Jumping : PlayerAction::Falling;
    } else
        p.action = p.speed > 0.5f ? PlayerAction::Walking : PlayerAction::Idle;
    bool wasAir = oldAction == PlayerAction::Jumping || oldAction == PlayerAction::Falling;
    if ((p.grounded && oldAction != p.action) || (!p.grounded && !wasAir))
        p.animationFrame = 0;
    else
        p.animationFrame += p.grounded && p.action == PlayerAction::Walking ? std::max(0.25f, p.speed / 4) : 1;
    if (p.animationFrame > 100000)
        p.animationFrame = std::fmod(p.animationFrame, 60);
    if (p.position.y < -10000 || std::abs(p.position.x) >= 8190 || std::abs(p.position.z) >= 8190) {
        ++p.respawns;
        resetPlayer(p, world);
    }
}
}
