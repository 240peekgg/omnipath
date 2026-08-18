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
    Path,
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
    std::uint32_t toggles = 0;
};

struct AttemptResult {
    float progress = 0.0f;
    std::uint32_t survivedTicks = 0;
    float deathX = 0.0f;
    float deathY = 0.0f;
    bool died = true;
};

struct DeathCluster {
    float x = 0.0f;
    float y = 0.0f;
    float progress = 0.0f;
    std::uint32_t tick = 0;
    std::uint32_t hits = 0;
    int actionBias = 0;
    bool recommendedHold = true;
    float confidence = 0.0f;
};

struct SolverConfig {
    std::size_t population = 24;
    std::size_t eliteCount = 5;
    std::size_t genomeLength = 8192;
    std::uint32_t decisionEveryTicks = 1;
    double mutationRate = 0.010;
    double burstMutationRate = 0.35;
    SearchMode mode = SearchMode::GuidedMutation;
};

class EvolutionSolver {
public:
    explicit EvolutionSolver(SolverConfig config = {});

    void reset(std::uint64_t seed = 0);
    const Candidate& current() const;
    std::size_t currentIndex() const;
    std::size_t generation() const;
    std::size_t populationSize() const;
    const Candidate& best() const;
    bool hasBest() const;

    void submit(AttemptResult const& result);
    void submit(float progressPercent, std::uint32_t survivedTicks);

    const Gene& geneForTick(std::uint32_t tick) const;
    std::uint32_t decisionEveryTicks() const;

    const std::vector<DeathCluster>& deathMap() const;
    const std::vector<float>& generationHistory() const;
    std::size_t stagnantGenerations() const;

private:
    SolverConfig m_cfg;
    std::vector<Candidate> m_population;
    Candidate m_best;
    bool m_hasBest = false;
    std::size_t m_index = 0;
    std::size_t m_generation = 0;
    std::size_t m_stagnantGenerations = 0;
    std::size_t m_childSerial = 0;
    float m_previousGenerationBest = 0.0f;
    std::vector<DeathCluster> m_deathMap;
    std::vector<float> m_generationHistory;
    std::mt19937_64 m_rng;

    void initializePopulation();
    void breedNextGeneration();
    void recordDeath(AttemptResult const& result);

    Candidate evolutionChild(Candidate const& a, Candidate const& b);
    Candidate guidedChild(Candidate const& parent);
    Candidate pathChild(Candidate const& parent);

    void clearScore(Candidate& candidate) const;
    std::uint32_t countToggles(Candidate const& candidate) const;
    double score(float progressPercent, std::uint32_t survivedTicks, std::uint32_t toggles) const;
};

const char* modeName(SearchMode mode);

} // namespace omnipath
