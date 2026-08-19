#pragma once

#include <Geode/Geode.hpp>

#include "Evolution.hpp"

#include <cstdint>
#include <vector>

namespace omnipath {

enum class PlayerMode : std::uint8_t {
    Cube,
    Ship,
    Ball,
    Ufo,
    Wave,
    Robot,
    Spider,
    Swing,
    Unknown,
};

const char* modeName(PlayerMode mode);

struct SimPlayer {
    bool active = true;
    bool alive = true;
    bool held = false;
    bool onGround = false;
    bool upsideDown = false;
    bool dashing = false;
    bool mini = false;
    float x = 0.0f;
    float y = 0.0f;
    double vy = 0.0;
    PlayerMode mode = PlayerMode::Cube;
    std::uint32_t dashTicks = 0;
    int dualRequest = 0;
    GameObject* lastPortal = nullptr;
    GameObject* lastPad = nullptr;
    GameObject* lastRing = nullptr;
};

struct SimAgent {
    SimPlayer p1;
    SimPlayer p2;
    bool dual = false;
    bool alive = true;
    bool completed = false;
    float maxX = 0.0f;
    float progress = 0.0f;
    std::uint32_t survivedTicks = 0;
    PlayerMode deathMode = PlayerMode::Unknown;
    Gene lastGene{};
};

class WorldModel {
public:
    void rebuild(PlayLayer* layer);
    void resetAgent(SimAgent& agent) const;
    void stepAgent(SimAgent& agent, Gene gene, float dx, float dt) const;

    float progressForX(float x) const;
    float startX() const;
    float finishX() const;
    float floorY() const;
    std::size_t objectCount() const;
    std::size_t hazardCount() const;
    std::size_t solidCount() const;

private:
    struct IndexedObject {
        float anchorX = 0.0f;
        GameObject* object = nullptr;
    };

    std::vector<IndexedObject> m_objects;
    SimPlayer m_startP1;
    SimPlayer m_startP2;
    bool m_initialDual = false;
    float m_startX = 0.0f;
    float m_finishX = 1000.0f;
    float m_floorY = 0.0f;
    std::size_t m_hazardCount = 0;
    std::size_t m_solidCount = 0;

    static PlayerMode playerMode(PlayerObject* player);
    static bool isSolid(GameObjectType type);
    static bool isHazard(GameObjectType type);
    static bool isRing(GameObjectType type);
    static bool isPad(GameObjectType type);
    static bool isPortal(GameObjectType type);
    static bool relevantObject(GameObjectType type);
    static float halfSize(SimPlayer const& player);
    static cocos2d::CCRect playerRect(SimPlayer const& player, float inset = 0.0f);
    static cocos2d::CCRect insetRect(cocos2d::CCRect rect, float xInset, float yInset);
    static bool intersects(cocos2d::CCRect const& a, cocos2d::CCRect const& b);
    static bool canLand(PlayerMode mode);

    SimPlayer fromRealPlayer(PlayerObject* player) const;
    void stepPlayer(SimPlayer& player, bool input, float dx, float dt) const;
    void processInteractiveObjects(SimPlayer& player, bool input) const;
    void applyPortal(SimPlayer& player, GameObject* object) const;
    void applyPad(SimPlayer& player, GameObject* object) const;
    void applyRing(SimPlayer& player, GameObject* object, bool input) const;
    void resolveWorldCollision(SimPlayer& player, float previousX, float previousY) const;
};

} // namespace omnipath
