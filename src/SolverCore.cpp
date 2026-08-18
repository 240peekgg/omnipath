#include "SolverCore.hpp"

#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace omnipath {

namespace {

std::size_t clampIndex(long long value, std::size_t size) {
    if (size == 0) return 0;
    if (value < 0) return 0;
    auto v = static_cast<std::size_t>(value);
    return std::min(v, size - 1);
}

} // namespace

const char* modeName(SearchMode mode) {
    switch (mode) {
        case SearchMode::Evolution: return "evolution";
        case SearchMode::GuidedMutation: return "guided";
        case SearchMode::Path: return "path";
    }
    return "unknown";
}

EvolutionSolver::EvolutionSolver(SolverConfig config) : m_cfg(config) {
    if (m_cfg.population < 10) m_cfg.population = 10;
    if (m_cfg.population > 100) m_cfg.population = 100;
    if (m_cfg.eliteCount < 2) m_cfg.eliteCount = 2;
    if (m_cfg.eliteCount >= m_cfg.population) m_cfg.eliteCount = m_cfg.population - 1;
    if (m_cfg.genomeLength < 256) m_cfg.genomeLength = 256;
    if (m_cfg.decisionEveryTicks < 1) m_cfg.decisionEveryTicks = 1;
    reset();
}

void EvolutionSolver::reset(std::uint64_t seed) {
    if (!seed) {
        seed = static_cast<std::uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count()
        );
    }
    m_rng.seed(seed);
    m_generation = 0;
    m_index = 0;
    m_hasBest = false;
    m_best = {};
    m_stagnantGenerations = 0;
    m_previousGenerationBest = 0.0f;
    m_childSerial = 0;
    m_deathMap.clear();
    m_generationHistory.clear();
    initializePopulation();
}

void EvolutionSolver::initializePopulation() {
    m_population.assign(m_cfg.population, {});

    for (std::size_t n = 0; n < m_population.size(); ++n) {
        auto& candidate = m_population[n];
        candidate.genes.resize(m_cfg.genomeLength);

        // Candidate zero is a pure no-input probe. It gives PATH mode a clean
        // first death point instead of starting with meaningless random spam.
        if (n == 0) {
            continue;
        }

        // Several deterministic tap/hold rhythms make the first generation
        // dramatically less stupid on basic cube gameplay. They also serve as
        // useful seeds for robot, UFO and simple ship sections.
        if (n < 10) {
            const std::size_t period = 30 + n * 7;
            const std::size_t hold = 4 + (n % 4) * 3;
            const std::size_t offset = 7 + n * 3;
            for (std::size_t i = 0; i < candidate.genes.size(); ++i) {
                auto phase = (i + period - (offset % period)) % period;
                candidate.genes[i].p1 = phase < hold;
                candidate.genes[i].p2 = false;
            }
            continue;
        }

        // The rest are random, but piecewise-constant instead of per-frame
        // noise. This produces actual taps and holds that GD can use.
        std::bernoulli_distribution startHeld(0.08);
        std::bernoulli_distribution toggleP1(0.028);
        std::bernoulli_distribution toggleP2(0.009);
        bool p1 = startHeld(m_rng);
        bool p2 = false;
        std::size_t dwell1 = 0;
        std::size_t dwell2 = 0;

        for (std::size_t i = 0; i < candidate.genes.size(); ++i) {
            if (dwell1 >= 3 && toggleP1(m_rng)) {
                p1 = !p1;
                dwell1 = 0;
            }
            if (dwell2 >= 8 && toggleP2(m_rng)) {
                p2 = !p2;
                dwell2 = 0;
            }
            candidate.genes[i] = {p1, p2};
            ++dwell1;
            ++dwell2;
        }
    }
}

void EvolutionSolver::clearScore(Candidate& candidate) const {
    candidate.fitness = -1.0;
    candidate.progress = 0.0f;
    candidate.survivedTicks = 0;
    candidate.toggles = 0;
}

std::uint32_t EvolutionSolver::countToggles(Candidate const& candidate) const {
    bool p1 = false;
    bool p2 = false;
    std::uint32_t toggles = 0;
    for (auto const& g : candidate.genes) {
        if (g.p1 != p1) ++toggles;
        if (g.p2 != p2) ++toggles;
        p1 = g.p1;
        p2 = g.p2;
    }
    return toggles;
}

double EvolutionSolver::score(
    float progressPercent,
    std::uint32_t survivedTicks,
    std::uint32_t toggles
) const {
    const double p = std::clamp<double>(progressPercent, 0.0, 100.0);

    // Progress wins by a huge margin. Survival breaks ties, while input spam
    // gets a tiny penalty so two equally successful routes prefer the cleaner
    // macro instead of 500 useless clicks.
    return p * p * 3000.0
        + static_cast<double>(survivedTicks) * 4.0
        - static_cast<double>(toggles) * 0.20;
}

void EvolutionSolver::recordDeath(AttemptResult const& result) {
    if (!result.died || m_population.empty() || m_index >= m_population.size()) return;

    auto const& candidate = m_population[m_index];
    if (candidate.genes.empty()) return;

    auto geneIndex = std::min<std::size_t>(
        result.survivedTicks / m_cfg.decisionEveryTicks,
        candidate.genes.size() - 1
    );
    bool wasHeld = candidate.genes[geneIndex].p1;

    DeathCluster* match = nullptr;
    float bestDistance = std::numeric_limits<float>::max();

    for (auto& cluster : m_deathMap) {
        float dx = std::abs(cluster.x - result.deathX);
        float dp = std::abs(cluster.progress - result.progress);
        if (dx <= 72.0f && dp <= 1.75f) {
            float d = dx + dp * 24.0f;
            if (d < bestDistance) {
                bestDistance = d;
                match = &cluster;
            }
        }
    }

    if (!match) {
        DeathCluster cluster;
        cluster.x = result.deathX;
        cluster.y = result.deathY;
        cluster.progress = result.progress;
        cluster.tick = result.survivedTicks;
        cluster.hits = 1;
        cluster.actionBias = wasHeld ? -1 : 1;
        cluster.recommendedHold = cluster.actionBias >= 0;
        cluster.confidence = 1.0f;
        m_deathMap.push_back(cluster);
        return;
    }

    auto oldHits = match->hits;
    auto newHits = oldHits + 1;
    auto blend = [oldHits, newHits](float oldValue, float newValue) {
        return (oldValue * static_cast<float>(oldHits) + newValue)
            / static_cast<float>(newHits);
    };

    match->x = blend(match->x, result.deathX);
    match->y = blend(match->y, result.deathY);
    match->progress = blend(match->progress, result.progress);
    match->tick = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(match->tick) * oldHits + result.survivedTicks)
        / newHits
    );
    match->hits = newHits;

    // If an attempt reaches this hazard while not holding, PATH should first
    // try a press. If it dies while holding, it should also explore release.
    match->actionBias += wasHeld ? -1 : 1;
    match->recommendedHold = match->actionBias >= 0;
    match->confidence = std::min(
        1.0f,
        static_cast<float>(std::abs(match->actionBias))
            / static_cast<float>(match->hits)
    );
}

void EvolutionSolver::submit(AttemptResult const& result) {
    auto& c = m_population.at(m_index);
    c.progress = std::clamp(result.progress, 0.0f, 100.0f);
    c.survivedTicks = result.survivedTicks;
    c.toggles = countToggles(c);
    c.fitness = score(c.progress, c.survivedTicks, c.toggles);

    recordDeath(result);

    if (!m_hasBest || c.fitness > m_best.fitness) {
        m_best = c;
        m_hasBest = true;
    }

    ++m_index;
    if (m_index < m_population.size()) return;

    float generationBest = 0.0f;
    for (auto const& candidate : m_population)
        generationBest = std::max(generationBest, candidate.progress);

    m_generationHistory.push_back(generationBest);
    if (m_generationHistory.size() > 100)
        m_generationHistory.erase(m_generationHistory.begin());

    if (generationBest <= m_previousGenerationBest + 0.02f)
        ++m_stagnantGenerations;
    else
        m_stagnantGenerations = 0;

    m_previousGenerationBest = std::max(m_previousGenerationBest, generationBest);

    breedNextGeneration();
    m_index = 0;
    ++m_generation;
}

void EvolutionSolver::submit(float progressPercent, std::uint32_t survivedTicks) {
    AttemptResult result;
    result.progress = progressPercent;
    result.survivedTicks = survivedTicks;
    result.died = false;
    submit(result);
}

Candidate EvolutionSolver::evolutionChild(Candidate const& a, Candidate const& b) {
    Candidate child = a;
    clearScore(child);

    if (child.genes.empty()) return child;

    std::uniform_int_distribution<std::size_t> cutDist(0, child.genes.size() - 1);
    auto cut = cutDist(m_rng);
    for (std::size_t i = cut; i < child.genes.size(); ++i)
        child.genes[i] = b.genes[i];

    double adaptive = m_cfg.mutationRate * (1.0 + std::min<std::size_t>(m_stagnantGenerations, 8) * 0.22);
    std::bernoulli_distribution mutate(adaptive);
    std::bernoulli_distribution mutateP2(adaptive * 0.45);

    for (auto& gene : child.genes) {
        if (mutate(m_rng)) gene.p1 = !gene.p1;
        if (mutateP2(m_rng)) gene.p2 = !gene.p2;
    }

    std::bernoulli_distribution burst(m_cfg.burstMutationRate);
    if (burst(m_rng)) {
        std::uniform_int_distribution<std::size_t> where(0, child.genes.size() - 1);
        std::uniform_int_distribution<int> length(3, 30);
        auto start = where(m_rng);
        auto len = static_cast<std::size_t>(length(m_rng));
        bool value = !child.genes[start].p1;
        for (std::size_t i = start; i < std::min(start + len, child.genes.size()); ++i)
            child.genes[i].p1 = value;
    }

    return child;
}

Candidate EvolutionSolver::guidedChild(Candidate const& parent) {
    Candidate child = parent;
    clearScore(child);
    if (child.genes.empty()) return child;

    auto deathGene = std::min<std::size_t>(
        parent.survivedTicks / m_cfg.decisionEveryTicks,
        child.genes.size() - 1
    );

    // The crucial difference from v0.1: GUIDED uses the actual death tick,
    // not level percent mapped across the whole 8192-gene macro. Everything
    // well before the failure remains frozen.
    auto windowStart = deathGene > 110 ? deathGene - 110 : 0;
    auto windowEnd = std::min<std::size_t>(deathGene + 150, child.genes.size() - 1);

    double localRate = 0.020 + std::min<std::size_t>(m_stagnantGenerations, 10) * 0.004;
    std::bernoulli_distribution localMut(localRate);
    std::bernoulli_distribution p2Mut(localRate * 0.35);

    for (std::size_t i = windowStart; i <= windowEnd; ++i) {
        if (localMut(m_rng)) child.genes[i].p1 = !child.genes[i].p1;
        if (p2Mut(m_rng)) child.genes[i].p2 = !child.genes[i].p2;
    }

    // Deterministic probes around the exact place of death make basic jumps
    // discoverable quickly instead of waiting for a lucky random mutation.
    static constexpr int shifts[] = {-34, -26, -20, -15, -11, -7, -3, 2, 7, 12};
    auto serial = m_childSerial++;
    auto variant = serial % (sizeof(shifts) / sizeof(shifts[0]));
    auto start = clampIndex(static_cast<long long>(deathGene) + shifts[variant], child.genes.size());
    auto len = static_cast<std::size_t>(4 + (serial % 5) * 4);
    bool desired = (serial % 4) != 3; // mostly press probes, sometimes release

    auto restore = parent.genes[std::min(start + len, parent.genes.size() - 1)].p1;
    for (std::size_t i = start; i < std::min(start + len, child.genes.size()); ++i)
        child.genes[i].p1 = desired;
    if (start + len < child.genes.size())
        child.genes[start + len].p1 = restore;

    return child;
}

Candidate EvolutionSolver::pathChild(Candidate const& parent) {
    Candidate child = parent;
    clearScore(child);
    if (child.genes.empty()) return child;

    if (m_deathMap.empty())
        return guidedChild(parent);

    // Prefer the furthest learned obstacle, with repeated deaths adding some
    // confidence. This is our lightweight approximate hazard map.
    auto bestIt = m_deathMap.begin();
    double bestValue = -1.0;
    for (auto it = m_deathMap.begin(); it != m_deathMap.end(); ++it) {
        double value = static_cast<double>(it->progress) * 100.0
            + static_cast<double>(std::min<std::uint32_t>(it->hits, 20)) * 3.0;
        if (value > bestValue) {
            bestValue = value;
            bestIt = it;
        }
    }

    auto const& hazard = *bestIt;
    auto targetGene = std::min<std::size_t>(
        hazard.tick / m_cfg.decisionEveryTicks,
        child.genes.size() - 1
    );

    auto serial = m_childSerial++;
    static constexpr int lead[] = {6, 9, 12, 15, 18, 22, 27, 32, 38, 45};
    auto leadTicks = lead[serial % (sizeof(lead) / sizeof(lead[0]))];
    auto start = clampIndex(static_cast<long long>(targetGene) - leadTicks, child.genes.size());
    auto len = static_cast<std::size_t>(4 + (serial % 7) * 3);

    bool desired = hazard.recommendedHold;
    if (serial % 5 == 4) desired = !desired;

    // Most PATH children probe P1. Every sixth one probes P2 as well so dual
    // sections can eventually build their own successful branch.
    bool targetP2 = (serial % 6 == 5);
    bool restore1 = parent.genes[std::min(start + len, parent.genes.size() - 1)].p1;
    bool restore2 = parent.genes[std::min(start + len, parent.genes.size() - 1)].p2;

    for (std::size_t i = start; i < std::min(start + len, child.genes.size()); ++i) {
        if (targetP2) child.genes[i].p2 = desired;
        else child.genes[i].p1 = desired;
    }

    if (start + len < child.genes.size()) {
        if (targetP2) child.genes[start + len].p2 = restore2;
        else child.genes[start + len].p1 = restore1;
    }

    // Small local exploration after the obstacle. Once a probe clears a spike
    // the solver should immediately be able to adapt to the next one.
    auto localEnd = std::min<std::size_t>(targetGene + 100, child.genes.size() - 1);
    std::bernoulli_distribution local(0.012 + std::min<std::size_t>(m_stagnantGenerations, 8) * 0.003);
    for (std::size_t i = targetGene; i <= localEnd; ++i) {
        if (local(m_rng)) child.genes[i].p1 = !child.genes[i].p1;
    }

    return child;
}

void EvolutionSolver::breedNextGeneration() {
    std::sort(m_population.begin(), m_population.end(), [](auto const& a, auto const& b) {
        return a.fitness > b.fitness;
    });

    std::vector<Candidate> next;
    next.reserve(m_cfg.population);

    // Always keep the global best route as the first seed. This guarantees
    // that learning a new obstacle can never destroy the already solved path.
    if (m_hasBest) {
        auto champion = m_best;
        clearScore(champion);
        next.push_back(std::move(champion));
    }

    for (std::size_t i = 0; i < m_cfg.eliteCount && next.size() < m_cfg.population; ++i) {
        auto elite = m_population[i];
        clearScore(elite);
        next.push_back(std::move(elite));
    }

    std::uniform_int_distribution<std::size_t> chooseElite(0, m_cfg.eliteCount - 1);

    while (next.size() < m_cfg.population) {
        auto const& a = m_population[chooseElite(m_rng)];
        Candidate child;

        switch (m_cfg.mode) {
            case SearchMode::Evolution: {
                auto const& b = m_population[chooseElite(m_rng)];
                child = evolutionChild(a, b);
                break;
            }
            case SearchMode::GuidedMutation:
                child = guidedChild(m_hasBest ? m_best : a);
                break;
            case SearchMode::Path:
                child = pathChild(m_hasBest ? m_best : a);
                break;
        }

        next.push_back(std::move(child));
    }

    m_population = std::move(next);
}

const Candidate& EvolutionSolver::current() const { return m_population.at(m_index); }
std::size_t EvolutionSolver::currentIndex() const { return m_index; }
std::size_t EvolutionSolver::generation() const { return m_generation; }
std::size_t EvolutionSolver::populationSize() const { return m_population.size(); }
const Candidate& EvolutionSolver::best() const { return m_best; }
bool EvolutionSolver::hasBest() const { return m_hasBest; }
std::uint32_t EvolutionSolver::decisionEveryTicks() const { return m_cfg.decisionEveryTicks; }
const std::vector<DeathCluster>& EvolutionSolver::deathMap() const { return m_deathMap; }
const std::vector<float>& EvolutionSolver::generationHistory() const { return m_generationHistory; }
std::size_t EvolutionSolver::stagnantGenerations() const { return m_stagnantGenerations; }

const Gene& EvolutionSolver::geneForTick(std::uint32_t tick) const {
    auto idx = static_cast<std::size_t>(tick / m_cfg.decisionEveryTicks);
    if (idx >= current().genes.size()) idx = current().genes.size() - 1;
    return current().genes[idx];
}

} // namespace omnipath
