#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

#include "SolverCore.hpp"

#include <cctype>
#include <fstream>
#include <memory>
#include <string>

using namespace geode::prelude;

namespace {

struct PendingConfig {
    bool armed = false;
    std::string macroName = "omnipath-run";
    omnipath::SearchMode mode = omnipath::SearchMode::GuidedMutation;
    std::uint32_t decisionTicks = 2;
};

PendingConfig g_pending;

struct LiveSession {
    bool active = false;
    std::string macroName;
    std::unique_ptr<omnipath::EvolutionSolver> solver;
    std::uint32_t tick = 0;
    bool p1Down = false;
    bool p2Down = false;
    int deadTicks = 0;
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

void saveBestMacro() {
    if (!g_live.solver || !g_live.solver->hasBest()) return;

    auto const& best = g_live.solver->best();
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
    out << "  \"format\": \"omnipath-v1\",\n";
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
    out << "\n  ]\n}\n";
    log::info("saved OmniPath macro to {}", path.string());
}

class OmniPathPopup : public Popup {
protected:
    TextInput* m_nameInput = nullptr;
    CCLabelBMFont* m_modeLabel = nullptr;
    LevelInfoLayer* m_owner = nullptr;
    omnipath::SearchMode m_mode = omnipath::SearchMode::GuidedMutation;

    bool init(LevelInfoLayer* owner) {
        if (!Popup::init(330.f, 215.f)) return false;
        m_owner = owner;
        setTitle("OmniPath");

        m_nameInput = TextInput::create(220.f, "macro name", "bigFont.fnt");
        m_nameInput->setString(g_pending.macroName, false);
        m_nameInput->setPosition({165.f, 142.f});
        m_mainLayer->addChild(m_nameInput);

        auto modeTitle = CCLabelBMFont::create("search mode", "goldFont.fnt");
        modeTitle->setScale(.55f);
        modeTitle->setPosition({165.f, 108.f});
        m_mainLayer->addChild(modeTitle);

        m_modeLabel = CCLabelBMFont::create("guided mutation", "bigFont.fnt");
        m_modeLabel->setScale(.45f);
        m_modeLabel->setPosition({165.f, 84.f});
        m_mainLayer->addChild(m_modeLabel);

        auto modeSpr = ButtonSprite::create("switch", .65f);
        auto modeBtn = CCMenuItemSpriteExtra::create(
            modeSpr, this, menu_selector(OmniPathPopup::onSwitchMode)
        );
        modeBtn->setPosition({110.f, 42.f});
        m_buttonMenu->addChild(modeBtn);

        auto startSpr = ButtonSprite::create("arm + play", .65f);
        auto startBtn = CCMenuItemSpriteExtra::create(
            startSpr, this, menu_selector(OmniPathPopup::onStart)
        );
        startBtn->setPosition({220.f, 42.f});
        m_buttonMenu->addChild(startBtn);

        return true;
    }

    void onSwitchMode(CCObject*) {
        m_mode = m_mode == omnipath::SearchMode::Evolution
            ? omnipath::SearchMode::GuidedMutation
            : omnipath::SearchMode::Evolution;
        m_modeLabel->setString(m_mode == omnipath::SearchMode::GuidedMutation
            ? "guided mutation" : "evolution");
    }

    void onStart(CCObject*) {
        if (!m_owner || !m_owner->m_level) return;
        g_pending.armed = true;
        g_pending.macroName = sanitizeName(std::string(m_nameInput->getString()));
        g_pending.mode = m_mode;
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
                "Platformer mode is not supported in this prototype",
                "OK"
            )->show();
            return true;
        }

        omnipath::SolverConfig cfg;
        cfg.population = 14;
        cfg.eliteCount = 3;
        cfg.genomeLength = 4096;
        cfg.decisionEveryTicks = g_pending.decisionTicks;
        cfg.mode = g_pending.mode;
        cfg.mutationRate = cfg.mode == omnipath::SearchMode::GuidedMutation ? 0.008 : 0.016;

        g_live = {};
        g_live.active = true;
        g_live.macroName = g_pending.macroName;
        g_live.solver = std::make_unique<omnipath::EvolutionSolver>(cfg);

        log::info("OmniPath armed: name={}, mode={}", g_live.macroName,
            cfg.mode == omnipath::SearchMode::GuidedMutation ? "guided" : "evolution");
        return true;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!g_live.active || !g_live.solver || !m_started) return;

        if (getCurrentPercent() >= 99.999f) {
            g_live.solver->submit(100.f, g_live.tick);
            saveBestMacro();
            releaseInputs(this);
            g_live.active = false;
            Notification::create("OmniPath: solved + macro saved", static_cast<CCNode*>(nullptr), 2.0f)->show();
            return;
        }

        if (m_playerDied) {
            ++g_live.deadTicks;
            if (g_live.deadTicks >= 3) {
                this->resetLevel();
            }
            return;
        }

        g_live.deadTicks = 0;
        applyGene(this, g_live.solver->geneForTick(g_live.tick));
        ++g_live.tick;
    }

    void resetLevel() {
        if (g_live.active && g_live.solver && g_live.tick > 0) {
            auto progress = getCurrentPercent();
            g_live.solver->submit(progress, g_live.tick);
            saveBestMacro();
            log::debug("OmniPath gen={} candidate={} progress={} best={}",
                g_live.solver->generation(), g_live.solver->currentIndex(), progress,
                g_live.solver->best().progress);
        }

        releaseInputs(this);
        g_live.tick = 0;
        g_live.deadTicks = 0;
        PlayLayer::resetLevel();
    }

    void onQuit() {
        if (g_live.active) {
            saveBestMacro();
            g_live.active = false;
        }
        PlayLayer::onQuit();
    }
};
