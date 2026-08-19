#include "SolverCore.hpp"

#include <chrono>
#include <cmath>
#include <limits>

namespace omnipath {

namespace {

template <class T>
T clampValue(T value, T lo, T hi) {
    return std::max(lo, std::min(value, hi));
}

float lane(std::size_t index, std::size_t count) {
    if (count <= 1) return 0.0f;
    auto v = static_cast<float>(index % count) / static_cast<float>(count - 1);
    return v * 2.0f - 1.0f;
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

ParallelEvolution::ParallelEvolution(SolverConfig config) : m_cfg(config) {
    m_cfg.population = clampValue<std::size_t>(m_cfg.population, 8, 100);
    m_cfg.eliteCount = clampValue<std::size_t>(m_cfg.eliteCount, 2, std::max<std::size_t>(2, m_cfg.population - 1));
    m_cfg.genomeLength = std::max<std::size_t>(m_cfg.genomeLength, 4096);
    m_cfg.decisionEveryTicks = std::max<std::uint32_t>(1, m_cfg.decisionEveryTicks);
    reset();
}

void ParallelEvolution::reset(std::uint64_t seed) {
    if (!seed) {
        seed = static_cast<std::uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count()
        );
    }

    m_rng.seed(seed);
    m_population.clear();
    m_runtime.clear();
    m_best = {};
    m_hasBest = false;
    m_generation = 0;
    m_generationLeaderIndex = 0;
    m_generationLeaderProgress = 0.0f;
    m_stagnantGenerations = 0;
    m_generationHistory.clear();
    initializePopulation();
    beginGeneration();
}

PolicyParams ParallelEvolution::seededPolicy(std::size_t i) const {
    PolicyParams p;

    auto a = lane(i, 7);
    auto b = lane(i / 7, 7);
    auto c = lane(i / 49, 3);

    p.cubeLead = 48.0f + (a + 1.0f) * 26.0f;
    p.cubeSpanGain = 0.18f + (b + 1.0f) * 0.17f;
    p.cubeHoldTicks = static_cast<std::uint16_t>(2 + (i % 5));
    p.cubeCooldown = static_cast<std::uint16_t>(2 + ((i / 5) % 4));

    p.orbLead = 20.0f + (b + 1.0f) * 14.0f;
    p.orbCooldown = static_cast<std::uint16_t>(3 + (i % 5));

    p.targetBias = b * 30.0f;
    p.shipGain = 0.045f + (a + 1.0f) * 0.025f;
    p.shipVelocityGain = 0.18f + (b + 1.0f) * 0.20f;
    p.shipDeadzone = 2.0f + (c + 1.0f) * 4.0f;
    p.shipDwell = static_cast<std::uint16_t>(1 + (i % 3));

    p.waveDeadzone = 3.0f + (b + 1.0f) * 5.0f;
    p.waveDwell = static_cast<std::uint16_t>(1 + ((i / 3) % 2));

    p.ufoDeadzone = 5.0f + (a + 1.0f) * 8.0f;
    p.ufoCooldown = static_cast<std::uint16_t>(5 + (i % 7));

    p.robotLead = 56.0f + (a + 1.0f) * 29.0f;
    p.robotHoldTicks = static_cast<std::uint16_t>(5 + ((i / 4) % 9) * 2);

    p.toggleDeadzone = 8.0f + (b + 1.0f) * 10.0f;
    p.toggleCooldown = static_cast<std::uint16_t>(4 + (i % 7));
    return p;
}

void ParallelEvolution::initializePopulation() {
    m_population.resize(m_cfg.population);
    m_runtime.resize(m_cfg.population);

    for (std::size_t i = 0; i < m_population.size(); ++i) {
        auto& candidate = m_population[i];
        candidate.policy = seededPolicy(i);
        candidate.genes.assign(m_cfg.genomeLength, {});
    }
}

void ParallelEvolution::beginGeneration() {
    m_generationLeaderIndex = 0;
    m_generationLeaderProgress = 0.0f;
    m_runtime.assign(m_population.size(), {});

    for (auto& candidate : m_population) {
        candidate.fitness = -1.0;
        candidate.progress = 0.0f;
        candidate.maxX = 0.0f;
        candidate.survivedTicks = 0;
        candidate.toggles = 0;
        candidate.deathMode = PlayerMode::Unknown;
        std::fill(candidate.genes.begin(), candidate.genes.end(), Gene{});
    }
}

bool ParallelEvolution::decidePlayer(
    std::uint32_t tick,
    PlayerObservation const& o,
    PolicyParams const& p,
    PlayerRuntime& rt
) {
    if (!o.active) return false;
    if (o.dashing) return rt.held;

    auto pulse = [&](std::uint32_t holdTicks, std::uint32_t cooldown) {
        if (rt.held && tick >= rt.heldUntil) {
            rt.held = false;
            rt.lastChangeTick = tick;
        }
        if (!rt.held && tick >= rt.cooldownUntil) {
            rt.held = true;
            rt.heldUntil = tick + std::max<std::uint32_t>(1, holdTicks);
            rt.cooldownUntil = tick + std::max<std::uint32_t>(2, cooldown);
            rt.lastChangeTick = tick;
        }
        return rt.held;
    };

    auto setHeld = [&](bool desired, std::uint32_t dwell) {
        if (desired != rt.held && tick >= rt.lastChangeTick + std::max<std::uint32_t>(1, dwell)) {
            rt.held = desired;
            rt.lastChangeTick = tick;
        }
        return rt.held;
    };

    if (o.touchingRing || (o.interactableAhead && o.nearestInteractableDx <= p.orbLead)) {
        if (tick >= rt.cooldownUntil)
            return pulse(1, p.orbCooldown);
    }

    float target = o.hasTarget ? o.targetY + p.targetBias : o.y;
    float error = target - o.y;
    if (o.upsideDown) error = -error;

    switch (o.mode) {
        case PlayerMode::Cube: {
            if (rt.held && tick >= rt.heldUntil) {
                rt.held = false;
                rt.lastChangeTick = tick;
            }

            float clusterBoost = clampValue(o.hazardSpan * p.cubeSpanGain, 0.0f, 86.0f);
            float lead = p.cubeLead + clusterBoost;
            bool shouldJump = o.hazardAhead && o.nearestHazardDx <= lead;
            if (o.onGround && shouldJump && tick >= rt.cooldownUntil) {
                rt.held = true;
                rt.heldUntil = tick + p.cubeHoldTicks;
                rt.cooldownUntil = tick + p.cubeCooldown;
                rt.lastChangeTick = tick;
            }
            return rt.held;
        }

        case PlayerMode::Robot: {
            if (rt.held && tick >= rt.heldUntil) {
                rt.held = false;
                rt.lastChangeTick = tick;
            }
            float lead = p.robotLead + clampValue(o.hazardSpan * 0.25f, 0.0f, 70.0f);
            if (o.onGround && o.hazardAhead && o.nearestHazardDx <= lead && tick >= rt.cooldownUntil) {
                rt.held = true;
                auto heightExtra = static_cast<std::uint32_t>(clampValue(std::abs(o.nearestHazardDy) / 18.0f, 0.0f, 8.0f));
                rt.heldUntil = tick + p.robotHoldTicks + heightExtra;
                rt.cooldownUntil = tick + 4;
                rt.lastChangeTick = tick;
            }
            return rt.held;
        }

        case PlayerMode::Ship: {
            float control = error * p.shipGain - static_cast<float>(o.yVelocity) * p.shipVelocityGain;
