#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace omnipath {

struct Gene {
    bool p1 = false;
    bool p2 = false;
};

struct Candidate {
    std::vector<Gene> genes;
    double fitness = -1.0;
    float progress = 0.0f;
    float maxX = 0.0f;
    std::uint32_t survivedTicks = 0;
    std::uint32_t toggles = 0;
    bool completed = false;
    std::uint64_t lineage = 0;
};

struct AttemptResult {
    float progress = 0.0f;
    float maxX = 0.0f;
    std::uint32_t survivedTicks = 0;
    bool completed = false;
};

struct EvolutionConfig {
    std::size_t population = 32;
    std::size_t eliteCount = 6;
    std::size_t genomeLength = 24000;
    std::uint32_t decisionEveryTicks = 1;
    float globalMutationRate = 0.0012f;
    float frontierMutationRate = 0.045f;
    std::uint32_t frontierWindow = 220;
};

class EvolutionEngine {
public:
    explicit EvolutionEngine(EvolutionConfig config = {});

    void reset(std::uint64_t seed = 0);
    Gene decide(std::size_t candidateIndex, std::uint32_t tick) const;
    void submitGeneration(std::vector<AttemptResult> const& results);

    std::size_t generation() const;
    std::size_t populationSize() const;
    std::size_t generationLeaderIndex() const;
    float generationLeaderProgress() const;
    std::size_t stagnantGenerations() const;
    const std::vector<float>& generationHistory() const;
    const std::vector<Candidate>& population() const;
    const Candidate& candidate(std::size_t index) const;
    const Candidate& best() const;
    bool hasBest() const;
    bool explorationGeneration() const;
    std::uint32_t decisionEveryTicks() const;
    std::uint64_t seed() const;

private:
    EvolutionConfig m_cfg;
    std::vector<Candidate> m_population;
    Candidate m_best;
    bool m_hasBest = false;
    std::size_t m_generation = 0;
    std::size_t m_generationLeaderIndex = 0;
    float m_generationLeaderProgress = 0.0f;
    std::size_t m_stagnantGenerations = 0;
    std::vector<float> m_generationHistory;
    std::mt19937_64 m_rng;
    std::uint64_t m_seed = 0;
    std::uint64_t m_nextLineage = 1;

    void initializeRandomPopulation();
    void randomizeGenome(Candidate& candidate);
    void breedNextGeneration();
    Candidate makeChild(Candidate const& a, Candidate const& b, std::size_t childIndex);
    void crossover(Candidate& child, Candidate const& a, Candidate const& b);
    void mutate(Candidate& child, std::uint32_t frontierTick, std::size_t childIndex);
    void injectRandomBurst(std::vector<Gene>& genes, std::size_t center, std::size_t radius);
    void clearScore(Candidate& candidate);
    std::uint32_t countToggles(Candidate const& candidate, std::uint32_t survivedTicks) const;
    double score(AttemptResult const& result, std::uint32_t toggles) const;
};

} // namespace omnipath
