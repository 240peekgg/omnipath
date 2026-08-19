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

void WorldModel::stepPlayer(SimPlayer& player, bool input, float dx, float dt) const {
    if (!player.active || !player.alive) return;

    auto frameScale = clampValue(dt * 60.0f, 0.20f, 6.0f);
    auto predictedVertical = static_cast<float>(std::abs(player.vy) * frameScale + 10.0f * frameScale);
    auto maxTravel = std::max(std::abs(dx), predictedVertical);
    auto steps = clampValue(static_cast<int>(std::ceil(maxTravel / 2.75f)), 1, 32);
    auto stepScale = frameScale / static_cast<float>(steps);
    auto stepDx = dx / static_cast<float>(steps);

    bool pressed = input && !player.held;
    player.held = input;

    for (int step = 0; step < steps && player.alive; ++step) {
        processInteractiveObjects(player, input);

        auto previousX = player.x;
        auto previousY = player.y;
        bool wasOnGround = player.onGround;
        player.onGround = false;
        double direction = player.upsideDown ? -1.0 : 1.0;

        if (player.dashing && player.dashTicks > 0) {
            player.vy = 0.0;
            --player.dashTicks;
            if (player.dashTicks == 0) player.dashing = false;
        } else {
            switch (player.mode) {
                case PlayerMode::Cube: {
                    if (input && wasOnGround)
                        player.vy = direction * (player.mini ? 10.8 : 11.45);
                    player.vy += -direction * 0.92 * stepScale;
                    player.y += static_cast<float>(player.vy * stepScale);
                    break;
                }

                case PlayerMode::Robot: {
                    if (pressed && wasOnGround)
                        player.vy = direction * (player.mini ? 9.8 : 10.6);
                    if (input && player.vy * direction > 0.0)
                        player.vy += direction * 0.25 * stepScale;
                    player.vy += -direction * 0.92 * stepScale;
                    player.y += static_cast<float>(player.vy * stepScale);
                    break;
                }

                case PlayerMode::Ship: {
                    auto acceleration = input ? 0.52 : -0.43;
                    player.vy += direction * acceleration * stepScale;
                    player.vy *= std::pow(0.985, stepScale);
                    player.vy = clampValue(player.vy, -8.8, 8.8);
                    player.y += static_cast<float>(player.vy * stepScale);
                    break;
                }

                case PlayerMode::Wave: {
                    auto waveDirection = input ? 1.0f : -1.0f;
                    if (player.upsideDown) waveDirection = -waveDirection;
                    player.y += std::abs(stepDx) * waveDirection;
                    player.vy = waveDirection * std::abs(stepDx) / std::max(0.01f, stepScale);
                    break;
                }

                case PlayerMode::Ufo: {
                    if (pressed) player.vy = direction * (player.mini ? 7.2 : 7.9);
                    player.vy += -direction * 0.62 * stepScale;
                    player.y += static_cast<float>(player.vy * stepScale);
                    break;
                }

                case PlayerMode::Ball: {
                    if (pressed) {
                        player.upsideDown = !player.upsideDown;
                        direction = player.upsideDown ? -1.0 : 1.0;
                        player.vy = direction * 3.0;
                    }
                    player.vy += -direction * 0.84 * stepScale;
                    player.y += static_cast<float>(player.vy * stepScale);
                    break;
                }

                case PlayerMode::Spider: {
                    if (pressed) {
                        player.upsideDown = !player.upsideDown;
                        player.vy = player.upsideDown ? 16.0 : -16.0;
                    }
                    player.vy *= std::pow(0.70, stepScale);
                    player.y += static_cast<float>(player.vy * stepScale);
                    break;
                }

                case PlayerMode::Swing: {
                    if (pressed) player.upsideDown = !player.upsideDown;
                    direction = player.upsideDown ? -1.0 : 1.0;
                    player.vy += direction * 0.44 * stepScale;
                    player.vy = clampValue(player.vy, -8.2, 8.2);
                    player.y += static_cast<float>(player.vy * stepScale);
                    break;
                }

                case PlayerMode::Unknown:
                default:
                    player.vy += -0.92 * stepScale;
                    player.y += static_cast<float>(player.vy * stepScale);
                    break;
            }
        }

        player.x += stepDx;
        resolveWorldCollision(player, previousX, previousY);
        if (player.alive) processInteractiveObjects(player, input);
        pressed = false;
    }
}

void WorldModel::stepAgent(SimAgent& agent, Gene gene, float dx, float dt) const {
    if (!agent.alive || agent.completed) return;

    agent.lastGene = gene;
    stepPlayer(agent.p1, gene.p1, dx, dt);
    if (agent.dual) stepPlayer(agent.p2, gene.p2, dx, dt);

    int request = agent.p1.dualRequest;
    if (agent.dual && agent.p2.dualRequest != 0) request = agent.p2.dualRequest;
    agent.p1.dualRequest = 0;
    agent.p2.dualRequest = 0;

    if (request > 0 && !agent.dual) {
        agent.dual = true;
        agent.p2 = m_startP2;
        if (!agent.p2.active) agent.p2 = agent.p1;
        agent.p2.active = true;
        agent.p2.alive = true;
        agent.p2.x = agent.p1.x;
        agent.p2.held = false;
        agent.p2.lastPortal = nullptr;
        agent.p2.lastPad = nullptr;
        agent.p2.lastRing = nullptr;
    } else if (request < 0 && agent.dual) {
        agent.dual = false;
        agent.p2.active = false;
        agent.p2.alive = false;
    }

    agent.maxX = std::max(agent.maxX, agent.p1.x);
    agent.progress = std::max(agent.progress, progressForX(agent.p1.x));
    ++agent.survivedTicks;

    bool playersAlive = agent.p1.alive && (!agent.dual || agent.p2.alive);
    if (!playersAlive) {
        agent.alive = false;
        agent.deathMode = !agent.p1.alive ? agent.p1.mode : agent.p2.mode;
        return;
    }

    if (agent.p1.x >= m_finishX - 18.0f && (!agent.dual || agent.p2.x >= m_finishX - 18.0f)) {
        agent.progress = 100.0f;
        agent.completed = true;
        agent.alive = false;
    }
}

float WorldModel::progressForX(float x) const {
    auto span = std::max(1.0f, m_finishX - m_startX);
    return clampValue((x - m_startX) / span * 100.0f, 0.0f, 100.0f);
}

float WorldModel::startX() const { return m_startX; }
float WorldModel::finishX() const { return m_finishX; }
float WorldModel::floorY() const { return m_floorY; }
std::size_t WorldModel::objectCount() const { return m_objects.size(); }
std::size_t WorldModel::hazardCount() const { return m_hazardCount; }
std::size_t WorldModel::solidCount() const { return m_solidCount; }

} // namespace omnipath
