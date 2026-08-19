        auto win = CCDirector::sharedDirector()->getWinSize();
        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->setID("omnipath-pause-menu");
        addChild(menu, 10000);

        auto title = CCLabelBMFont::create("OmniPath Parallel", "goldFont.fnt");
        title->setScale(.38f);
        title->setPosition({win.width * .5f, 54.f});
        addChild(title, 10000);

        auto speedText = std::string("speed x") + std::to_string(g_live.trainingSpeed);
        auto speed = CCMenuItemSpriteExtra::create(
            ButtonSprite::create(speedText.c_str(), .48f), this, menu_selector(OmniPathPauseLayer::onSpeed)
        );
        speed->setPosition({win.width * .5f - 105.f, 28.f});
        menu->addChild(speed);

        auto touch = CCMenuItemSpriteExtra::create(
            ButtonSprite::create(g_live.ignoreTouch ? "touch LOCK" : "touch ON", .48f),
            this, menu_selector(OmniPathPauseLayer::onTouch)
        );
        touch->setPosition({win.width * .5f, 28.f});
        menu->addChild(touch);

        auto ghosts = CCMenuItemSpriteExtra::create(
            ButtonSprite::create(g_live.ghosts ? "ghosts ON" : "ghosts OFF", .48f),
            this, menu_selector(OmniPathPauseLayer::onGhosts)
        );
        ghosts->setPosition({win.width * .5f + 105.f, 28.f});
        menu->addChild(ghosts);
    }

    void updateButton(CCObject* sender, std::string const& text) {
        auto item = static_cast<CCMenuItemSpriteExtra*>(sender);
        if (!item) return;
        auto spr = static_cast<ButtonSprite*>(item->getNormalImage());
        if (!spr) return;
        spr->setString(text.c_str());
        item->updateSprite();
    }

    void onSpeed(CCObject* sender) {
        switch (g_live.trainingSpeed) {
            case 1: g_live.trainingSpeed = 2; break;
            case 2: g_live.trainingSpeed = 4; break;
            default: g_live.trainingSpeed = 1; break;
        }
        updateButton(sender, std::string("speed x") + std::to_string(g_live.trainingSpeed));
    }

    void onTouch(CCObject* sender) {
        g_live.ignoreTouch = !g_live.ignoreTouch;
        updateButton(sender, g_live.ignoreTouch ? "touch LOCK" : "touch ON");
    }

    void onGhosts(CCObject* sender) {
        g_live.ghosts = !g_live.ghosts;
        updateGhostSprites();
        updateButton(sender, g_live.ghosts ? "ghosts ON" : "ghosts OFF");
    }
};

class $modify(OmniPathPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        if (!g_pending.armed) return true;
        g_pending.armed = false;

        if (m_isPlatformer) {
            FLAlertLayer::create("OmniPath", "Platformer mode is not supported yet", "OK")->show();
            return true;
        }

        omnipath::SolverConfig cfg;
        cfg.population = g_pending.candidates;
        cfg.eliteCount = std::max<std::size_t>(3, cfg.population / 6);
        cfg.genomeLength = 24000;
        cfg.decisionEveryTicks = 1;

        g_live = {};
        g_live.active = true;
        g_live.ignoreTouch = true;
        g_live.ghosts = true;
        g_live.trainingSpeed = 2;
        g_live.appliedSpeed = 1;
        g_live.macroName = g_pending.macroName;
        g_live.maxGenerations = g_pending.maxGenerations;
        g_live.solver = std::make_unique<omnipath::ParallelEvolution>(cfg);
        g_live.agents.resize(cfg.population);

        auto scheduler = CCDirector::sharedDirector()->getScheduler();
        g_live.baseTimeScale = scheduler ? scheduler->getTimeScale() : 1.0f;

        buildWorldIndex(this);
        resetParallelAgents(this);
        releaseInputs(this);
        createHud(this);
        updateHud(this);
        setSpeedMultiplier(g_live.trainingSpeed);

        log::info(
            "OmniPath PARALLEL started: name={}, population={}, maxGenerations={}",
            g_live.macroName,
            cfg.population,
            g_live.maxGenerations
        );
        return true;
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (g_live.active && !g_live.verifying && (player == m_player1 || player == m_player2))
            return;
        PlayLayer::destroyPlayer(player, object);
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!g_live.active || !g_live.solver || !m_started) return;

        if (g_live.verifying) {
            setSpeedMultiplier(1);
            g_live.verifyProgress = std::max(g_live.verifyProgress, getCurrentPercent());
            if (m_player1) g_live.verifyMaxX = std::max(g_live.verifyMaxX, m_player1->getPositionX());

            if (getCurrentPercent() >= 99.999f) {
                g_live.verifiedCandidate = g_live.verificationCandidate;
                g_live.verifiedCandidate.progress = 100.0f;
                g_live.verifiedCandidate.maxX = g_live.verifyMaxX;
                g_live.hasVerifiedCandidate = true;
                g_live.bestVerifiedProgress = 100.0f;
                saveCandidateMacro(g_live.verifiedCandidate);
                endTraining(this, "OmniPath: SOLVED + VERIFIED");
                return;
            }

            if (m_playerDied) {
                ++g_live.deadTicks;
                g_live.bestVerifiedProgress = std::max(g_live.bestVerifiedProgress, g_live.verifyProgress);
                if (g_live.deadTicks >= 2) {
                    log::info("OmniPath verification failed at {}% - returning to parallel training", g_live.verifyProgress);
                    g_live.verifying = false;
                    g_live.deadTicks = 0;
                    releaseInputs(this);
                    this->resetLevel();
                }
                return;
            }

            g_live.deadTicks = 0;
            auto geneIndex = static_cast<std::size_t>(g_live.tick / g_live.solver->decisionEveryTicks());
            omnipath::Gene gene{};
            if (geneIndex < g_live.verificationCandidate.genes.size())
                gene = g_live.verificationCandidate.genes[geneIndex];
            applyGene(this, gene);
            ++g_live.tick;
            if ((g_live.tick % 5) == 0) updateHud(this);
            return;
        }

        int effectiveSpeed = g_live.trainingSpeed;
        if (g_live.aliveCount <= std::max<std::size_t>(2, g_live.solver->populationSize() / 8))
            effectiveSpeed = std::min(effectiveSpeed, 2);
        setSpeedMultiplier(effectiveSpeed);

        float rawDx = 0.0f;
        if (m_player1) rawDx = m_player1->getPositionX() - g_live.proxyLastX;
        if (rawDx > 0.05f && rawDx < 80.0f) g_live.lastGoodDx = rawDx;
        float dx = (rawDx > 0.05f && rawDx < 80.0f) ? rawDx : g_live.lastGoodDx;
        float percent = getCurrentPercent();

        g_live.aliveCount = 0;
        std::vector<omnipath::Gene> frameGenes(g_live.agents.size());
        for (std::size_t i = 0; i < g_live.agents.size(); ++i) {
            auto& agent = g_live.agents[i];
            if (!agent.alive) continue;

            auto observation = observeAgent(agent);
            auto gene = g_live.solver->decide(i, g_live.tick, observation);
            frameGenes[i] = gene;

            simulatePlayer(agent.p1, gene.p1, dx, dt);
            if (agent.dual) simulatePlayer(agent.p2, gene.p2, dx, dt);

            agent.maxX = std::max(agent.maxX, agent.p1.x);
            agent.progress = std::max(agent.progress, percent);
            agent.survivedTicks = g_live.tick + 1;
            agent.alive = agent.p1.alive && (!agent.dual || agent.p2.alive);

            if (!agent.alive) {
                agent.deathMode = !agent.p1.alive ? agent.p1.mode : agent.p2.mode;
            } else {
                ++g_live.aliveCount;
            }
        }

        if (percent >= 99.75f && g_live.aliveCount > 0) {
            auto winner = bestAliveCandidate();
            startVerification(this, winner);
            return;
        }

        if (g_live.aliveCount == 0 || g_live.tick + 2 >= 24000) {
            auto results = collectGenerationResults();
            auto finishedGeneration = g_live.solver->generation() + 1;
            g_live.solver->submitGeneration(results);

            log::info(
                "OmniPath parallel generation {} done: best={}%, stagnation={}",
                finishedGeneration,
                g_live.solver->generationHistory().empty() ? 0.0f : g_live.solver->generationHistory().back(),
                g_live.solver->stagnantGenerations()
            );

            if (g_live.solver->generation() >= g_live.maxGenerations) {
                endTraining(this, "OmniPath: generation limit reached");
                return;
            }

            releaseInputs(this);
            this->resetLevel();
            return;
        }

        g_live.leaderIndex = chooseLeader();
        auto& leader = g_live.agents[g_live.leaderIndex];
        applyGene(this, frameGenes[g_live.leaderIndex]);

        if (m_player1) {
            m_player1->setPosition({leader.p1.x, leader.p1.y});
            m_player1->m_yVelocity = leader.p1.vy;
            g_live.proxyLastX = leader.p1.x;
        }
        if (leader.dual && m_player2) {
            m_player2->setPosition({leader.p2.x, leader.p2.y});
            m_player2->m_yVelocity = leader.p2.vy;
        }

        updateGhostSprites();
        ++g_live.tick;
        if ((g_live.tick % 4) == 0) updateHud(this);
    }

    void resetLevel() {
        bool wasActive = g_live.active;
        bool verify = g_live.verifying;
        if (wasActive) releaseInputs(this);

        PlayLayer::resetLevel();

        if (!wasActive || !g_live.active) return;
        g_live.tick = 0;
        g_live.deadTicks = 0;
        if (m_player1) {
            g_live.proxyLastX = m_player1->getPositionX();
            g_live.verifyMaxX = m_player1->getPositionX();
        }

        if (verify) {
            for (auto& agent : g_live.agents) {
                if (agent.ghost1) agent.ghost1->setVisible(false);
                if (agent.ghost2) agent.ghost2->setVisible(false);
            }
            g_live.verifyProgress = 0.0f;
            setSpeedMultiplier(1);
        } else {
            resetParallelAgents(this);
            setSpeedMultiplier(g_live.trainingSpeed);
        }
        updateHud(this);
    }

    void onQuit() {
        if (g_live.active) {
            saveBestMacro(true);
            releaseInputs(this);
            restoreTimeScale();
            clearGhosts();
            g_live.active = false;
        }
        PlayLayer::onQuit();
    }
};
