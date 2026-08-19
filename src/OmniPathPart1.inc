#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/cocos.hpp>

#include "SolverCore.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace geode::prelude;

namespace {

struct PendingConfig {
    bool armed = false;
    std::string macroName = "omnipath-run";
    std::uint32_t candidates = 32;
    std::uint32_t maxGenerations = 200;
};

PendingConfig g_pending;

struct IndexedObject {
    float initialX = 0.0f;
    GameObject* object = nullptr;
};

struct SimPlayer {
    bool active = true;
    bool alive = true;
    bool held = false;
    bool onGround = false;
    bool upsideDown = false;
    bool dashing = false;
    float x = 0.0f;
    float y = 0.0f;
    double vy = 0.0;
    omnipath::PlayerMode mode = omnipath::PlayerMode::Cube;
    GameObject* lastPortal = nullptr;
    GameObject* lastPad = nullptr;
    GameObject* lastRing = nullptr;
};

struct SimAgent {
    SimPlayer p1;
    SimPlayer p2;
    bool dual = false;
    bool alive = true;
    float maxX = 0.0f;
    float progress = 0.0f;
    std::uint32_t survivedTicks = 0;
    omnipath::PlayerMode deathMode = omnipath::PlayerMode::Unknown;
    CCSprite* ghost1 = nullptr;
    CCSprite* ghost2 = nullptr;
};

struct LiveSession {
    bool active = false;
    bool injectingInput = false;
    bool ignoreTouch = true;
    bool ghosts = true;
    bool verifying = false;
    int trainingSpeed = 2;
    int appliedSpeed = 1;
    float baseTimeScale = 1.0f;

    std::string macroName;
    std::unique_ptr<omnipath::ParallelEvolution> solver;
    std::vector<SimAgent> agents;
    std::vector<IndexedObject> worldObjects;
    std::uint32_t tick = 0;
    std::uint32_t maxGenerations = 200;
    bool p1Down = false;
    bool p2Down = false;
    int deadTicks = 0;
    float proxyLastX = 0.0f;
    float lastGoodDx = 4.0f;
    float baseGroundY = 0.0f;
    float verifyMaxX = 0.0f;
    float verifyProgress = 0.0f;
    std::size_t leaderIndex = 0;
    std::size_t aliveCount = 0;
    float bestVerifiedProgress = 0.0f;

    omnipath::Candidate verificationCandidate;
    omnipath::Candidate verifiedCandidate;
    bool hasVerifiedCandidate = false;

    CCLabelBMFont* hudMain = nullptr;
    CCLabelBMFont* hudStats = nullptr;
    CCLabelBMFont* hudHistory = nullptr;
};

LiveSession g_live;

std::string sanitizeName(std::string name) {
    if (name.empty()) name = "omnipath-run";
    for (auto& ch : name) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_')) ch = '_';
    }
    return name;
}

omnipath::PlayerMode playerMode(PlayerObject* p) {
    if (!p) return omnipath::PlayerMode::Unknown;
    if (p->m_isDart) return omnipath::PlayerMode::Wave;
    if (p->m_isShip) return omnipath::PlayerMode::Ship;
    if (p->m_isBird) return omnipath::PlayerMode::Ufo;
    if (p->m_isBall) return omnipath::PlayerMode::Ball;
    if (p->m_isRobot) return omnipath::PlayerMode::Robot;
    if (p->m_isSpider) return omnipath::PlayerMode::Spider;
    if (p->m_isSwing) return omnipath::PlayerMode::Swing;
    return omnipath::PlayerMode::Cube;
}

bool isSolid(GameObjectType type) {
    return type == GameObjectType::Solid ||
        type == GameObjectType::Slope ||
        type == GameObjectType::CollisionObject ||
        type == GameObjectType::Breakable;
}

bool isHazard(GameObjectType type) {
    return type == GameObjectType::Hazard || type == GameObjectType::AnimatedHazard;
}

bool isRing(GameObjectType type) {
    switch (type) {
        case GameObjectType::YellowJumpRing:
        case GameObjectType::PinkJumpRing:
        case GameObjectType::GravityRing:
        case GameObjectType::GreenRing:
        case GameObjectType::DropRing:
        case GameObjectType::RedJumpRing:
        case GameObjectType::CustomRing:
        case GameObjectType::DashRing:
        case GameObjectType::GravityDashRing:
        case GameObjectType::SpiderOrb:
        case GameObjectType::TeleportOrb:
            return true;
        default:
            return false;
    }
}

bool isPad(GameObjectType type) {
    switch (type) {
        case GameObjectType::YellowJumpPad:
        case GameObjectType::PinkJumpPad:
        case GameObjectType::GravityPad:
        case GameObjectType::RedJumpPad:
        case GameObjectType::SpiderPad:
            return true;
        default:
            return false;
    }
}

bool isPortal(GameObjectType type) {
    switch (type) {
        case GameObjectType::InverseGravityPortal:
        case GameObjectType::NormalGravityPortal:
        case GameObjectType::ShipPortal:
        case GameObjectType::CubePortal:
        case GameObjectType::BallPortal:
        case GameObjectType::UfoPortal:
        case GameObjectType::WavePortal:
        case GameObjectType::RobotPortal:
        case GameObjectType::SpiderPortal:
        case GameObjectType::SwingPortal:
        case GameObjectType::GravityTogglePortal:
        case GameObjectType::RegularSizePortal:
        case GameObjectType::MiniSizePortal:
        case GameObjectType::TeleportPortal:
        case GameObjectType::DualPortal:
        case GameObjectType::SoloPortal:
            return true;
        default:
            return false;
    }
}

bool relevantObject(GameObjectType type) {
    return isSolid(type) || isHazard(type) || isRing(type) || isPad(type) || isPortal(type);
}

CCRect playerRect(SimPlayer const& p, float inset = 0.0f) {
    float half = 12.0f - inset;
    return {p.x - half, p.y - half, half * 2.0f, half * 2.0f};
}

bool rectsIntersect(CCRect const& a, CCRect const& b) {
    return a.getMinX() < b.getMaxX() && a.getMaxX() > b.getMinX() &&
        a.getMinY() < b.getMaxY() && a.getMaxY() > b.getMinY();
}

CCRect insetRect(CCRect r, float xInset, float yInset) {
    r.origin.x += xInset;
    r.origin.y += yInset;
    r.size.width = std::max(1.0f, r.size.width - xInset * 2.0f);
    r.size.height = std::max(1.0f, r.size.height - yInset * 2.0f);
    return r;
}

template <class Fn>
void forNearby(float x, float behind, float ahead, Fn&& fn) {
    if (g_live.worldObjects.empty()) return;

    float minX = x - behind;
    float maxX = x + ahead;
    auto it = std::lower_bound(
        g_live.worldObjects.begin(),
        g_live.worldObjects.end(),
        minX,
        [](IndexedObject const& entry, float value) { return entry.initialX < value; }
    );

    for (; it != g_live.worldObjects.end() && it->initialX <= maxX; ++it) {
        if (it->object) fn(it->object);
    }
}

omnipath::PlayerObservation observeSimPlayer(SimPlayer const& p) {
    omnipath::PlayerObservation out;
    out.active = p.active && p.alive;
    out.mode = p.mode;
    out.x = p.x;
    out.y = p.y;
    out.yVelocity = p.vy;
    out.onGround = p.onGround;
    out.upsideDown = p.upsideDown;
    out.dashing = p.dashing;

    float lower = p.y - 210.0f;
    float upper = p.y + 210.0f;
    bool foundLower = false;
    bool foundUpper = false;

    struct HazardSpan {
        float start = 99999.0f;
        float end = -99999.0f;
    } cluster;

    float nearestHazardDx = 99999.0f;
    float nearestHazardDy = 0.0f;
    float nearestInteractableDx = 99999.0f;
    std::vector<std::pair<float, float>> hazardIntervals;

    forNearby(p.x, 24.0f, 300.0f, [&](GameObject* obj) {
        auto type = obj->getType();
        auto rect = obj->getObjectRect();
        auto dx = rect.getMinX() - (p.x + 12.0f);
        auto dy = obj->getPositionY() - p.y;

        if (isRing(type)) {
            if (rectsIntersect(playerRect(p, 2.0f), insetRect(rect, 2.0f, 2.0f)))
                out.touchingRing = true;
            if (dx >= -10.0f && dx < nearestInteractableDx) nearestInteractableDx = dx;
        }

        if (isHazard(type)) {
            auto hit = insetRect(rect, 4.5f, 5.0f);
            bool relevantHeight = hit.getMaxY() > p.y - 70.0f && hit.getMinY() < p.y + 70.0f;
            if (relevantHeight && dx >= -20.0f) {
                hazardIntervals.emplace_back(hit.getMinX(), hit.getMaxX());
                if (dx < nearestHazardDx) {
                    nearestHazardDx = dx;
                    nearestHazardDy = dy;
                }
