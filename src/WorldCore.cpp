#include "WorldSim.hpp"

#include <Geode/utils/cocos.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace geode::prelude;

namespace omnipath {

namespace {

template <class T>
T clampValue(T value, T lo, T hi) {
    return std::max(lo, std::min(value, hi));
}

} // namespace

const char* modeName(PlayerMode mode) {
    switch (mode) {
        case PlayerMode::Cube: return "cube";
        case PlayerMode::Ship: return "ship";
        case PlayerMode::Ball: return "ball";
        case PlayerMode::Ufo: return "ufo";
        case PlayerMode::Wave: return "wave";
        case PlayerMode::Robot: return "robot";
        case PlayerMode::Spider: return "spider";
        case PlayerMode::Swing: return "swing";
        case PlayerMode::Unknown: return "unknown";
    }
    return "unknown";
}

PlayerMode WorldModel::playerMode(PlayerObject* player) {
    if (!player) return PlayerMode::Unknown;
    if (player->m_isDart) return PlayerMode::Wave;
    if (player->m_isShip) return PlayerMode::Ship;
    if (player->m_isBird) return PlayerMode::Ufo;
    if (player->m_isBall) return PlayerMode::Ball;
    if (player->m_isRobot) return PlayerMode::Robot;
    if (player->m_isSpider) return PlayerMode::Spider;
    if (player->m_isSwing) return PlayerMode::Swing;
    return PlayerMode::Cube;
}

bool WorldModel::isSolid(GameObjectType type) {
    return type == GameObjectType::Solid ||
        type == GameObjectType::Slope ||
        type == GameObjectType::CollisionObject ||
        type == GameObjectType::Breakable;
}

bool WorldModel::isHazard(GameObjectType type) {
    return type == GameObjectType::Hazard || type == GameObjectType::AnimatedHazard;
}

bool WorldModel::isRing(GameObjectType type) {
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

bool WorldModel::isPad(GameObjectType type) {
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

bool WorldModel::isPortal(GameObjectType type) {
    switch (type) {
        case GameObjectType::InverseGravityPortal:
        case GameObjectType::NormalGravityPortal:
        case GameObjectType::ShipPortal:
        case GameObjectType::CubePortal:
        case GameObjectType::BallPortal:
        case GameObjectType::RegularSizePortal:
        case GameObjectType::MiniSizePortal:
        case GameObjectType::UfoPortal:
        case GameObjectType::DualPortal:
        case GameObjectType::SoloPortal:
        case GameObjectType::WavePortal:
        case GameObjectType::RobotPortal:
        case GameObjectType::TeleportPortal:
        case GameObjectType::SpiderPortal:
        case GameObjectType::SwingPortal:
        case GameObjectType::GravityTogglePortal:
            return true;
        default:
            return false;
    }
}

bool WorldModel::relevantObject(GameObjectType type) {
    return isSolid(type) || isHazard(type) || isRing(type) || isPad(type) || isPortal(type);
}

float WorldModel::halfSize(SimPlayer const& player) {
    return player.mini ? 6.8f : 10.6f;
}

CCRect WorldModel::playerRect(SimPlayer const& player, float inset) {
    auto half = std::max(2.0f, halfSize(player) - inset);
    return {player.x - half, player.y - half, half * 2.0f, half * 2.0f};
}

CCRect WorldModel::insetRect(CCRect rect, float xInset, float yInset) {
    auto xi = std::min(xInset, std::max(0.0f, rect.size.width * 0.45f));
    auto yi = std::min(yInset, std::max(0.0f, rect.size.height * 0.45f));
    rect.origin.x += xi;
    rect.origin.y += yi;
    rect.size.width = std::max(0.5f, rect.size.width - xi * 2.0f);
    rect.size.height = std::max(0.5f, rect.size.height - yi * 2.0f);
    return rect;
}

bool WorldModel::intersects(CCRect const& a, CCRect const& b) {
    return a.getMinX() < b.getMaxX() && a.getMaxX() > b.getMinX() &&
        a.getMinY() < b.getMaxY() && a.getMaxY() > b.getMinY();
}

bool WorldModel::canLand(PlayerMode mode) {
    switch (mode) {
        case PlayerMode::Cube:
        case PlayerMode::Robot:
        case PlayerMode::Ball:
        case PlayerMode::Ufo:
        case PlayerMode::Spider:
            return true;
        default:
            return false;
    }
}

SimPlayer WorldModel::fromRealPlayer(PlayerObject* player) const {
    SimPlayer out;
    if (!player) {
        out.active = false;
        out.alive = false;
        return out;
    }

    out.active = true;
    out.alive = true;
    out.x = player->getPositionX();
    out.y = player->getPositionY();
    out.vy = player->m_yVelocity;
    out.mode = playerMode(player);
    out.onGround = player->m_isOnGround;
    out.upsideDown = player->m_isUpsideDown;
    out.dashing = player->m_isDashing;
    out.mini = player->m_vehicleSize < 1.0f;
    return out;
}

void WorldModel::rebuild(PlayLayer* layer) {
    m_objects.clear();
    m_hazardCount = 0;
    m_solidCount = 0;

    m_startP1 = fromRealPlayer(layer ? layer->m_player1 : nullptr);
    m_startP2 = fromRealPlayer(layer ? layer->m_player2 : nullptr);
    m_initialDual = layer && layer->m_player2 && !layer->m_player2->m_isHidden && layer->m_player2->isVisible();
    m_startX = m_startP1.x;
    m_floorY = m_startP1.y;

    float furthestObjectX = m_startX + 600.0f;
    if (layer && layer->m_objects) {
        m_objects.reserve(layer->m_objects->count());
        for (auto object : geode::cocos::CCArrayExt<GameObject, false>(layer->m_objects)) {
            if (!object) continue;
            furthestObjectX = std::max(furthestObjectX, object->getPositionX());
            auto type = object->getType();
            if (!relevantObject(type)) continue;
            m_objects.push_back({object->getPositionX(), object});
            if (isHazard(type)) ++m_hazardCount;
            if (isSolid(type)) ++m_solidCount;
        }
    }

    std::sort(m_objects.begin(), m_objects.end(), [](IndexedObject const& a, IndexedObject const& b) {
        return a.anchorX < b.anchorX;
    });

    if (layer && layer->m_endPortal && layer->m_endPortal->getPositionX() > m_startX + 100.0f)
        m_finishX = layer->m_endPortal->getPositionX();
    else
        m_finishX = furthestObjectX + 75.0f;

    if (m_finishX <= m_startX + 100.0f) m_finishX = m_startX + 1000.0f;
}

void WorldModel::resetAgent(SimAgent& agent) const {
    agent = {};
    agent.p1 = m_startP1;
    agent.p2 = m_startP2;
    agent.dual = m_initialDual;
    agent.alive = agent.p1.alive && (!agent.dual || agent.p2.alive);
    agent.completed = false;
    agent.maxX = agent.p1.x;
    agent.progress = progressForX(agent.p1.x);
    agent.survivedTicks = 0;
    agent.deathMode = PlayerMode::Unknown;
    agent.lastGene = {};

    if (!agent.dual) {
        agent.p2.active = false;
        agent.p2.alive = false;
    }
}


} // namespace omnipath
