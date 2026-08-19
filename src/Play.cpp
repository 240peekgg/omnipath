#include <Geode/modify/PlayLayer.hpp>

#include "Session.hpp"

#include <algorithm>
#include <memory>
#include <vector>

using namespace geode::prelude;
using namespace omnipath::app;

class $modify(OmniPathPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        if (!g_pending.armed) return true;
        g_pending.armed = false;

        if (m_isPlatformer) {
            FLAlertLayer::create("OmniPath", "Platformer mode is not supported yet", "OK")->show();
            return true;
        }

        omnipath::EvolutionConfig config;
        config.population = g_pending.candidates;
        config.eliteCount = std::max<std::size_t>(3, config.population / 6);
        config.genomeLength = 24000;
        config.decisionEveryTicks = 1;
        config.globalMutationRate = 0.0012f;
        config.frontierMutationRate = 0.045f;
        config.frontierWindow = 220;

        g_live = {};
        g_live.active = true;
        g_live.ignoreTouch = true;
        g_live.ghosts = true;
        g_live.trainingSpeed = 2;
        g_live.appliedSpeed = 1;
        g_live.macroName = g_pending.macroName;
        g_live.maxGenerations = g_pending.maxGenerations;
        g_live.evolution = std::make_unique<omnipath::EvolutionEngine>(config);
        g_live.agents.resize(config.population);
        g_live.visuals.resize(config.population);

        auto scheduler = CCDirector::sharedDirector()->getScheduler();
        g_live.baseTimeScale = scheduler ? scheduler->getTimeScale() : 1.0f;

        g_live.world.rebuild(this);
        resetAgents(this);
        releaseInputs(this);
        createHud(this);
        setProxyVisible(this, false);
        setSpeedMultiplier(g_live.trainingSpeed);
        updateHud();

        log::info(
            "OmniPath 0.5 started: population={}, generations={}, seed={}, worldObjects={}, hazards={}, solids={}, startX={}, finishX={}",
            config.population,
            g_live.maxGenerations,
            g_live.evolution->seed(),
            g_live.world.objectCount(),
            g_live.world.hazardCount(),
            g_live.world.solidCount(),
            g_live.world.startX(),
            g_live.world.finishX()
        );
        return true;
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        // Training uses the real player only as an invisible camera/speed proxy.
        // Visible shadow agents have their own strict collision deaths.
        if (g_live.active && !g_live.verifying && (player == m_player1 || player == m_player2))
            return;
        PlayLayer::destroyPlayer(player, object);
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!g_live.active || !g_live.evolution || !m_started) return;

        if (g_live.verifying) {
            setProxyVisible(this, true);
            setSpeedMultiplier(1);
            g_live.verifyProgress = std::max(g_live.verifyProgress, getCurrentPercent());
            if (m_player1)
                g_live.verifyMaxX = std::max(g_live.verifyMaxX, m_player1->getPositionX());

            if (getCurrentPercent() >= 99.999f) {
                g_live.verifiedCandidate = g_live.verificationCandidate;
                g_live.verifiedCandidate.progress = 100.0f;
                g_live.verifiedCandidate.maxX = g_live.verifyMaxX;
                g_live.verifiedCandidate.completed = true;
                g_live.hasVerifiedCandidate = true;
                g_live.bestVerifiedProgress = 100.0f;
                saveCandidateMacro(g_live.verifiedCandidate, true);
                endTraining(this, "OmniPath: SOLVED + VERIFIED");
                return;
            }

            if (m_playerDied) {
                ++g_live.deadTicks;
                g_live.bestVerifiedProgress = std::max(g_live.bestVerifiedProgress, g_live.verifyProgress);
                if (g_live.deadTicks >= 2) {
                    log::info(
                        "OmniPath verification failed at {}% - continuing evolution",
                        g_live.verifyProgress
                    );
                    g_live.verifying = false;
                    g_live.deadTicks = 0;
                    releaseInputs(this);
                    this->resetLevel();
                }
                return;
            }

            g_live.deadTicks = 0;
            auto index = static_cast<std::size_t>(
                g_live.tick / g_live.evolution->decisionEveryTicks()
            );
            omnipath::Gene gene{};
            if (index < g_live.verificationCandidate.genes.size())
                gene = g_live.verificationCandidate.genes[index];
            applyGene(this, gene);
            ++g_live.tick;
            if ((g_live.tick % 4) == 0) updateHud();
            return;
        }

        setProxyVisible(this, false);

        int effectiveSpeed = g_live.trainingSpeed;
        if (g_live.aliveCount <= std::max<std::size_t>(2, g_live.evolution->populationSize() / 10))
            effectiveSpeed = std::min(effectiveSpeed, 2);
        setSpeedMultiplier(effectiveSpeed);

        float rawDx = 0.0f;
        if (m_player1) rawDx = m_player1->getPositionX() - g_live.proxyLastX;
        if (rawDx > 0.05f && rawDx < 90.0f) g_live.lastGoodDx = rawDx;
        float dx = (rawDx > 0.05f && rawDx < 90.0f) ? rawDx : g_live.lastGoodDx;

        g_live.aliveCount = 0;
        for (std::size_t i = 0; i < g_live.agents.size(); ++i) {
            auto& agent = g_live.agents[i];
            if (!agent.alive || agent.completed) continue;

            auto gene = g_live.evolution->decide(i, g_live.tick);
            g_live.world.stepAgent(agent, gene, dx, dt);
            if (agent.alive) ++g_live.aliveCount;
        }

        if (hasCompletedCandidate()) {
            auto winner = bestCompletedCandidate();
            startVerification(this, winner);
            return;
        }

        if (g_live.aliveCount == 0 || g_live.tick + 2 >= configGenomeLimit()) {
            auto results = collectResults();
            auto closedGeneration = g_live.evolution->generation() + 1;
            g_live.evolution->submitGeneration(results);

            auto generationBest = g_live.evolution->generationHistory().empty()
                ? 0.0f
                : g_live.evolution->generationHistory().back();
            log::info(
                "OmniPath generation {} complete: best={}%, stagnant={}, nextPhase={}",
                closedGeneration,
                generationBest,
                g_live.evolution->stagnantGenerations(),
                g_live.evolution->explorationGeneration() ? "explore" : "evolve"
            );

            if (g_live.evolution->generation() >= g_live.maxGenerations) {
                endTraining(this, "OmniPath: generation limit reached");
                return;
            }

            releaseInputs(this);
            this->resetLevel();
            return;
        }

        g_live.leaderIndex = chooseLeader();
        auto& leader = g_live.agents[g_live.leaderIndex];
        applyGene(this, leader.lastGene);

        // The proxy remains invisible and is capped before the real end portal so training
        // can never visually or logically complete through noclip. Only verification may finish.
        if (m_player1) {
            auto proxyX = std::min(leader.p1.x, g_live.world.finishX() - 90.0f);
            m_player1->setPosition({proxyX, leader.p1.y});
            m_player1->m_yVelocity = leader.p1.vy;
            g_live.proxyLastX = proxyX;
            m_player1->setVisible(false);
        }
        if (leader.dual && m_player2) {
            auto proxyX = std::min(leader.p2.x, g_live.world.finishX() - 90.0f);
            m_player2->setPosition({proxyX, leader.p2.y});
            m_player2->m_yVelocity = leader.p2.vy;
            m_player2->setVisible(false);
        }

        updateGhosts(this);
        ++g_live.tick;
        if ((g_live.tick % 4) == 0) updateHud();
    }

    std::uint32_t configGenomeLimit() const {
        return 24000;
    }

    void resetLevel() {
        bool wasActive = g_live.active;
        bool verify = g_live.verifying;
        if (wasActive) releaseInputs(this);

        PlayLayer::resetLevel();

        if (!wasActive || !g_live.active) return;
        g_live.tick = 0;
        g_live.deadTicks = 0;
        g_live.proxyLastX = m_player1 ? m_player1->getPositionX() : g_live.world.startX();
        g_live.lastGoodDx = 4.0f;

        if (verify) {
            setProxyVisible(this, true);
            for (auto& visual : g_live.visuals) {
                if (visual.p1) visual.p1->setVisible(false);
                if (visual.p2) visual.p2->setVisible(false);
            }
            g_live.verifyProgress = 0.0f;
            g_live.verifyMaxX = m_player1 ? m_player1->getPositionX() : g_live.world.startX();
            setSpeedMultiplier(1);
        } else {
            resetAgents(this);
            setProxyVisible(this, false);
            setSpeedMultiplier(g_live.trainingSpeed);
        }
        updateHud();
    }

    void onQuit() {
        if (g_live.active) {
            saveBestMacro();
            releaseInputs(this);
            restoreTimeScale();
            setProxyVisible(this, true);
            clearGhosts();
            g_live.active = false;
        }
        PlayLayer::onQuit();
    }
};
