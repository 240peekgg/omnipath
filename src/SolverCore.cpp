#include "SolverCore.hpp"

#include <chrono>
#include <cmath>
#include <stdexcept>

namespace omnipath {

EvolutionSolver::EvolutionSolver(SolverConfig config) : m_cfg(config) {
    if (m_cfg.population < 2) m_cfg.population = 2;
    if (m_cfg.eliteCount < 1) m_cfg.eliteCount = 1;
    if (m_cfg.eliteCount >= m_cfg.population) m_cfg.eliteCount = m_cfg.population - 1;
    if (m_cfg.genomeLength < 64) m_cfg.genomeLength = 64;
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
    initializePopulation();
}

void EvolutionSolver::initializePopulation() {
    m_population.assign(m_cfg.population, {});
    std::bernoulli_distribution startHeld(0.10);
    std::bernoulli_distribution toggle(0.045);

    for (auto& candidate : m_population) {
        candidate.genes.resize(m_cfg.genomeLength);
        bool p1 = startHeld(m_rng);
        bool p2 = false;
        for (std::size_t i = 0; i < candidate.genes.size(); ++i) {
            if (toggle(m_rng)) p1 = !p1;
            if (toggle(m_rng) && (i % 3 == 0)) p2 = !p2;
            candidate.genes[i] = {p1, p2};
        }
    }

    bool held = false;
    for (std::size_t i = 0; i < m_population[0].genes.size(); ++i) {
        if (i % 180 == 60 || i % 180 == 66) held = !held;
        m_population[0].genes[i] = {held, false};
    }
}

double EvolutionSolver::score(float progressPercent, std::uint32_t survivedTicks) const {
    const double p = std::clamp<double>(progressPercent, 0.0, 100.0);
    return p * p * 1000.0 + static_cast<double>(survivedTicks);
}

void EvolutionSolver::submit(float progressPercent, std::uint32_t survivedTicks) {
    auto& c = m_population.at(m_index);
    c.progress = progressPercent;
    c.survivedTicks = survivedTicks;
    c.fitness = score(progressPercent, survivedTicks);

    if (!m_hasBest || c.fitness > m_best.fitness) {
        m_best = c;
        m_hasBest = true;
    }

    ++m_index;
    if (m_index >= m_population.size()) {
        breedNextGeneration();
        m_index = 0;
        ++m_generation;
    }
}

Candidate EvolutionSolver::mutatedChild(Candidate const& parent) {
    Candidate child = parent;
    child.fitness = -1.0;
    child.progress = 0.0f;
    child.survivedTicks = 0;

    std::bernoulli_distribution mutate(m_cfg.mutationRate);
    std::bernoulli_distribution mutateP2(m_cfg.mutationRate * 0.65);
    std::bernoulli_distribution burst(m_cfg.burstMutationRate);
    std::uniform_int_distribution<std::size_t> where(0, child.genes.size() - 1);
    std::uniform_int_distribution<int> burstLen(2, 18);

    for (auto& gene : child.genes) {
        if (mutate(m_rng)) gene.p1 = !gene.p1;
        if (mutateP2(m_rng)) gene.p2 = !gene.p2;
    }

    if (burst(m_rng)) {
        auto start = where(m_rng);
        auto len = static_cast<std::size_t>(burstLen(m_rng));
        bool value = !child.genes[start].p1;
        for (std::size_t i = start; i < std::min(start + len, child.genes.size()); ++i)
            child.genes[i].p1 = value;
    }

    if (m_cfg.mode == SearchMode::GuidedMutation && m_hasBest) {
        auto reached = static_cast<std::size_t>(
            std::clamp(m_best.progress / 100.f, 0.f, 1.f) * child.genes.size()
        );
        auto stableEnd = reached > 48 ? reached - 48 : 0;
        for (std::size_t i = 0; i < stableEnd; ++i) {
            if (std::bernoulli_distribution(0.985)(m_rng))
                child.genes[i] = m_best.genes[i];
        }
    }

    return child;
}

void EvolutionSolver::breedNextGeneration() {
    std::sort(m_population.begin(), m_population.end(), [](auto const& a, auto const& b) {
        return a.fitness > b.fitness;
    });

    std::vector<Candidate> next;
    next.reserve(m_cfg.population);

    for (std::size_t i = 0; i < m_cfg.eliteCount; ++i) {
        auto elite = m_population[i];
        elite.fitness = -1.0;
        elite.progress = 0.0f;
        elite.survivedTicks = 0;
        next.push_back(std::move(elite));
    }

    std::uniform_int_distribution<std::size_t> chooseElite(0, m_cfg.eliteCount - 1);
    while (next.size() < m_cfg.population) {
        next.push_back(mutatedChild(m_population[chooseElite(m_rng)]));
    }

    m_population = std::move(next);
}

const Candidate& EvolutionSolver::current() const { return m_population.at(m_index); }
std::size_t EvolutionSolver::currentIndex() const { return m_index; }
std::size_t EvolutionSolver::generation() const { return m_generation; }
const Candidate& EvolutionSolver::best() const { return m_best; }
bool EvolutionSolver::hasBest() const { return m_hasBest; }
std::uint32_t EvolutionSolver::decisionEveryTicks() const { return m_cfg.decisionEveryTicks; }

const Gene& EvolutionSolver::geneForTick(std::uint32_t tick) const {
    auto idx = static_cast<std::size_t>(tick / m_cfg.decisionEveryTicks);
    if (idx >= current().genes.size()) idx = current().genes.size() - 1;
    return current().genes[idx];
}

} // namespace omnipath
