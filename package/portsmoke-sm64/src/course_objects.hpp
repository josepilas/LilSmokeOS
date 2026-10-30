#pragma once
#include "gameplay.hpp"

namespace portsmoke::sm64 {
enum class ObjectKind { Coin, RedCoin, Goomba, Star };
struct CourseObject {
    ObjectKind kind;
    Position position;
    Position home;
    float yaw = 0;
    unsigned age = 0, starBit = 0;
    bool active = true;
};
struct CourseEvents {
    bool coin = false, stomp = false, hurt = false, star = false;
};
struct CourseState {
    std::vector<CourseObject> objects;
    unsigned coins = 0, redCoins = 0, stars = 0, health = 8, invulnerable = 0, ticks = 0;
};
struct ObjectGraphics {
    std::array<SceneMesh, 4> coins;
    SceneMesh star, goombaHead, goombaBody, goombaLeftFoot, goombaRightFoot;
};
CourseState loadBobObjects(const std::filesystem::path& cache, const CollisionWorld& world);
ObjectGraphics loadObjectGraphics(const std::filesystem::path& cache);
CourseEvents stepObjects(CourseState& course, PlayerState& player, const CollisionWorld& world);
void loadCourseSave(CourseState& course, const std::filesystem::path& file);
void saveCourse(CourseState& course, const std::filesystem::path& file);
}
