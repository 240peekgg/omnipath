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

void WorldModel::applyPortal(SimPlayer& player, GameObject* object) const {
    if (!object || object == player.lastPortal) return;
    auto type = object->getType();

    switch (type) {
        case GameObjectType::InverseGravityPortal:
            player.upsideDown = true;
            break;
        case GameObjectType::NormalGravityPortal:
            player.upsideDown = false;
            break;
        case GameObjectType::GravityTogglePortal:
            player.upsideDown = !player.upsideDown;
            break;
        case GameObjectType::ShipPortal:
            player.mode = PlayerMode::Ship;
            break;
        case GameObjectType::CubePortal:
            player.mode = PlayerMode::Cube;
            break;
        case GameObjectType::BallPortal:
            player.mode = PlayerMode::Ball;
            break;
        case GameObjectType::UfoPortal:
            player.mode = PlayerMode::Ufo;
            break;
        case GameObjectType::WavePortal:
            player.mode = PlayerMode::Wave;
            break;
        case GameObjectType::RobotPortal:
            player.mode = PlayerMode::Robot;
            break;
        case GameObjectType::SpiderPortal:
            player.mode = PlayerMode::Spider;
            break;
        case GameObjectType::SwingPortal:
            player.mode = PlayerMode::Swing;
            break;
        case GameObjectType::RegularSizePortal:
            player.mini = false;
            break;
        case GameObjectType::MiniSizePortal:
            player.mini = true;
            break;
        case GameObjectType::DualPortal:
            player.dualRequest = 1;
            break;
        case GameObjectType::SoloPortal:
            player.dualRequest = -1;
            break;
        default:
            break;
    }

    if (type == GameObjectType::CubePortal || type == GameObjectType::BallPortal ||
        type == GameObjectType::RobotPortal || type == GameObjectType::SpiderPortal) {
        player.vy *= 0.55;
    }

    player.lastPortal = object;
}

void WorldModel::applyPad(SimPlayer& player, GameObject* object) const {
    if (!object || object == player.lastPad) return;
    auto type = object->getType();
    double direction = player.upsideDown ? -1.0 : 1.0;

    switch (type) {
        case GameObjectType::PinkJumpPad:
            player.vy = direction * 8.2;
            break;
        case GameObjectType::YellowJumpPad:
            player.vy = direction * 11.4;
            break;
        case GameObjectType::RedJumpPad:
            player.vy = direction * 14.4;
            break;
        case GameObjectType::GravityPad:
            player.upsideDown = !player.upsideDown;
            player.vy = (player.upsideDown ? -1.0 : 1.0) * 10.2;
            break;
        case GameObjectType::SpiderPad:
            player.upsideDown = !player.upsideDown;
            player.vy = 0.0;
            break;
        default:
            break;
    }

    player.onGround = false;
    player.lastPad = object;
}

void WorldModel::applyRing(SimPlayer& player, GameObject* object, bool input) const {
    if (!object || !input || object == player.lastRing) return;
    auto type = object->getType();
    double direction = player.upsideDown ? -1.0 : 1.0;

    switch (type) {
        case GameObjectType::PinkJumpRing:
            player.vy = direction * 7.6;
            break;
        case GameObjectType::YellowJumpRing:
            player.vy = direction * 10.2;
            break;
        case GameObjectType::RedJumpRing:
            player.vy = direction * 13.0;
            break;
        case GameObjectType::GravityRing:
        case GameObjectType::GreenRing:
            player.upsideDown = !player.upsideDown;
            player.vy = (player.upsideDown ? -1.0 : 1.0) * 9.2;
            break;
        case GameObjectType::DropRing:
            player.vy = 0.0;
            break;
        case GameObjectType::SpiderOrb:
            player.upsideDown = !player.upsideDown;
            player.vy = 0.0;
            break;
        case GameObjectType::DashRing:
            player.dashing = true;
            player.dashTicks = 38;
            player.vy = 0.0;
            break;
        case GameObjectType::GravityDashRing:
            player.upsideDown = !player.upsideDown;
            player.dashing = true;
            player.dashTicks = 38;
            player.vy = 0.0;
            break;
        default:
            // Custom / teleport orbs are not safe to fake as noclip. Give them a
            // conservative jump impulse; real-engine verification still decides success.
            player.vy = direction * 9.4;
            break;
    }

    player.onGround = false;
    player.lastRing = object;
}

void WorldModel::processInteractiveObjects(SimPlayer& player, bool input) const {
    if (!player.alive || !player.active || m_objects.empty()) return;

    bool touchingPortal = false;
    bool touchingPad = false;
    bool touchingRing = false;
    auto hitbox = playerRect(player, 1.0f);
    auto minAnchor = player.x - 300.0f;
    auto maxAnchor = player.x + 300.0f;

    auto it = std::lower_bound(
        m_objects.begin(),
        m_objects.end(),
        minAnchor,
        [](IndexedObject const& entry, float value) { return entry.anchorX < value; }
    );

    for (; it != m_objects.end() && it->anchorX <= maxAnchor; ++it) {
        auto* object = it->object;
        if (!object || object->m_isDisabled) continue;
        auto type = object->getType();
        if (!(isPortal(type) || isPad(type) || isRing(type))) continue;

        auto rect = insetRect(object->getObjectRect(), 0.5f, 0.5f);
        if (rect.getMaxX() < player.x - 45.0f || rect.getMinX() > player.x + 45.0f) continue;
        if (!intersects(hitbox, rect)) continue;

        if (isPortal(type)) {
            touchingPortal = true;
            applyPortal(player, object);
        } else if (isPad(type)) {
            touchingPad = true;
            applyPad(player, object);
        } else {
            touchingRing = true;
            applyRing(player, object, input);
        }
    }

    if (!touchingPortal) player.lastPortal = nullptr;
    if (!touchingPad) player.lastPad = nullptr;
    if (!touchingRing) player.lastRing = nullptr;
}


} // namespace omnipath
