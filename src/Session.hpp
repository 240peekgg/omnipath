#pragma once

#include <Geode/Geode.hpp>
#include "Evolution.hpp"
#include "WorldSim.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace omnipath::app {

struct PendingConfig {
    bool armed = false;
    std::string macroName = "omnipath-run";
    std::uint32_t candidates = 32;
    std::uint32_t maxGenerations = 250;
};

struct GhostVisual {
    CCSprite* p1 = nullptr;
    CCSprite* p2 = nullptr;
};

struct LiveSession {
    bool active = false;
    bool injectingInput = false;
    bool ignoreTouch = true;
    bool ghosts = true;
    bool verifying = false;
    int trainingSpeed = 2;
    int appliedSpeed = 1;
    float baseTimeScale = 1.0f;
    std::string macroName;
    std::unique_ptr<EvolutionEngine> evolution;
    WorldModel world;
    std::vector<SimAgent> agents;
    std::vector<GhostVisual> visuals;
    std::uint32_t tick = 0;
    std::uint32_t maxGenerations = 250;
    bool p1Down = false;
    bool p2Down = false;
    int deadTicks = 0;
    float proxyLastX = 0.0f;
    float lastGoodDx = 4.0f;
    std::size_t leaderIndex = 0;
    std::size_t aliveCount = 0;
    float verifyProgress = 0.0f;
    float verifyMaxX = 0.0f;
    float bestVerifiedProgress = 0.0f;
    std::uint32_t verificationGeneration = 0;
    Candidate verificationCandidate;
    Candidate verifiedCandidate;
    bool hasVerifiedCandidate = false;
    CCLayerColor* hudPanel = nullptr;
    CCLabelBMFont* hudTitle = nullptr;
    CCLabelBMFont* hudPhase = nullptr;
    CCLabelBMFont* hudStats = nullptr;
    CCLabelBMFont* hudHistory = nullptr;
};

extern PendingConfig g_pending;
extern LiveSession g_live;

std::string sanitizeName(std::string name);
std::string percentText(float value, int precision = 1);
const char* phaseName();
void setSpeedMultiplier(int multiplier);
void restoreTimeScale();
void releaseInputs(PlayLayer* layer);
void applyGene(PlayLayer* layer, Gene const& gene);
void setProxyVisible(PlayLayer* layer, bool visible);

void clearGhosts();
void ensureGhosts(PlayLayer* layer);
void updateGhosts(PlayLayer* layer);
void createHud(PlayLayer* layer);
void updateHud();

void saveCandidateMacro(Candidate const& candidate, bool verified, std::string const& suffix = "");
void saveBestMacro();
void resetAgents(PlayLayer* layer);
std::size_t chooseLeader();
std::vector<AttemptResult> collectResults();
std::size_t bestCompletedCandidate();
bool hasCompletedCandidate();
void endTraining(PlayLayer* layer, char const* message);
void startVerification(PlayLayer* layer, std::size_t winner);

} // namespace omnipath::app
