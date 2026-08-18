#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

#include "SolverCore.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>

using namespace geode::prelude;

namespace {

struct PendingConfig {
    bool armed = false;
    std::string macroName = "omnipath-run";
    omnipath::SearchMode mode = omnipath::SearchMode::Path;
    std::uint32_t decisionTicks = 1;
    std::uint32_t batchGenerations = 30;
};

PendingConfig g_pending;

struct LiveSession {
    bool active = false;
    std::string macroName;
    omnipath::SearchMode mode = omnipath::SearchMode::Path;
    std::unique_ptr<omnipath::EvolutionSolver> solver;
    std::uint32_t tick = 0;
    bool p1Down = false;
    bool p2Down = false;
    int deadTicks = 0;
    std::uint32_t batchGenerations = 30;
    float lastX = 0.0f;
    float lastY = 0.0f;
    float lastSavedBest = -1.0f;
    CCLabelBMFont* hudMain = nullptr;
    CCLabelBMFont* hudStats = nullptr;
    CCLabelBMFont* hudHistory = nullptr;
};

LiveSession g_live;

std::string sanitizeName(std::string name) {
    if (name.empty()) name = "omnipath-run";
    for (auto& ch : name) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_')) ch = '_';
    }
    return name;
}

void releaseInputs(PlayLayer* layer) {
    if (!layer) return;
    if (g_live.p1Down) layer->handleButton(false, 1, true);
    if (g_live.p2Down) layer->handleButton(false, 1, false);
    g_live.p1Down = false;
    g_live.p2Down = false;
}

void applyGene(PlayLayer* layer, omnipath::Gene const& gene) {
    if (gene.p1 != g_live.p1Down) {
        layer->handleButton(gene.p1, 1, true);
        g_live.p1Down = gene.p1;
    }
    if (gene.p2 != g_live.p2Down) {
        layer->handleButton(gene.p2, 1, false);
        g_live.p2Down = gene.p2;
    }
}

std::string percentText(float value, int precision = 2) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(precision) << value;
    return ss.str();
}

void saveBestMacro(bool force = false) {
    if (!g_live.solver || !g_live.solver->hasBest()) return;

    auto const& best = g_live.solver->best();
    if (!force && best.progress <= g_live.lastSavedBest + 0.001f) return;
    g_live.lastSavedBest = best.progress;

    auto dir = Mod::get()->getSaveDir() / "macros";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    auto path = dir / (sanitizeName(g_live.macroName) + ".omnipath.json");

    std::ofstream out(path);
    if (!out) {
        log::error("failed to save OmniPath macro to {}", path.string());
        return;
    }

    out << "{\n";
    out << "  \"format\": \"omnipath-v2\",\n";
    out << "  \"mode\": \"" << omnipath::modeName(g_live.mode) << "\",\n";
    out << "  \"batchGenerations\": " << g_live.batchGenerations << ",\n";
    out << "  \"decisionTicks\": " << g_live.solver->decisionEveryTicks() << ",\n";
    out << "  \"bestProgress\": " << best.progress << ",\n";
    out << "  \"events\": [\n";

    bool prev1 = false, prev2 = false;
    bool first = true;
    for (std::size_t i = 0; i < best.genes.size(); ++i) {
        auto const& g = best.genes[i];
        if (g.p1 == prev1 && g.p2 == prev2) continue;
        if (!first) out << ",\n";
        first = false;
        out << "    {\"tick\":" << (i * g_live.solver->decisionEveryTicks())
            << ",\"p1\":" << (g.p1 ? "true" : "false")
            << ",\"p2\":" << (g.p2 ? "true" : "false") << "}";
        prev1 = g.p1;
        prev2 = g.p2;
    }
    out << "\n  ],\n";

    out << "  \"deathMap\": [\n";
    bool firstDeath = true;
    for (auto const& d : g_live.solver->deathMap()) {
        if (!firstDeath) out << ",\n";
        firstDeath = false;
        out << "    {\"x\":" << d.x
            << ",\"y\":" << d.y
            << ",\"progress\":" << d.progress
            << ",\"tick\":" << d.tick
            << ",\"hits\":" << d.hits
            << ",\"action\":\"" << (d.recommendedHold ? "press" : "release")
            << "\",\"confidence\":" << d.confidence << "}";
    }
    out << "\n  ]\n}\n";
    log::info("saved OmniPath macro to {}", path.string());
}

void createHud(PlayLayer* layer) {
    if (!layer) return;

    auto win = CCDirector::sharedDirector()->getWinSize();

    g_live.hudMain = CCLabelBMFont::create("OmniPath", "bigFont.fnt");
    g_live.hudMain->setAnchorPoint({0.0f, 1.0f});
    g_live.hudMain->setScale(0.38f);
    g_live.hudMain->setPosition({7.0f, win.height - 7.0f});
    layer->addChild(g_live.hudMain, 100000);

    g_live.hudStats = CCLabelBMFont::create("starting...", "bigFont.fnt");
    g_live.hudStats->setAnchorPoint({0.0f, 1.0f});
    g_live.hudStats->setScale(0.30f);
    g_live.hudStats->setPosition({7.0f, win.height - 25.0f});
    layer->addChild(g_live.hudStats, 100000);

    g_live.hudHistory = CCLabelBMFont::create("gen best: -", "bigFont.fnt");
    g_live.hudHistory->setAnchorPoint({0.0f, 1.0f});
    g_live.hudHistory->setScale(0.25f);
    g_live.hudHistory->setPosition({7.0f, win.height - 40.0f});
    layer->addChild(g_live.hudHistory, 100000);
}

void updateHud(PlayLayer* layer, bool completed = false) {
    if (!g_live.solver || !g_live.hudMain || !g_live.hudStats || !g_live.hudHistory) return;

    auto gen = g_live.solver->generation();
    auto displayGen = std::min<std::size_t>(gen + 1, g_live.batchGenerations);
    auto candidate = g_live.solver->currentIndex() + 1;

    std::ostringstream main;
    main << "OmniPath " << omnipath::modeName(g_live.mode)
         << "  G " << displayGen << "/" << g_live.batchGenerations
         << "  C " << candidate << "/" << g_live.solver->populationSize();
    if (completed) main << "  DONE";
    g_live.hudMain->setString(main.str().c_str());

    auto bestProgress = g_live.solver->hasBest() ? g_live.solver->best().progress : 0.0f;
    auto currentProgress = layer ? layer->getCurrentPercent() : 0.0f;

    std::ostringstream stats;
    stats << "now " << percentText(currentProgress)
          << "%  best " << percentText(bestProgress)
          << "%  deaths " << g_live.solver->deathMap().size()
          << "  stuck " << g_live.solver->stagnantGenerations();
    g_live.hudStats->setString(stats.str().c_str());

    auto const& history = g_live.solver->generationHistory();
    std::ostringstream hist;
    hist << "gen best: ";
    if (history.empty()) {
        hist << "-";
    } else {
        auto begin = history.size() > 8 ? history.size() - 8 : 0;
        for (std::size_t i = begin; i < history.size(); ++i) {
            if (i != begin) hist << " | ";
            hist << static_cast<int>(std::round(history[i])) << "%";
        }
    }
    g_live.hudHistory->setString(hist.str().c_str());
}

class OmniPathPopup : public Popup {
protected:
    TextInput* m_nameInput = nullptr;
    CCLabelBMFont* m_modeLabel = nullptr;
    CCLabelBMFont* m_batchLabel = nullptr;
    LevelInfoLayer* m_owner = nullptr;
    omnipath::SearchMode m_mode = omnipath::SearchMode::Path;
    std::uint32_t m_batchGenerations = 30;

    void refreshMode() {
        if (m_modeLabel)
            m_modeLabel->setString(omnipath::modeName(m_mode));
    }

    void refreshBatch() {
        if (!m_batchLabel) return;
        auto text = std::to_string(m_batchGenerations) + " generations";
        m_batchLabel->setString(text.c_str());
    }

    bool init(LevelInfoLayer* owner) {
        if (!Popup::init(360.f, 285.f)) return false;
        m_owner = owner;
        m_mode = g_pending.mode;
        m_batchGenerations = std::clamp<std::uint32_t>(g_pending.batchGenerations, 10, 100);
        setTitle("OmniPath v0.2");

        auto nameTitle = CCLabelBMFont::create("macro name", "goldFont.fnt");
        nameTitle->setScale(.48f);
        nameTitle->setPosition({180.f, 220.f});
        m_mainLayer->addChild(nameTitle);

        m_nameInput = TextInput::create(230.f, "macro name", "bigFont.fnt");
        m_nameInput->setString(g_pending.macroName, false);
        m_nameInput->setPosition({180.f, 195.f});
        m_mainLayer->addChild(m_nameInput);

        auto modeTitle = CCLabelBMFont::create("search mode", "goldFont.fnt");
        modeTitle->setScale(.48f);
        modeTitle->setPosition({180.f, 157.f});
        m_mainLayer->addChild(modeTitle);

        m_modeLabel = CCLabelBMFont::create("path", "bigFont.fnt");
        m_modeLabel->setScale(.46f);
        m_modeLabel->setPosition({180.f, 134.f});
        m_mainLayer->addChild(m_modeLabel);
        refreshMode();

        auto modeSpr = ButtonSprite::create("switch mode", .58f);
        auto modeBtn = CCMenuItemSpriteExtra::create(
            modeSpr, this, menu_selector(OmniPathPopup::onSwitchMode)
        );
        modeBtn->setPosition({180.f, 108.f});
        m_buttonMenu->addChild(modeBtn);

        auto batchTitle = CCLabelBMFont::create("batch", "goldFont.fnt");
        batchTitle->setScale(.48f);
        batchTitle->setPosition({180.f, 79.f});
        m_mainLayer->addChild(batchTitle);

        m_batchLabel = CCLabelBMFont::create("30 generations", "bigFont.fnt");
        m_batchLabel->setScale(.40f);
        m_batchLabel->setPosition({180.f, 58.f});
        m_mainLayer->addChild(m_batchLabel);
        refreshBatch();

        auto minusSpr = ButtonSprite::create("-10", .58f);
        auto minusBtn = CCMenuItemSpriteExtra::create(
            minusSpr, this, menu_selector(OmniPathPopup::onBatchMinus)
        );
        minusBtn->setPosition({92.f, 57.f});
        m_buttonMenu->addChild(minusBtn);

        auto plusSpr = ButtonSprite::create("+10", .58f);
        auto plusBtn = CCMenuItemSpriteExtra::create(
            plusSpr, this, menu_selector(OmniPathPopup::onBatchPlus)
        );
        plusBtn->setPosition({268.f, 57.f});
        m_buttonMenu->addChild(plusBtn);

        auto startSpr = ButtonSprite::create("START BATCH", .62f);
        auto startBtn = CCMenuItemSpriteExtra::create(
            startSpr, this, menu_selector(OmniPathPopup::onStart)
        );
        startBtn->setPosition({180.f, 25.f});
        m_buttonMenu->addChild(startBtn);

        return true;
    }

    void onSwitchMode(CCObject*) {
        switch (m_mode) {
            case omnipath::SearchMode::Evolution:
                m_mode = omnipath::SearchMode::GuidedMutation;
                break;
            case omnipath::SearchMode::GuidedMutation:
                m_mode = omnipath::SearchMode::Path;
                break;
            case omnipath::SearchMode::Path:
                m_mode = omnipath::SearchMode::Evolution;
                break;
        }
        refreshMode();
    }

    void onBatchMinus(CCObject*) {
        if (m_batchGenerations > 10) m_batchGenerations -= 10;
        refreshBatch();
    }

    void onBatchPlus(CCObject*) {
        if (m_batchGenerations < 100) m_batchGenerations += 10;
        refreshBatch();
    }

    void onStart(CCObject*) {
        if (!m_owner || !m_owner->m_level) return;
        g_pending.armed = true;
        g_pending.macroName = sanitizeName(std::string(m_nameInput->getString()));
        g_pending.mode = m_mode;
        g_pending.batchGenerations = m_batchGenerations;
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
        auto popup = OmniPathPopup::create(this);
        popup->show();
    }
};

class $modify(OmniPathPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        if (!g_pending.armed) return true;
        g_pending.armed = false;

        if (m_isPlatformer) {
            FLAlertLayer::create(
                "OmniPath",
                "Platformer mode is not supported",
                "OK"
            )->show();
            return true;
        }

        omnipath::SolverConfig cfg;
        cfg.genomeLength = 12000;
        cfg.decisionEveryTicks = g_pending.decisionTicks;
        cfg.mode = g_pending.mode;

        // The three modes intentionally use different search pressure. This is
        // not just three names pointing at the same mutation loop anymore.
        switch (cfg.mode) {
            case omnipath::SearchMode::Evolution:
                cfg.population = 32;
                cfg.eliteCount = 6;
                cfg.mutationRate = 0.014;
                cfg.burstMutationRate = 0.45;
                break;
            case omnipath::SearchMode::GuidedMutation:
                cfg.population = 20;
                cfg.eliteCount = 4;
                cfg.mutationRate = 0.007;
                cfg.burstMutationRate = 0.30;
                break;
            case omnipath::SearchMode::Path:
                cfg.population = 16;
                cfg.eliteCount = 3;
                cfg.mutationRate = 0.004;
                cfg.burstMutationRate = 0.20;
                break;
        }

        g_live = {};
        g_live.active = true;
        g_live.macroName = g_pending.macroName;
        g_live.mode = g_pending.mode;
        g_live.batchGenerations = g_pending.batchGenerations;
        g_live.solver = std::make_unique<omnipath::EvolutionSolver>(cfg);

        if (m_player1) {
            g_live.lastX = m_player1->getPositionX();
            g_live.lastY = m_player1->getPositionY();
        }

        createHud(this);
        updateHud(this);

        log::info(
            "OmniPath started: name={}, mode={}, batch={}, population={}",
            g_live.macroName,
            omnipath::modeName(cfg.mode),
            g_live.batchGenerations,
            cfg.population
        );
        return true;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!g_live.active || !g_live.solver || !m_started) return;

        if (m_player1) {
            g_live.lastX = m_player1->getPositionX();
            g_live.lastY = m_player1->getPositionY();
        }

        if ((g_live.tick % 5) == 0)
            updateHud(this);

        if (getCurrentPercent() >= 99.999f) {
            omnipath::AttemptResult result;
            result.progress = 100.0f;
            result.survivedTicks = g_live.tick;
            result.deathX = g_live.lastX;
            result.deathY = g_live.lastY;
            result.died = false;
            g_live.solver->submit(result);

            saveBestMacro(true);
            releaseInputs(this);
            g_live.active = false;
            updateHud(this, true);
            Notification::create(
                "OmniPath: SOLVED - macro saved",
                static_cast<CCNode*>(nullptr),
                3.0f
            )->show();
            return;
        }

        if (m_playerDied) {
            ++g_live.deadTicks;
            if (g_live.deadTicks >= 2)
                this->resetLevel();
            return;
        }

        g_live.deadTicks = 0;
        applyGene(this, g_live.solver->geneForTick(g_live.tick));
        ++g_live.tick;
    }

    void resetLevel() {
        bool finishedBatch = false;

        if (g_live.active && g_live.solver && g_live.tick > 0) {
            auto oldBest = g_live.solver->hasBest() ? g_live.solver->best().progress : -1.0f;

            omnipath::AttemptResult result;
            result.progress = getCurrentPercent();
            result.survivedTicks = g_live.tick;
            result.deathX = g_live.lastX;
            result.deathY = g_live.lastY;
            result.died = m_playerDied;
            g_live.solver->submit(result);

            auto newBest = g_live.solver->hasBest() ? g_live.solver->best().progress : -1.0f;
            if (newBest > oldBest + 0.001f)
                saveBestMacro(false);

            log::debug(
                "OmniPath mode={} gen={} candidate={} progress={} best={} deaths={}",
                omnipath::modeName(g_live.mode),
                g_live.solver->generation(),
                g_live.solver->currentIndex(),
                result.progress,
                newBest,
                g_live.solver->deathMap().size()
            );

            if (g_live.solver->generation() >= g_live.batchGenerations) {
                finishedBatch = true;
                saveBestMacro(true);
                releaseInputs(this);
                g_live.active = false;
                updateHud(this, true);
                Notification::create(
                    "OmniPath: batch complete - best macro saved",
                    static_cast<CCNode*>(nullptr),
                    3.0f
                )->show();
            }
        }

        releaseInputs(this);
        g_live.tick = 0;
        g_live.deadTicks = 0;
        PlayLayer::resetLevel();

        if (finishedBatch) {
            // Keep the final HUD visible after reset so the 10-100 generation
            // batch result can be inspected before leaving the level.
            updateHud(this, true);
        }
    }

    void onQuit() {
        if (g_live.active) {
            saveBestMacro(true);
            g_live.active = false;
        }
        PlayLayer::onQuit();
    }
};
