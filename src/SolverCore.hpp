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
    float playerSpeed = 1.0f;
    float targetY = 0.0f;
    float corridorHalfHeight = 120.0f;
    float nearestHazardDx = 99999.0f;
    float nearestHazardDy = 0.0f;
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
    float hazardLead = 95.0f;
    float orbLead = 42.0f;
    std::uint16_t cubeTapTicks = 2;
    std::uint16_t robotHoldTicks = 9;
    std::uint16_t ufoCooldown = 10;
    std::uint16_t toggleCooldown = 9;
    std::uint16_t shipDwell = 3;
    std::uint16_t waveDwell = 2;
    float targetBias = 0.0f;
    float verticalGain = 0.060f;
    float velocityGain = 0.65f;
    float hysteresis = 7.0f;
    float frontierJitter = 0.0f;
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
    std::size_t population = 100;
    std::size_t eliteCount = 12;
    std::size_t genomeLength = 16000;
    std::uint32_t decisionEveryTicks = 1;
    std::uint32_t frontierMarginTicks = 80;
};

class FrontierPlanner {
public:
    explicit FrontierPlanner(SolverConfig config = {});

    void reset(std::uint64_t seed = 0);
    Gene decide(std::uint32_t tick, Observation const& observation);
    void submit(AttemptResult const& result);

    const Candidate& current() const;
    std::size_t currentIndex() const;
    std::size_t generation() const;
    std::size_t populationSize() const;
    const Candidate& best() const;
    bool hasBest() const;
    std::uint32_t decisionEveryTicks() const;
    std::uint32_t frontierTick() const;
    float generationLeaderProgress() const;
    std::size_t generationLeaderIndex() const;
    std::size_t stagnantGenerations() const;
    const std::vector<float>& generationHistory() const;

private:
    struct PlayerRuntime {
        bool held = false;
        std::uint32_t heldUntil = 0;
        std::uint32_t lastChangeTick = 0;
        std::uint32_t cooldownUntil = 0;
    };

    SolverConfig m_cfg;
    std::vector<Candidate> m_population;
    Candidate m_best;
    bool m_hasBest = false;
    std::size_t m_index = 0;
    std::size_t m_generation = 0;
    std::size_t m_stagnantGenerations = 0;
    float m_generationLeaderProgress = 0.0f;
    std::size_t m_generationLeaderIndex = 0;
    std::uint32_t m_frontierTick = 0;
    std::vector<float> m_generationHistory;
    std::mt19937_64 m_rng;
    PlayerRuntime m_runtime1;
    PlayerRuntime m_runtime2;

    void initializePopulation();
    void breedNextGeneration();
    void resetRuntime();
    PolicyParams seededPolicy(std::size_t index) const;
    PolicyParams mutatePolicy(PolicyParams const& parent, std::size_t childIndex);
    bool decidePlayer(std::uint32_t tick, PlayerObservation const& obs, PolicyParams const& p, PlayerRuntime& rt, bool inheritedHeld);
    std::uint32_t countToggles(Candidate const& c) const;
    double score(AttemptResult const& result, std::uint32_t toggles) const;
};

const char* modeName(PlayerMode mode);

} // namespace omnipath
