#pragma once

#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace omnipath {

enum class SearchMode {
    Evolution,
    GuidedMutation,
};

struct Gene {
    bool p1 = false;
    bool p2 = false;
};

struct Candidate {
    std::vector<Gene> genes;
    double fitness = -1.0;
    float progress = 0.0f;
    std::uint32_t survivedTicks = 0;
};

struct SolverConfig {
    std::size_t population = 14;
    std::size_t eliteCount = 3;
    std::size_t genomeLength = 4096;
    std::uint32_t decisionEveryTicks = 2;
    double mutationRate = 0.012;
    double burstMutationRate = 0.22;
    SearchMode mode = SearchMode::Evolution;
};

class EvolutionSolver {
public:
    explicit EvolutionSolver(SolverConfig config = {});

    void reset(std::uint64_t seed = 0);
    const Candidate& current() const;
    std::size_t currentIndex() const;
    std::size_t generation() const;
    const Candidate& best() const;
    bool hasBest() const;

    void submit(float progressPercent, std::uint32_t survivedTicks);

    const Gene& geneForTick(std::uint32_t tick) const;
    std::uint32_t decisionEveryTicks() const;

private:
    SolverConfig m_cfg;
    std::vector<Candidate> m_population;
    Candidate m_best;
    bool m_hasBest = false;
    std::size_t m_index = 0;
    std::size_t m_generation = 0;
    std::mt19937_64 m_rng;

    void initializePopulation();
    void breedNextGeneration();
    Candidate mutatedChild(Candidate const& parent);
    double score(float progressPercent, std::uint32_t survivedTicks) const;
};

} // namespace omnipath
