#pragma once

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

namespace omnipath {

enum class PlayerMode : std::uint8_t {
    Cube,
    Ship,
    Ball,
    Ufo,
    Wave,
    Robot,
    Spider,
    Swing,
    Unknown,
};

struct Gene {
    bool p1 = false;
    bool p2 = false;
};

struct PlayerObservation {
    PlayerMode mode = PlayerMode::Unknown;
    float x = 0.0f;
    float y = 0.0f;
    double yVelocity = 0.0;
    float targetY = 0.0f;
    float corridorHalfHeight = 120.0f;
    float nearestHazardDx = 99999.0f;
    float nearestHazardDy = 0.0f;
    float hazardSpan = 0.0f;
    float nearestInteractableDx = 99999.0f;
    bool hasTarget = false;
    bool hazardAhead = false;
    bool interactableAhead = false;
    bool touchingRing = false;
    bool onGround = false;
    bool upsideDown = false;
    bool dashing = false;
    bool active = true;
};

struct Observation {
    PlayerObservation p1;
    PlayerObservation p2;
    bool dual = false;
};

struct PolicyParams {
    float cubeLead = 72.0f;
    float cubeSpanGain = 0.34f;
    std::uint16_t cubeHoldTicks = 4;
    std::uint16_t cubeCooldown = 3;

    float orbLead = 34.0f;
    std::uint16_t orbCooldown = 4;

    float targetBias = 0.0f;
    float shipGain = 0.075f;
    float shipVelocityGain = 0.36f;
    float shipDeadzone = 4.0f;
    std::uint16_t shipDwell = 1;

    float waveDeadzone = 7.0f;
    std::uint16_t waveDwell = 1;

    float ufoDeadzone = 10.0f;
    std::uint16_t ufoCooldown = 7;

    float robotLead = 78.0f;
    std::uint16_t robotHoldTicks = 9;

    float toggleDeadzone = 16.0f;
    std::uint16_t toggleCooldown = 6;
};

struct Candidate {
    PolicyParams policy;
    std::vector<Gene> genes;
    double fitness = -1.0;
    float progress = 0.0f;
    float maxX = 0.0f;
    std::uint32_t survivedTicks = 0;
    std::uint32_t toggles = 0;
    PlayerMode deathMode = PlayerMode::Unknown;
};

struct AttemptResult {
    float progress = 0.0f;
    float maxX = 0.0f;
    float deathX = 0.0f;
    float deathY = 0.0f;
    std::uint32_t survivedTicks = 0;
    PlayerMode deathMode = PlayerMode::Unknown;
    bool died = true;
};

struct SolverConfig {
    std::size_t population = 32;
    std::size_t eliteCount = 6;
    std::size_t genomeLength = 24000;
    std::uint32_t decisionEveryTicks = 1;
};

class ParallelEvolution {
public:
    explicit ParallelEvolution(SolverConfig config = {});

    void reset(std::uint64_t seed = 0);
    void beginGeneration();
    Gene decide(std::size_t candidateIndex, std::uint32_t tick, Observation const& observation);
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
    std::uint32_t decisionEveryTicks() const;

private:
    struct PlayerRuntime {
        bool held = false;
        std::uint32_t heldUntil = 0;
        std::uint32_t cooldownUntil = 0;
        std::uint32_t lastChangeTick = 0;
    };

    struct CandidateRuntime {
        PlayerRuntime p1;
        PlayerRuntime p2;
    };

    SolverConfig m_cfg;
    std::vector<Candidate> m_population;
    std::vector<CandidateRuntime> m_runtime;
    Candidate m_best;
    bool m_hasBest = false;
    std::size_t m_generation = 0;
    std::size_t m_generationLeaderIndex = 0;
    float m_generationLeaderProgress = 0.0f;
    std::size_t m_stagnantGenerations = 0;
    std::vector<float> m_generationHistory;
    std::mt19937_64 m_rng;

    void initializePopulation();
    void breedNextGeneration();
    PolicyParams seededPolicy(std::size_t index) const;
    PolicyParams mutatePolicy(PolicyParams const& parent, std::size_t childIndex);
    bool decidePlayer(std::uint32_t tick, PlayerObservation const& obs, PolicyParams const& p, PlayerRuntime& rt);
    std::uint32_t countToggles(Candidate const& candidate, std::uint32_t survivedTicks) const;
    double score(AttemptResult const& result, std::uint32_t toggles) const;
};

const char* modeName(PlayerMode mode);

} // namespace omnipath
