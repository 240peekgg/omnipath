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

void WorldModel::resolveWorldCollision(
    SimPlayer& player,
    float previousX,
    float previousY
) const {
    if (!player.alive || !player.active) return;

    auto half = halfSize(player);
    auto current = playerRect(player, 0.6f);
    auto previous = current;
    previous.origin.x += previousX - player.x;
    previous.origin.y += previousY - player.y;

    bool landed = false;
    auto minAnchor = player.x - 320.0f;
    auto maxAnchor = player.x + 320.0f;
    auto it = std::lower_bound(
        m_objects.begin(),
        m_objects.end(),
        minAnchor,
        [](IndexedObject const& entry, float value) { return entry.anchorX < value; }
    );

    for (; it != m_objects.end() && it->anchorX <= maxAnchor && player.alive; ++it) {
        auto* object = it->object;
        if (!object || object->m_isDisabled) continue;
        auto type = object->getType();
        if (!(isSolid(type) || isHazard(type))) continue;

        auto rect = object->getObjectRect();
        if (rect.getMaxX() < player.x - 42.0f || rect.getMinX() > player.x + 42.0f) continue;

        if (isHazard(type)) {
            auto hazard = insetRect(rect, 2.0f, 2.5f);
            if (intersects(current, hazard)) player.alive = false;
            continue;
        }

        if (object->m_isPassable || !intersects(current, rect)) continue;

        // Ships / waves / swing are destroyed by touching a wall, floor block, or ceiling block.
        if (!canLand(player.mode)) {
            player.alive = false;
            break;
        }

        auto horizontalOverlap = current.getMaxX() > rect.getMinX() + 0.5f &&
            current.getMinX() < rect.getMaxX() - 0.5f;
        if (!horizontalOverlap) continue;

        auto prevBottom = previousY - half;
        auto prevTop = previousY + half;
        auto nowBottom = player.y - half;
        auto nowTop = player.y + half;

        if (!player.upsideDown && player.vy <= 0.0 &&
            prevBottom >= rect.getMaxY() - 3.0f && nowBottom <= rect.getMaxY() + 3.0f) {
            player.y = rect.getMaxY() + half;
            player.vy = 0.0;
            player.onGround = true;
            landed = true;
            current = playerRect(player, 0.6f);
            continue;
        }

        if (player.upsideDown && player.vy >= 0.0 &&
            prevTop <= rect.getMinY() + 3.0f && nowTop >= rect.getMinY() - 3.0f) {
            player.y = rect.getMinY() - half;
            player.vy = 0.0;
            player.onGround = true;
            landed = true;
            current = playerRect(player, 0.6f);
            continue;
        }

        // Hitting the underside of a block cancels vertical velocity but does not noclip.
        if (!player.upsideDown && player.vy > 0.0 &&
            prevTop <= rect.getMinY() + 2.0f && nowTop >= rect.getMinY() - 2.0f) {
            player.y = rect.getMinY() - half;
            player.vy = 0.0;
            current = playerRect(player, 0.6f);
            continue;
        }

        if (player.upsideDown && player.vy < 0.0 &&
            prevBottom >= rect.getMaxY() - 2.0f && nowBottom <= rect.getMaxY() + 2.0f) {
            player.y = rect.getMaxY() + half;
            player.vy = 0.0;
            current = playerRect(player, 0.6f);
            continue;
        }

        // Any remaining overlap is a side / embedded collision. Kill the shadow immediately.
        // This is intentionally strict: a candidate must never look like it phased through a wall.
        player.alive = false;
    }

    if (!player.alive) return;

    if (!player.upsideDown && canLand(player.mode)) {
        if (player.y - half <= m_floorY - half + 0.5f) {
            player.y = m_floorY;
            player.vy = 0.0;
            player.onGround = true;
            landed = true;
        }
    } else if (!player.upsideDown && !canLand(player.mode) && player.y - half < m_floorY - half - 1.0f) {
        player.alive = false;
    }

    if (!landed && canLand(player.mode)) {
        // onGround remains false unless collision resolution explicitly found support.
    }

    if (player.y < -500.0f || player.y > 3400.0f)
        player.alive = false;
}


} // namespace omnipath
