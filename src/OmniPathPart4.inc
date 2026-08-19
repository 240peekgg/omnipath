}

void saveBestMacro(bool force = false) {
    (void)force;
    if (g_live.hasVerifiedCandidate) {
        saveCandidateMacro(g_live.verifiedCandidate);
        return;
    }
    if (g_live.solver && g_live.solver->hasBest())
        saveCandidateMacro(g_live.solver->best(), "-simulated");
}

void endTraining(PlayLayer* layer, char const* message) {
    saveBestMacro(true);
    releaseInputs(layer);
    restoreTimeScale();
    g_live.active = false;
    clearGhosts();
    updateHud(layer);
    if (message)
        Notification::create(message, static_cast<CCNode*>(nullptr), 3.0f)->show();
}

void buildWorldIndex(PlayLayer* layer) {
    g_live.worldObjects.clear();
    if (!layer || !layer->m_objects) return;

    g_live.worldObjects.reserve(layer->m_objects->count());
    for (auto obj : geode::cocos::CCArrayExt<GameObject, false>(layer->m_objects)) {
        if (!obj || !relevantObject(obj->getType())) continue;
        g_live.worldObjects.push_back({obj->getPositionX(), obj});
    }

    std::sort(g_live.worldObjects.begin(), g_live.worldObjects.end(), [](IndexedObject const& a, IndexedObject const& b) {
        return a.initialX < b.initialX;
    });
}

void resetParallelAgents(PlayLayer* layer) {
    if (!layer || !g_live.solver) return;

    auto p1 = fromRealPlayer(layer->m_player1);
    bool dual = layer->m_player2 && !layer->m_player2->m_isHidden && layer->m_player2->isVisible();
    auto p2 = dual ? fromRealPlayer(layer->m_player2) : SimPlayer{};
    if (!dual) {
        p2.active = false;
        p2.alive = false;
    }

    g_live.baseGroundY = p1.y;
    g_live.proxyLastX = p1.x;
    g_live.lastGoodDx = 4.0f;
    g_live.tick = 0;
    g_live.deadTicks = 0;
    g_live.aliveCount = g_live.solver->populationSize();
    g_live.leaderIndex = 0;
    g_live.verifyProgress = 0.0f;
    g_live.verifyMaxX = p1.x;

    if (g_live.agents.size() != g_live.solver->populationSize())
        g_live.agents.resize(g_live.solver->populationSize());

    for (auto& agent : g_live.agents) {
        agent.p1 = p1;
        agent.p2 = p2;
        agent.dual = dual;
        agent.alive = true;
        agent.maxX = p1.x;
        agent.progress = 0.0f;
        agent.survivedTicks = 0;
        agent.deathMode = omnipath::PlayerMode::Unknown;
    }

    ensureGhostSprites(layer, dual);
    updateGhostSprites();
}

float leaderQuality(SimAgent const& agent) {
    if (!agent.alive) return -std::numeric_limits<float>::infinity();
    auto obs = observeSimPlayer(agent.p1);
    float quality = agent.progress * 1000.0f;
    if (obs.hasTarget) quality -= std::abs(agent.p1.y - obs.targetY) * 0.08f;
    if (obs.hazardAhead) quality += std::min(obs.nearestHazardDx, 120.0f) * 0.015f;
    return quality;
}

std::size_t chooseLeader() {
    std::size_t best = 0;
    float bestScore = -std::numeric_limits<float>::infinity();
    for (std::size_t i = 0; i < g_live.agents.size(); ++i) {
        auto score = leaderQuality(g_live.agents[i]);
        if (score > bestScore) {
            bestScore = score;
            best = i;
        }
    }
    return best;
}

std::vector<omnipath::AttemptResult> collectGenerationResults() {
    std::vector<omnipath::AttemptResult> results;
    results.reserve(g_live.agents.size());
    for (auto const& agent : g_live.agents) {
        omnipath::AttemptResult result;
        result.progress = agent.progress;
        result.maxX = agent.maxX;
        result.deathX = agent.p1.x;
        result.deathY = agent.p1.y;
        result.survivedTicks = agent.survivedTicks;
        result.deathMode = agent.deathMode;
        result.died = true;
        results.push_back(result);
    }
    return results;
}

std::size_t bestAliveCandidate() {
    std::size_t best = 0;
    float bestScore = -1.0f;
    for (std::size_t i = 0; i < g_live.agents.size(); ++i) {
        if (!g_live.agents[i].alive) continue;
        float score = g_live.agents[i].progress * 1000.0f + g_live.agents[i].maxX;
        if (score > bestScore) {
            bestScore = score;
            best = i;
        }
    }
    return best;
}

void startVerification(PlayLayer* layer, std::size_t winner) {
    if (!layer || !g_live.solver || winner >= g_live.solver->populationSize()) return;

    g_live.verificationCandidate = g_live.solver->candidate(winner);
    auto results = collectGenerationResults();
    results[winner].progress = 99.9f;
    results[winner].died = true;
    g_live.solver->submitGeneration(results);

    g_live.verifying = true;
    g_live.tick = 0;
    g_live.deadTicks = 0;
    g_live.verifyProgress = 0.0f;
    releaseInputs(layer);
    setSpeedMultiplier(1);
    layer->resetLevel();
}

class OmniPathPopup : public Popup {
protected:
    TextInput* m_nameInput = nullptr;
    CCLabelBMFont* m_candidatesLabel = nullptr;
    LevelInfoLayer* m_owner = nullptr;
    std::uint32_t m_candidates = 32;

    void refreshCandidates() {
        if (!m_candidatesLabel) return;
        auto text = std::to_string(m_candidates) + " parallel ghosts";
        m_candidatesLabel->setString(text.c_str());
    }

    bool init(LevelInfoLayer* owner) {
        if (!Popup::init(360.f, 260.f)) return false;
        m_owner = owner;
        m_candidates = std::clamp<std::uint32_t>(g_pending.candidates, 8, 100);
        setTitle("OmniPath v0.4 PARALLEL");

        auto subtitle = CCLabelBMFont::create("one generation = many independent players", "goldFont.fnt");
        subtitle->setScale(.31f);
        subtitle->setPosition({180.f, 207.f});
        m_mainLayer->addChild(subtitle);

        auto nameTitle = CCLabelBMFont::create("macro name", "goldFont.fnt");
        nameTitle->setScale(.46f);
        nameTitle->setPosition({180.f, 176.f});
        m_mainLayer->addChild(nameTitle);

        m_nameInput = TextInput::create(230.f, "macro name", "bigFont.fnt");
        m_nameInput->setString(g_pending.macroName, false);
        m_nameInput->setPosition({180.f, 150.f});
        m_mainLayer->addChild(m_nameInput);

        auto popTitle = CCLabelBMFont::create("population", "goldFont.fnt");
        popTitle->setScale(.46f);
        popTitle->setPosition({180.f, 111.f});
        m_mainLayer->addChild(popTitle);

        m_candidatesLabel = CCLabelBMFont::create("32 parallel ghosts", "bigFont.fnt");
        m_candidatesLabel->setScale(.40f);
        m_candidatesLabel->setPosition({180.f, 87.f});
        m_mainLayer->addChild(m_candidatesLabel);
        refreshCandidates();

        auto minus = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("-8", .58f), this, menu_selector(OmniPathPopup::onMinus)
        );
        minus->setPosition({92.f, 84.f});
        m_buttonMenu->addChild(minus);

        auto plus = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("+8", .58f), this, menu_selector(OmniPathPopup::onPlus)
        );
        plus->setPosition({268.f, 84.f});
        m_buttonMenu->addChild(plus);

        auto hint = CCLabelBMFont::create("winner is replayed on real GD physics", "bigFont.fnt");
        hint->setScale(.26f);
        hint->setPosition({180.f, 53.f});
        m_mainLayer->addChild(hint);

        auto start = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("START PARALLEL", .62f), this, menu_selector(OmniPathPopup::onStart)
        );
        start->setPosition({180.f, 25.f});
        m_buttonMenu->addChild(start);
        return true;
    }

    void onMinus(CCObject*) {
        if (m_candidates > 8) m_candidates = std::max<std::uint32_t>(8, m_candidates - 8);
        refreshCandidates();
    }

    void onPlus(CCObject*) {
        if (m_candidates < 100) m_candidates = std::min<std::uint32_t>(100, m_candidates + 8);
        refreshCandidates();
    }

    void onStart(CCObject*) {
        if (!m_owner || !m_owner->m_level) return;
        g_pending.armed = true;
        g_pending.macroName = sanitizeName(std::string(m_nameInput->getString()));
        g_pending.candidates = m_candidates;
        this->onClose(nullptr);
        m_owner->onPlay(nullptr);
    }

public:
    static OmniPathPopup* create(LevelInfoLayer* owner) {
        auto ret = new OmniPathPopup;
        if (ret->init(owner)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

} // namespace

class $modify(OmniPathBaseGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        if (g_live.active && g_live.ignoreTouch && !g_live.injectingInput && button == 1)
            return;
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};

class $modify(OmniPathLevelInfoLayer, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;

        auto spr = ButtonSprite::create("AI", .70f);
        auto btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(OmniPathLevelInfoLayer::onOmniPath)
        );
        btn->setID("omnipath-button");
        if (m_playBtnMenu) {
            btn->setPosition({68.f, 0.f});
            m_playBtnMenu->addChild(btn);
        }
        return true;
    }

    void onOmniPath(CCObject*) {
        OmniPathPopup::create(this)->show();
    }
};

class $modify(OmniPathPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        if (!g_live.active) return;

