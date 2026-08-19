#include "Evolution.hpp"

#include <chrono>
#include <cmath>
#include <limits>

namespace omnipath {

namespace {

template <class T>
T clampValue(T value, T lo, T hi) {
    return std::max(lo, std::min(value, hi));
}

} // namespace

EvolutionEngine::EvolutionEngine(EvolutionConfig config) : m_cfg(config) {
    m_cfg.population = clampValue<std::size_t>(m_cfg.population, 8, 128);
    m_cfg.eliteCount = clampValue<std::size_t>(
        m_cfg.eliteCount,
        2,
        std::max<std::size_t>(2, m_cfg.population - 1)
    );
    m_cfg.genomeLength = std::max<std::size_t>(m_cfg.genomeLength, 4096);
    m_cfg.decisionEveryTicks = std::max<std::uint32_t>(1, m_cfg.decisionEveryTicks);
    m_cfg.globalMutationRate = clampValue(m_cfg.globalMutationRate, 0.00005f, 0.05f);
    m_cfg.frontierMutationRate = clampValue(m_cfg.frontierMutationRate, 0.001f, 0.40f);
    m_cfg.frontierWindow = std::max<std::uint32_t>(32, m_cfg.frontierWindow);
    reset();
}

void EvolutionEngine::reset(std::uint64_t seedValue) {
    if (!seedValue) {
        seedValue = static_cast<std::uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count()
        );
    }

    m_seed = seedValue;
    m_rng.seed(seedValue);
    m_population.clear();
    m_best = {};
    m_hasBest = false;
    m_generation = 0;
    m_generationLeaderIndex = 0;
    m_generationLeaderProgress = 0.0f;
    m_stagnantGenerations = 0;
    m_generationHistory.clear();
    m_nextLineage = 1;
    initializeRandomPopulation();
}

void EvolutionEngine::clearScore(Candidate& candidate) {
    candidate.fitness = -1.0;
    candidate.progress = 0.0f;
    candidate.maxX = 0.0f;
    candidate.survivedTicks = 0;
    candidate.toggles = 0;
    candidate.completed = false;
}

void EvolutionEngine::initializeRandomPopulation() {
    m_population.resize(m_cfg.population);
    for (auto& candidate : m_population) {
        candidate.genes.assign(m_cfg.genomeLength, {});
        candidate.lineage = m_nextLineage++;
        randomizeGenome(candidate);
        clearScore(candidate);
    }
}

void EvolutionEngine::randomizeGenome(Candidate& candidate) {
    if (candidate.genes.size() != m_cfg.genomeLength)
        candidate.genes.assign(m_cfg.genomeLength, {});

    std::bernoulli_distribution firstState(0.30);
    std::bernoulli_distribution chooseHeld(0.48);
    std::uniform_int_distribution<int> heldDwell(1, 9);
    std::uniform_int_distribution<int> releasedDwell(1, 15);
    std::bernoulli_distribution jitterFlip(0.035);

    bool p1 = firstState(m_rng);
    bool p2 = firstState(m_rng);
    int p1Remaining = p1 ? heldDwell(m_rng) : releasedDwell(m_rng);
    int p2Remaining = p2 ? heldDwell(m_rng) : releasedDwell(m_rng);

    for (std::size_t i = 0; i < candidate.genes.size(); ++i) {
        if (p1Remaining <= 0) {
            p1 = chooseHeld(m_rng);
            p1Remaining = p1 ? heldDwell(m_rng) : releasedDwell(m_rng);
        }
        if (p2Remaining <= 0) {
            p2 = chooseHeld(m_rng);
            p2Remaining = p2 ? heldDwell(m_rng) : releasedDwell(m_rng);
        }

        // A small independent flip keeps agents from becoming synchronized even when
        // two random dwell lengths happen to line up.
        if (jitterFlip(m_rng)) p1 = !p1;
        if (jitterFlip(m_rng)) p2 = !p2;

        candidate.genes[i] = {p1, p2};
        --p1Remaining;
        --p2Remaining;
    }

    // Always begin released. This mirrors a normal level start and makes the first
    // actual press an observable random event instead of an inherited held button.
    if (!candidate.genes.empty()) candidate.genes.front() = {};
}

Gene EvolutionEngine::decide(std::size_t candidateIndex, std::uint32_t tick) const {
    if (candidateIndex >= m_population.size()) return {};
    auto const& genes = m_population[candidateIndex].genes;
    if (genes.empty()) return {};

    auto index = static_cast<std::size_t>(tick / m_cfg.decisionEveryTicks);
    if (index >= genes.size()) index = genes.size() - 1;
    return genes[index];
}

std::uint32_t EvolutionEngine::countToggles(
    Candidate const& candidate,
    std::uint32_t survivedTicks
) const {
    auto requested = static_cast<std::size_t>(
        survivedTicks / m_cfg.decisionEveryTicks + 2
    );
    auto count = std::min(candidate.genes.size(), requested);

    Gene previous{};
    std::uint32_t toggles = 0;
    for (std::size_t i = 0; i < count; ++i) {
        auto const& gene = candidate.genes[i];
        if (gene.p1 != previous.p1) ++toggles;
        if (gene.p2 != previous.p2) ++toggles;
        previous = gene;
    }
    return toggles;
}

double EvolutionEngine::score(AttemptResult const& result, std::uint32_t toggles) const {
    auto progress = clampValue<double>(result.progress, 0.0, 100.0);
    auto completion = result.completed ? 1.0e15 : 0.0;

    // Distance dominates. Survival breaks ties. Toggle cost is deliberately tiny:
    // exploration must be allowed to click a lot before evolution discovers cleaner input.
    return completion
        + progress * progress * progress * progress * 2500.0
        + static_cast<double>(result.maxX) * 400.0
        + static_cast<double>(result.survivedTicks) * 5.0
        - static_cast<double>(toggles) * 0.15;
}

void EvolutionEngine::crossover(
    Candidate& child,
    Candidate const& a,
    Candidate const& b
) {
    child.genes.resize(m_cfg.genomeLength);
    std::uniform_int_distribution<int> blockSize(12, 120);
    std::bernoulli_distribution chooseParent(0.5);

    bool fromA = chooseParent(m_rng);
    std::size_t i = 0;
    while (i < child.genes.size()) {
        auto length = static_cast<std::size_t>(blockSize(m_rng));
        auto end = std::min(child.genes.size(), i + length);
        auto const& source = fromA ? a.genes : b.genes;
        for (; i < end; ++i) child.genes[i] = source[i];
        fromA = !fromA;
    }
}

void EvolutionEngine::injectRandomBurst(
    std::vector<Gene>& genes,
    std::size_t center,
    std::size_t radius
) {
    if (genes.empty()) return;

    auto lo = center > radius ? center - radius : 0;
    auto hi = std::min(genes.size() - 1, center + radius);
    if (lo > hi) return;

    std::uniform_int_distribution<std::size_t> startDist(lo, hi);
    std::uniform_int_distribution<int> lengthDist(1, 18);
    std::bernoulli_distribution value(0.5);
    std::bernoulli_distribution mutateP2(0.55);

    auto start = startDist(m_rng);
    auto length = static_cast<std::size_t>(lengthDist(m_rng));
    auto end = std::min(genes.size(), start + length);
    bool p1 = value(m_rng);
    bool p2 = value(m_rng);
    bool doP2 = mutateP2(m_rng);

    for (auto i = start; i < end; ++i) {
        genes[i].p1 = p1;
        if (doP2) genes[i].p2 = p2;
    }
}

void EvolutionEngine::mutate(
    Candidate& child,
    std::uint32_t frontierTick,
    std::size_t childIndex
) {
    if (child.genes.empty()) return;

    float stagnationScale = 1.0f + static_cast<float>(std::min<std::size_t>(m_stagnantGenerations, 10)) * 0.18f;
    float childScale = 0.85f + static_cast<float>((childIndex % 7)) * 0.07f;
    auto globalRate = clampValue(
        m_cfg.globalMutationRate * stagnationScale * childScale,
        0.00005f,
        0.08f
    );
    auto frontierRate = clampValue(
        m_cfg.frontierMutationRate * stagnationScale * childScale,
        0.001f,
        0.45f
    );

    auto center = static_cast<std::size_t>(frontierTick / m_cfg.decisionEveryTicks);
    center = std::min(center, child.genes.size() - 1);
    auto radius = static_cast<std::size_t>(m_cfg.frontierWindow / m_cfg.decisionEveryTicks);
    radius = std::max<std::size_t>(24, radius);
    auto frontierLo = center > radius ? center - radius : 0;
    auto frontierHi = std::min(child.genes.size() - 1, center + radius);

    std::bernoulli_distribution globalFlip(globalRate);
    std::bernoulli_distribution frontierFlip(frontierRate);

    for (std::size_t i = 0; i < child.genes.size(); ++i) {
        bool inFrontier = i >= frontierLo && i <= frontierHi;
        if (globalFlip(m_rng) || (inFrontier && frontierFlip(m_rng)))
            child.genes[i].p1 = !child.genes[i].p1;
        if (globalFlip(m_rng) || (inFrontier && frontierFlip(m_rng)))
            child.genes[i].p2 = !child.genes[i].p2;
    }

    std::uniform_int_distribution<int> bursts(2, 5 + static_cast<int>(std::min<std::size_t>(m_stagnantGenerations, 5)));
    auto burstCount = bursts(m_rng);
    for (int i = 0; i < burstCount; ++i)
        injectRandomBurst(child.genes, center, radius);
}

Candidate EvolutionEngine::makeChild(
    Candidate const& a,
    Candidate const& b,
    std::size_t childIndex
) {
    Candidate child;
    child.lineage = m_nextLineage++;
    crossover(child, a, b);
    auto frontier = std::max(a.survivedTicks, b.survivedTicks);
    mutate(child, frontier, childIndex);
    clearScore(child);
    return child;
}

void EvolutionEngine::breedNextGeneration() {
    std::sort(m_population.begin(), m_population.end(), [](Candidate const& a, Candidate const& b) {
        return a.fitness > b.fitness;
    });

    auto elites = std::min(m_cfg.eliteCount, m_population.size());
    std::vector<Candidate> next;
    next.reserve(m_cfg.population);

    for (std::size_t i = 0; i < elites; ++i) {
        Candidate elite = m_population[i];
        clearScore(elite);
        next.push_back(std::move(elite));
    }

    std::uniform_int_distribution<std::size_t> parentDist(0, elites - 1);
    std::bernoulli_distribution immigrant(
        m_stagnantGenerations >= 4 ? 0.18 : 0.035
    );

    while (next.size() < m_cfg.population) {
        if (immigrant(m_rng)) {
            Candidate fresh;
            fresh.genes.assign(m_cfg.genomeLength, {});
            fresh.lineage = m_nextLineage++;
            randomizeGenome(fresh);
            clearScore(fresh);
            next.push_back(std::move(fresh));
            continue;
        }

        auto a = parentDist(m_rng);
        auto b = parentDist(m_rng);
        if (elites > 1 && a == b) b = (b + 1) % elites;
        next.push_back(makeChild(m_population[a], m_population[b], next.size()));
    }

    m_population = std::move(next);
}

void EvolutionEngine::submitGeneration(std::vector<AttemptResult> const& results) {
    if (results.size() != m_population.size()) return;

    m_generationLeaderIndex = 0;
    m_generationLeaderProgress = 0.0f;
    float generationBest = 0.0f;

    for (std::size_t i = 0; i < m_population.size(); ++i) {
        auto& candidate = m_population[i];
        auto const& result = results[i];

        candidate.progress = clampValue(result.progress, 0.0f, 100.0f);
        candidate.maxX = result.maxX;
        candidate.survivedTicks = result.survivedTicks;
        candidate.completed = result.completed;
        candidate.toggles = countToggles(candidate, result.survivedTicks);
        candidate.fitness = score(result, candidate.toggles);

        if (i == 0 || candidate.progress > m_generationLeaderProgress) {
            m_generationLeaderProgress = candidate.progress;
            m_generationLeaderIndex = i;
        }
        generationBest = std::max(generationBest, candidate.progress);

        if (!m_hasBest || candidate.fitness > m_best.fitness) {
            m_best = candidate;
            m_hasBest = true;
        }
    }

    auto previous = m_generationHistory.empty() ? -1.0f : m_generationHistory.back();
    m_generationHistory.push_back(generationBest);
    if (m_generationHistory.size() > 160)
        m_generationHistory.erase(m_generationHistory.begin());

    if (generationBest <= previous + 0.01f) ++m_stagnantGenerations;
    else m_stagnantGenerations = 0;

    breedNextGeneration();
    ++m_generation;
}

std::size_t EvolutionEngine::generation() const { return m_generation; }
std::size_t EvolutionEngine::populationSize() const { return m_population.size(); }
std::size_t EvolutionEngine::generationLeaderIndex() const { return m_generationLeaderIndex; }
float EvolutionEngine::generationLeaderProgress() const { return m_generationLeaderProgress; }
std::size_t EvolutionEngine::stagnantGenerations() const { return m_stagnantGenerations; }
const std::vector<float>& EvolutionEngine::generationHistory() const { return m_generationHistory; }
const std::vector<Candidate>& EvolutionEngine::population() const { return m_population; }
const Candidate& EvolutionEngine::candidate(std::size_t index) const { return m_population.at(index); }
const Candidate& EvolutionEngine::best() const { return m_best; }
bool EvolutionEngine::hasBest() const { return m_hasBest; }
bool EvolutionEngine::explorationGeneration() const { return m_generation == 0; }
std::uint32_t EvolutionEngine::decisionEveryTicks() const { return m_cfg.decisionEveryTicks; }
std::uint64_t EvolutionEngine::seed() const { return m_seed; }

} // namespace omnipath
