#include "SolverCore.hpp"

#include <chrono>
#include <cmath>

namespace omnipath {

namespace {

template <class T>
T clampValue(T v, T lo, T hi) {
    return std::max(lo, std::min(v, hi));
}

float normalizedLane(std::size_t index, std::size_t modulo) {
    if (modulo <= 1) return 0.0f;
    auto v = static_cast<float>(index % modulo) / static_cast<float>(modulo - 1);
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

FrontierPlanner::FrontierPlanner(SolverConfig config) : m_cfg(config) {
    m_cfg.population = clampValue<std::size_t>(m_cfg.population, 10, 100);
    m_cfg.eliteCount = clampValue<std::size_t>(m_cfg.eliteCount, 2, std::max<std::size_t>(2, m_cfg.population - 1));
    m_cfg.genomeLength = std::max<std::size_t>(m_cfg.genomeLength, 1024);
    m_cfg.decisionEveryTicks = std::max<std::uint32_t>(m_cfg.decisionEveryTicks, 1);
    reset();
}

void FrontierPlanner::reset(std::uint64_t seed) {
    if (!seed) {
        seed = static_cast<std::uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count()
        );
    }
    m_rng.seed(seed);
    m_population.clear();
    m_best = {};
    m_hasBest = false;
    m_index = 0;
    m_generation = 0;
    m_stagnantGenerations = 0;
    m_generationLeaderProgress = 0.0f;
    m_generationLeaderIndex = 0;
    m_frontierTick = 0;
    m_generationHistory.clear();
    resetRuntime();
    initializePopulation();
}

void FrontierPlanner::resetRuntime() {
    m_runtime1 = {};
    m_runtime2 = {};
}

PolicyParams FrontierPlanner::seededPolicy(std::size_t i) const {
    PolicyParams p;
    auto lane5 = normalizedLane(i, 5);
    auto lane7 = normalizedLane(i / 5, 7);
    auto lane4 = normalizedLane(i / 35, 4);

    p.hazardLead = 70.0f + (lane5 + 1.0f) * 28.0f;
    p.orbLead = 28.0f + (lane7 + 1.0f) * 12.0f;
    p.cubeTapTicks = static_cast<std::uint16_t>(1 + (i % 4));
    p.robotHoldTicks = static_cast<std::uint16_t>(5 + ((i / 4) % 8) * 2);
    p.ufoCooldown = static_cast<std::uint16_t>(7 + (i % 7));
    p.toggleCooldown = static_cast<std::uint16_t>(6 + ((i / 7) % 8));
    p.shipDwell = static_cast<std::uint16_t>(2 + (i % 4));
    p.waveDwell = static_cast<std::uint16_t>(1 + (i % 3));
    p.targetBias = lane7 * 34.0f;
    p.verticalGain = 0.045f + (lane5 + 1.0f) * 0.015f;
    p.velocityGain = 0.45f + (lane7 + 1.0f) * 0.22f;
    p.hysteresis = 4.0f + (lane4 + 1.0f) * 5.0f;
    p.frontierJitter = lane5 * 20.0f;
    return p;
}

void FrontierPlanner::initializePopulation() {
    m_population.resize(m_cfg.population);
    for (std::size_t i = 0; i < m_population.size(); ++i) {
        auto& c = m_population[i];
        c.policy = seededPolicy(i);
        c.genes.assign(m_cfg.genomeLength, {});
    }
}

bool FrontierPlanner::decidePlayer(
    std::uint32_t tick,
    PlayerObservation const& o,
    PolicyParams const& p,
    PlayerRuntime& rt,
    bool inheritedHeld
) {
    if (!o.active) return false;

    if (tick < m_frontierTick && m_hasBest) {
        rt.held = inheritedHeld;
        rt.lastChangeTick = tick;
        return inheritedHeld;
    }

    if (o.dashing) return rt.held;

    auto setHeld = [&](bool desired, std::uint32_t dwell) {
        if (desired != rt.held && tick >= rt.lastChangeTick + dwell) {
            rt.held = desired;
            rt.lastChangeTick = tick;
        }
        return rt.held;
    };

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

    if (o.touchingRing || (o.interactableAhead && o.nearestInteractableDx <= p.orbLead)) {
        if (tick >= rt.cooldownUntil)
            return pulse(1, 5);
    }

    float target = o.hasTarget ? (o.targetY + p.targetBias) : o.y;
    float error = target - o.y;
    float control = error * p.verticalGain - static_cast<float>(o.yVelocity) * p.velocityGain;
    if (o.upsideDown) control = -control;

    float lead = p.hazardLead + p.frontierJitter;
    bool hazardSoon = o.hazardAhead && o.nearestHazardDx <= lead;

    switch (o.mode) {
        case PlayerMode::Cube: {
            if (rt.held && tick >= rt.heldUntil) {
                rt.held = false;
                rt.lastChangeTick = tick;
            }
            if (o.onGround && hazardSoon && tick >= rt.cooldownUntil) {
                rt.held = true;
                rt.heldUntil = tick + p.cubeTapTicks;
                rt.cooldownUntil = tick + 8;
                rt.lastChangeTick = tick;
            }
            return rt.held;
        }

        case PlayerMode::Robot: {
            if (rt.held && tick >= rt.heldUntil) {
                rt.held = false;
                rt.lastChangeTick = tick;
            }
            if (o.onGround && hazardSoon && tick >= rt.cooldownUntil) {
                rt.held = true;
                auto extra = static_cast<int>(std::max(0.0f, o.nearestHazardDy) / 18.0f);
                rt.heldUntil = tick + clampValue<std::uint32_t>(p.robotHoldTicks + extra, 4, 28);
                rt.cooldownUntil = tick + 10;
                rt.lastChangeTick = tick;
            }
            return rt.held;
        }

        case PlayerMode::Ship: {
            bool desired = control > p.hysteresis * 0.01f;
            return setHeld(desired, p.shipDwell);
        }

        case PlayerMode::Wave: {
            float threshold = std::max(2.0f, p.hysteresis * 0.55f);
            bool desired = o.upsideDown ? (error < -threshold) : (error > threshold);
            return setHeld(desired, p.waveDwell);
        }

        case PlayerMode::Ufo: {
            if (rt.held && tick >= rt.heldUntil) {
                rt.held = false;
                rt.lastChangeTick = tick;
            }
            bool needLift = o.upsideDown ? (error < -p.hysteresis) : (error > p.hysteresis);
            if (needLift && tick >= rt.cooldownUntil)
                return pulse(1, p.ufoCooldown);
            return rt.held;
        }

        case PlayerMode::Ball:
        case PlayerMode::Spider:
        case PlayerMode::Swing: {
            if (rt.held && tick >= rt.heldUntil) {
                rt.held = false;
                rt.lastChangeTick = tick;
            }
            bool needSwitch = std::abs(error) > std::max(12.0f, p.hysteresis * 2.0f);
            if ((needSwitch || hazardSoon) && tick >= rt.cooldownUntil)
                return pulse(1, p.toggleCooldown);
            return rt.held;
        }

        case PlayerMode::Unknown:
        default: {
            if (rt.held && tick >= rt.heldUntil) rt.held = false;
            if (o.onGround && hazardSoon && tick >= rt.cooldownUntil)
                return pulse(2, 8);
            return rt.held;
        }
    }
}

Gene FrontierPlanner::decide(std::uint32_t tick, Observation const& obs) {
    auto& c = m_population.at(m_index);
    auto geneIndex = static_cast<std::size_t>(tick / m_cfg.decisionEveryTicks);
    if (geneIndex >= c.genes.size()) geneIndex = c.genes.size() - 1;

    Gene inherited{};
    if (m_hasBest && geneIndex < m_best.genes.size()) inherited = m_best.genes[geneIndex];

    Gene out;
    out.p1 = decidePlayer(tick, obs.p1, c.policy, m_runtime1, inherited.p1);
    out.p2 = obs.dual
        ? decidePlayer(tick, obs.p2, c.policy, m_runtime2, inherited.p2)
        : false;

    c.genes[geneIndex] = out;
    return out;
}

std::uint32_t FrontierPlanner::countToggles(Candidate const& c) const {
    Gene prev{};
    std::uint32_t toggles = 0;
    for (auto const& g : c.genes) {
        if (g.p1 != prev.p1) ++toggles;
        if (g.p2 != prev.p2) ++toggles;
        prev = g;
    }
    return toggles;
}

double FrontierPlanner::score(AttemptResult const& r, std::uint32_t toggles) const {
    double p = clampValue<double>(r.progress, 0.0, 100.0);
    return p * p * 100000.0
        + static_cast<double>(r.maxX) * 180.0
        + static_cast<double>(r.survivedTicks) * 2.0
        - static_cast<double>(toggles) * 3.0;
}

PolicyParams FrontierPlanner::mutatePolicy(PolicyParams const& parent, std::size_t childIndex) {
    PolicyParams p = parent;
    double spread = 1.0 + std::min<std::size_t>(m_stagnantGenerations, 8) * 0.22;
    std::normal_distribution<double> n(0.0, spread);
    float lane = normalizedLane(childIndex, 11);

    p.hazardLead = clampValue<float>(p.hazardLead + static_cast<float>(n(m_rng) * 7.0) + lane * 8.0f, 35.0f, 190.0f);
    p.orbLead = clampValue<float>(p.orbLead + static_cast<float>(n(m_rng) * 3.5), 16.0f, 80.0f);
    p.cubeTapTicks = clampValue<std::uint16_t>(static_cast<std::uint16_t>(std::max<int>(1, static_cast<int>(p.cubeTapTicks) + static_cast<int>(std::round(n(m_rng))))), 1, 8);
    p.robotHoldTicks = clampValue<std::uint16_t>(static_cast<std::uint16_t>(std::max<int>(2, static_cast<int>(p.robotHoldTicks) + static_cast<int>(std::round(n(m_rng) * 2.0)))), 3, 34);
    p.ufoCooldown = clampValue<std::uint16_t>(static_cast<std::uint16_t>(std::max<int>(3, static_cast<int>(p.ufoCooldown) + static_cast<int>(std::round(n(m_rng))))), 4, 24);
    p.toggleCooldown = clampValue<std::uint16_t>(static_cast<std::uint16_t>(std::max<int>(3, static_cast<int>(p.toggleCooldown) + static_cast<int>(std::round(n(m_rng))))), 4, 24);
    p.shipDwell = clampValue<std::uint16_t>(static_cast<std::uint16_t>(std::max<int>(1, static_cast<int>(p.shipDwell) + static_cast<int>(std::round(n(m_rng) * 0.5)))), 1, 8);
    p.waveDwell = clampValue<std::uint16_t>(static_cast<std::uint16_t>(std::max<int>(1, static_cast<int>(p.waveDwell) + static_cast<int>(std::round(n(m_rng) * 0.35)))), 1, 5);
    p.targetBias = clampValue<float>(p.targetBias + static_cast<float>(n(m_rng) * 7.0) + lane * 4.0f, -90.0f, 90.0f);
    p.verticalGain = clampValue<float>(p.verticalGain + static_cast<float>(n(m_rng) * 0.006), 0.015f, 0.14f);
    p.velocityGain = clampValue<float>(p.velocityGain + static_cast<float>(n(m_rng) * 0.08), 0.10f, 1.75f);
    p.hysteresis = clampValue<float>(p.hysteresis + static_cast<float>(n(m_rng) * 1.8), 2.0f, 32.0f);
    p.frontierJitter = clampValue<float>(static_cast<float>(n(m_rng) * 16.0) + lane * 14.0f, -45.0f, 45.0f);
    return p;
}

void FrontierPlanner::breedNextGeneration() {
    std::sort(m_population.begin(), m_population.end(), [](Candidate const& a, Candidate const& b) {
        return a.fitness > b.fitness;
    });

    std::vector<Candidate> next;
    next.reserve(m_cfg.population);

    if (m_hasBest) {
        Candidate champion;
        champion.policy = m_best.policy;
        champion.genes.assign(m_cfg.genomeLength, {});
        auto prefixGenes = std::min<std::size_t>(m_frontierTick / m_cfg.decisionEveryTicks, m_best.genes.size());
        std::copy_n(m_best.genes.begin(), prefixGenes, champion.genes.begin());
        next.push_back(std::move(champion));
    }

    std::size_t elites = std::min(m_cfg.eliteCount, m_population.size());
    for (std::size_t i = 0; i < elites && next.size() < m_cfg.population; ++i) {
        Candidate c;
        c.policy = m_population[i].policy;
        c.genes.assign(m_cfg.genomeLength, {});
        if (m_hasBest) {
            auto prefixGenes = std::min<std::size_t>(m_frontierTick / m_cfg.decisionEveryTicks, m_best.genes.size());
            std::copy_n(m_best.genes.begin(), prefixGenes, c.genes.begin());
        }
        next.push_back(std::move(c));
    }

    std::uniform_int_distribution<std::size_t> choose(0, elites - 1);
    while (next.size() < m_cfg.population) {
        auto parentIndex = choose(m_rng);
        Candidate c;
        c.policy = mutatePolicy(m_population[parentIndex].policy, next.size());
        c.genes.assign(m_cfg.genomeLength, {});
        if (m_hasBest) {
            auto prefixGenes = std::min<std::size_t>(m_frontierTick / m_cfg.decisionEveryTicks, m_best.genes.size());
            std::copy_n(m_best.genes.begin(), prefixGenes, c.genes.begin());
        }
        next.push_back(std::move(c));
    }

    m_population = std::move(next);
}

void FrontierPlanner::submit(AttemptResult const& result) {
    auto& c = m_population.at(m_index);
    c.progress = clampValue<float>(result.progress, 0.0f, 100.0f);
    c.maxX = result.maxX;
    c.survivedTicks = result.survivedTicks;
    c.deathMode = result.deathMode;
    c.toggles = countToggles(c);
    c.fitness = score(result, c.toggles);

    if (m_index == 0 || c.progress > m_generationLeaderProgress) {
        m_generationLeaderProgress = c.progress;
        m_generationLeaderIndex = m_index;
    }

    if (!m_hasBest || c.fitness > m_best.fitness) {
        m_best = c;
        m_hasBest = true;
        m_frontierTick = result.survivedTicks > m_cfg.frontierMarginTicks
            ? result.survivedTicks - m_cfg.frontierMarginTicks
            : 0;
    }

    ++m_index;
    resetRuntime();

    // Hard generation barrier: breeding starts only after candidate N/N.
    if (m_index < m_population.size()) return;

    float generationBest = 0.0f;
    for (auto const& candidate : m_population)
        generationBest = std::max(generationBest, candidate.progress);

    m_generationHistory.push_back(generationBest);
    if (m_generationHistory.size() > 120)
        m_generationHistory.erase(m_generationHistory.begin());

    float previous = m_generationHistory.size() > 1
        ? m_generationHistory[m_generationHistory.size() - 2]
        : -1.0f;
    if (generationBest <= previous + 0.01f) ++m_stagnantGenerations;
    else m_stagnantGenerations = 0;

    breedNextGeneration();
    ++m_generation;
    m_index = 0;
    m_generationLeaderProgress = 0.0f;
    m_generationLeaderIndex = 0;
    resetRuntime();
}

const Candidate& FrontierPlanner::current() const { return m_population.at(m_index); }
std::size_t FrontierPlanner::currentIndex() const { return m_index; }
std::size_t FrontierPlanner::generation() const { return m_generation; }
std::size_t FrontierPlanner::populationSize() const { return m_population.size(); }
const Candidate& FrontierPlanner::best() const { return m_best; }
bool FrontierPlanner::hasBest() const { return m_hasBest; }
std::uint32_t FrontierPlanner::decisionEveryTicks() const { return m_cfg.decisionEveryTicks; }
std::uint32_t FrontierPlanner::frontierTick() const { return m_frontierTick; }
float FrontierPlanner::generationLeaderProgress() const { return m_generationLeaderProgress; }
std::size_t FrontierPlanner::generationLeaderIndex() const { return m_generationLeaderIndex; }
std::size_t FrontierPlanner::stagnantGenerations() const { return m_stagnantGenerations; }
const std::vector<float>& FrontierPlanner::generationHistory() const { return m_generationHistory; }

} // namespace omnipath
