#include "Session.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <iomanip>

namespace omnipath::app {

PendingConfig g_pending;
LiveSession g_live;

std::string sanitizeName(std::string name) {
    if (name.empty()) name = "omnipath-run";
    for (auto& ch : name) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_'))
            ch = '_';
    }
    return name;
}

std::string percentText(float value, int precision = 1) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

const char* phaseName() {
    if (g_live.verifying) return "VERIFY / REAL PHYSICS";
    if (!g_live.evolution) return "IDLE";
    return g_live.evolution->explorationGeneration()
        ? "EXPLORE / RANDOM INPUTS"
        : "EVOLVE / CROSSOVER + MUTATION";
}

void setSpeedMultiplier(int multiplier) {
    if (!g_live.active) return;
    multiplier = std::clamp(multiplier, 1, 4);
    if (g_live.appliedSpeed == multiplier) return;
    auto scheduler = CCDirector::sharedDirector()->getScheduler();
    if (!scheduler) return;
    scheduler->setTimeScale(g_live.baseTimeScale * static_cast<float>(multiplier));
    g_live.appliedSpeed = multiplier;
}

void restoreTimeScale() {
    auto scheduler = CCDirector::sharedDirector()->getScheduler();
    if (scheduler) scheduler->setTimeScale(g_live.baseTimeScale);
    g_live.appliedSpeed = 1;
}

void releaseInputs(PlayLayer* layer) {
    if (!layer) return;
    g_live.injectingInput = true;
    if (g_live.p1Down) layer->handleButton(false, 1, true);
    if (g_live.p2Down) layer->handleButton(false, 1, false);
    g_live.injectingInput = false;
    g_live.p1Down = false;
    g_live.p2Down = false;
}

void applyGene(PlayLayer* layer, omnipath::Gene const& gene) {
    if (!layer) return;
    g_live.injectingInput = true;
    if (gene.p1 != g_live.p1Down) {
        layer->handleButton(gene.p1, 1, true);
        g_live.p1Down = gene.p1;
    }
    if (gene.p2 != g_live.p2Down) {
        layer->handleButton(gene.p2, 1, false);
        g_live.p2Down = gene.p2;
    }
    g_live.injectingInput = false;
}

void setProxyVisible(PlayLayer* layer, bool visible) {
    if (!layer) return;
    if (layer->m_player1) layer->m_player1->setVisible(visible);
    if (layer->m_player2) layer->m_player2->setVisible(visible);
}

void resetAgents(PlayLayer* layer) {
    if (!layer || !g_live.evolution) return;
    g_live.world.rebuild(layer);

    if (g_live.agents.size() != g_live.evolution->populationSize())
        g_live.agents.resize(g_live.evolution->populationSize());
    for (auto& agent : g_live.agents)
        g_live.world.resetAgent(agent);

    g_live.tick = 0;
    g_live.deadTicks = 0;
    g_live.aliveCount = g_live.agents.size();
    g_live.leaderIndex = 0;
    g_live.proxyLastX = layer->m_player1 ? layer->m_player1->getPositionX() : g_live.world.startX();
    g_live.lastGoodDx = 4.0f;
    ensureGhosts(layer);
    updateGhosts(layer);
}

float leaderScore(omnipath::SimAgent const& agent) {
    if (!agent.alive) return -std::numeric_limits<float>::infinity();
    return agent.progress * 10000.0f + agent.maxX + static_cast<float>(agent.survivedTicks) * 0.01f;
}

std::size_t chooseLeader() {
    std::size_t best = 0;
    float bestScore = -std::numeric_limits<float>::infinity();
    for (std::size_t i = 0; i < g_live.agents.size(); ++i) {
        auto score = leaderScore(g_live.agents[i]);
        if (score > bestScore) {
            bestScore = score;
            best = i;
        }
    }
    return best;
}

std::vector<omnipath::AttemptResult> collectResults() {
    std::vector<omnipath::AttemptResult> results;
    results.reserve(g_live.agents.size());
    for (auto const& agent : g_live.agents) {
        omnipath::AttemptResult result;
        result.progress = agent.progress;
        result.maxX = agent.maxX;
        result.survivedTicks = agent.survivedTicks;
        result.completed = agent.completed;
        results.push_back(result);
    }
    return results;
}

std::size_t bestCompletedCandidate() {
    std::size_t winner = 0;
    float score = -1.0f;
    for (std::size_t i = 0; i < g_live.agents.size(); ++i) {
        auto const& agent = g_live.agents[i];
        if (!agent.completed) continue;
        auto value = agent.progress * 1000.0f + agent.maxX;
        if (value > score) {
            score = value;
            winner = i;
        }
    }
    return winner;
}

bool hasCompletedCandidate() {
    for (auto const& agent : g_live.agents)
        if (agent.completed) return true;
    return false;
}

void endTraining(PlayLayer* layer, char const* message) {
    saveBestMacro();
    releaseInputs(layer);
    restoreTimeScale();
    setProxyVisible(layer, true);
    clearGhosts();
    g_live.active = false;
    if (message)
        Notification::create(message, static_cast<CCNode*>(nullptr), 3.0f)->show();
}

void startVerification(PlayLayer* layer, std::size_t winner) {
    if (!layer || !g_live.evolution || winner >= g_live.evolution->populationSize()) return;

    g_live.verificationGeneration = static_cast<std::uint32_t>(g_live.evolution->generation() + 1);
    g_live.verificationCandidate = g_live.evolution->candidate(winner);
    auto results = collectResults();
    g_live.evolution->submitGeneration(results);

    g_live.verifying = true;
    g_live.tick = 0;
    g_live.deadTicks = 0;
    g_live.verifyProgress = 0.0f;
    g_live.verifyMaxX = g_live.world.startX();
    releaseInputs(layer);
    setSpeedMultiplier(1);
    layer->resetLevel();
}


} // namespace omnipath::app
