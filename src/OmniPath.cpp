#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/cocos.hpp>

#include "SolverCore.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace {

struct PendingConfig {
    bool armed = false;
    std::string macroName = "omnipath-run";
    std::uint32_t candidates = 100;
    std::uint32_t maxGenerations = 100;
};

PendingConfig g_pending;

struct LiveSession {
    bool active = false;
    bool injectingInput = false;
    bool ignoreTouch = true;
    bool ghosts = true;
    int trainingSpeed = 4;
    int appliedSpeed = 1;
    float baseTimeScale = 1.0f;

    std::string macroName;
    std::unique_ptr<omnipath::FrontierPlanner> solver;
    std::uint32_t tick = 0;
    std::uint32_t maxGenerations = 100;
    bool p1Down = false;
    bool p2Down = false;
    int deadTicks = 0;
    float lastX = 0.0f;
    float lastY = 0.0f;
    float maxX = 0.0f;
    float lastSavedBest = -1.0f;

    CCLabelBMFont* hudMain = nullptr;
    CCLabelBMFont* hudStats = nullptr;
    CCLabelBMFont* hudHistory = nullptr;
    CCDrawNode* ghostDraw = nullptr;
    std::vector<GameObject*> scanObjects;
    std::vector<std::vector<CCPoint>> trails;
    std::vector<CCPoint> currentTrail;
    std::size_t leaderIndex = 0;
    float leaderProgress = 0.0f;
};

LiveSession g_live;

std::string sanitizeName(std::string name) {
    if (name.empty()) name = "omnipath-run";
    for (auto& ch : name) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_')) ch = '_';
    }
    return name;
}

omnipath::PlayerMode playerMode(PlayerObject* p) {
    if (!p) return omnipath::PlayerMode::Unknown;
    if (p->m_isDart) return omnipath::PlayerMode::Wave;
    if (p->m_isShip) return omnipath::PlayerMode::Ship;
    if (p->m_isBird) return omnipath::PlayerMode::Ufo;
    if (p->m_isBall) return omnipath::PlayerMode::Ball;
    if (p->m_isRobot) return omnipath::PlayerMode::Robot;
    if (p->m_isSpider) return omnipath::PlayerMode::Spider;
    if (p->m_isSwing) return omnipath::PlayerMode::Swing;
    return omnipath::PlayerMode::Cube;
}

bool isObstacle(GameObjectType type) {
    switch (type) {
        case GameObjectType::Solid:
        case GameObjectType::Hazard:
        case GameObjectType::Slope:
        case GameObjectType::AnimatedHazard:
        case GameObjectType::CollisionObject:
            return true;
        default:
            return false;
    }
}

bool isHazard(GameObjectType type) {
    return type == GameObjectType::Hazard || type == GameObjectType::AnimatedHazard;
}

bool isInteractable(GameObjectType type) {
    switch (type) {
        case GameObjectType::YellowJumpRing:
        case GameObjectType::PinkJumpRing:
        case GameObjectType::GravityRing:
        case GameObjectType::GreenRing:
        case GameObjectType::DropRing:
        case GameObjectType::RedJumpRing:
        case GameObjectType::CustomRing:
        case GameObjectType::DashRing:
        case GameObjectType::GravityDashRing:
        case GameObjectType::SpiderOrb:
        case GameObjectType::TeleportOrb:
            return true;
        default:
            return false;
    }
}

omnipath::PlayerObservation observePlayer(PlayLayer* layer, PlayerObject* p) {
    omnipath::PlayerObservation out;
    if (!layer || !p) {
        out.active = false;
        return out;
    }

    out.mode = playerMode(p);
    out.x = p->getPositionX();
    out.y = p->getPositionY();
    out.yVelocity = p->m_yVelocity;
    out.playerSpeed = p->m_playerSpeed;
    out.onGround = p->m_isOnGround;
    out.upsideDown = p->m_isUpsideDown;
    out.dashing = p->m_isDashing;
    out.touchingRing = p->m_touchingRings && p->m_touchingRings->count() > 0;

    float upper = out.y + 210.0f;
    float lower = out.y - 210.0f;
    bool foundUpper = false;
    bool foundLower = false;
    float bestHazardDx = 99999.0f;
    float bestHazardDy = 0.0f;
    float bestInteractableDx = 99999.0f;

    for (auto obj : g_live.scanObjects) {
        if (!obj || obj == p) continue;

        auto dx = obj->getPositionX() - out.x;
        if (dx < 4.0f || dx > 300.0f) continue;

        auto dy = obj->getPositionY() - out.y;
        auto type = obj->getType();

        if (isInteractable(type) && std::abs(dy) < 100.0f && dx < bestInteractableDx) {
            bestInteractableDx = dx;
        }

        if (!isObstacle(type)) continue;

        if (dx <= 190.0f) {
            if (dy >= 18.0f && obj->getPositionY() < upper) {
                upper = obj->getPositionY();
                foundUpper = true;
            }
            if (dy <= -18.0f && obj->getPositionY() > lower) {
                lower = obj->getPositionY();
                foundLower = true;
            }
        }

        bool dangerousHeight = std::abs(dy) <= 80.0f || isHazard(type);
        if (dangerousHeight && dx < bestHazardDx) {
            bestHazardDx = dx;
            bestHazardDy = dy;
        }
    }

    out.nearestHazardDx = bestHazardDx;
    out.nearestHazardDy = bestHazardDy;
    out.hazardAhead = bestHazardDx < 99990.0f;
    out.nearestInteractableDx = bestInteractableDx;
    out.interactableAhead = bestInteractableDx < 99990.0f;

    if (foundUpper || foundLower) {
        if (!foundUpper) upper = lower + 260.0f;
        if (!foundLower) lower = upper - 260.0f;
        out.targetY = (upper + lower) * 0.5f;
        out.corridorHalfHeight = std::max(18.0f, (upper - lower) * 0.5f);
        out.hasTarget = true;
    }

    return out;
}

omnipath::Observation observeWorld(PlayLayer* layer) {
    omnipath::Observation o;
    if (!layer) return o;
    o.p1 = observePlayer(layer, layer->m_player1);
    o.dual = layer->m_player2 && !layer->m_player2->m_isHidden && layer->m_player2->isVisible();
    if (o.dual) o.p2 = observePlayer(layer, layer->m_player2);
    else o.p2.active = false;
    return o;
}

void setSpeedMultiplier(int multiplier) {
    if (!g_live.active) return;
    multiplier = std::clamp(multiplier, 1, 8);
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

    auto const& p = best.policy;
    out << "{\n";
    out << "  \"format\": \"omnipath-v3\",\n";
    out << "  \"planner\": \"frontier-object-aware\",\n";
    out << "  \"population\": " << g_live.solver->populationSize() << ",\n";
    out << "  \"decisionTicks\": " << g_live.solver->decisionEveryTicks() << ",\n";
    out << "  \"bestProgress\": " << best.progress << ",\n";
    out << "  \"bestMaxX\": " << best.maxX << ",\n";
    out << "  \"frontierTick\": " << g_live.solver->frontierTick() << ",\n";
    out << "  \"deathMode\": \"" << omnipath::modeName(best.deathMode) << "\",\n";
    out << "  \"policy\": {"
        << "\"hazardLead\":" << p.hazardLead
        << ",\"orbLead\":" << p.orbLead
        << ",\"cubeTapTicks\":" << p.cubeTapTicks
        << ",\"robotHoldTicks\":" << p.robotHoldTicks
        << ",\"shipDwell\":" << p.shipDwell
        << ",\"waveDwell\":" << p.waveDwell
        << ",\"targetBias\":" << p.targetBias
        << ",\"verticalGain\":" << p.verticalGain
        << ",\"velocityGain\":" << p.velocityGain
        << ",\"hysteresis\":" << p.hysteresis << "},\n";
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
    log::info("saved OmniPath v3 macro to {}", path.string());
}

void createHud(PlayLayer* layer) {
    if (!layer) return;
    auto win = CCDirector::sharedDirector()->getWinSize();

    g_live.hudMain = CCLabelBMFont::create("OmniPath FRONTIER", "bigFont.fnt");
    g_live.hudMain->setAnchorPoint({0.0f, 1.0f});
    g_live.hudMain->setScale(0.34f);
    g_live.hudMain->setPosition({7.0f, win.height - 7.0f});
    layer->addChild(g_live.hudMain, 100000);

    g_live.hudStats = CCLabelBMFont::create("starting...", "bigFont.fnt");
    g_live.hudStats->setAnchorPoint({0.0f, 1.0f});
    g_live.hudStats->setScale(0.27f);
    g_live.hudStats->setPosition({7.0f, win.height - 23.0f});
    layer->addChild(g_live.hudStats, 100000);

    g_live.hudHistory = CCLabelBMFont::create("gen best: -", "bigFont.fnt");
    g_live.hudHistory->setAnchorPoint({0.0f, 1.0f});
    g_live.hudHistory->setScale(0.23f);
    g_live.hudHistory->setPosition({7.0f, win.height - 37.0f});
    layer->addChild(g_live.hudHistory, 100000);

    if (layer->m_objectLayer) {
        g_live.ghostDraw = CCDrawNode::create();
        layer->m_objectLayer->addChild(g_live.ghostDraw, 99999);
    }
}

void redrawGhosts() {
    if (!g_live.ghostDraw) return;
    g_live.ghostDraw->clear();
    if (!g_live.ghosts) return;

    for (std::size_t i = 0; i < g_live.trails.size(); ++i) {
        auto const& trail = g_live.trails[i];
        if (trail.size() < 2) continue;
        bool leader = i == g_live.leaderIndex;
        ccColor4F color = leader
            ? ccColor4F{0.35f, 1.0f, 0.42f, 0.62f}
            : ccColor4F{0.35f, 0.72f, 1.0f, 0.14f};
        float radius = leader ? 0.75f : 0.32f;
        for (std::size_t j = 1; j < trail.size(); ++j)
            g_live.ghostDraw->drawSegment(trail[j - 1], trail[j], radius, color);
    }

    if (g_live.currentTrail.size() >= 2) {
        ccColor4F current{1.0f, 0.88f, 0.25f, 0.75f};
        for (std::size_t j = 1; j < g_live.currentTrail.size(); ++j)
            g_live.ghostDraw->drawSegment(g_live.currentTrail[j - 1], g_live.currentTrail[j], 0.8f, current);
    }
}

void updateHud(PlayLayer* layer, omnipath::Observation const* observation = nullptr, bool completed = false) {
    if (!g_live.solver || !g_live.hudMain || !g_live.hudStats || !g_live.hudHistory) return;

    auto candidate = g_live.solver->currentIndex() + 1;
    std::ostringstream main;
    main << "FRONTIER  G " << (g_live.solver->generation() + 1)
         << "  C " << candidate << "/" << g_live.solver->populationSize();
    if (completed) main << "  DONE";
    g_live.hudMain->setString(main.str().c_str());

    auto best = g_live.solver->hasBest() ? g_live.solver->best().progress : 0.0f;
    auto now = layer ? layer->getCurrentPercent() : 0.0f;
    auto mode = observation ? omnipath::modeName(observation->p1.mode) : "-";

    std::ostringstream stats;
    stats << mode << "  now " << percentText(now, 1) << "%"
          << "  best " << percentText(best, 1) << "%"
          << "  frontier " << g_live.solver->frontierTick()
          << "  x" << g_live.appliedSpeed
          << (g_live.ignoreTouch ? "  TOUCH LOCK" : "  TOUCH ON");
    g_live.hudStats->setString(stats.str().c_str());

    auto const& history = g_live.solver->generationHistory();
    std::ostringstream hist;
    hist << "gen best: ";
    if (history.empty()) hist << "-";
    else {
        auto begin = history.size() > 8 ? history.size() - 8 : 0;
        for (std::size_t i = begin; i < history.size(); ++i) {
            if (i != begin) hist << " | ";
            hist << static_cast<int>(std::round(history[i])) << "%";
        }
    }
    g_live.hudHistory->setString(hist.str().c_str());
}

void endTraining(PlayLayer* layer, char const* message) {
    saveBestMacro(true);
    releaseInputs(layer);
    restoreTimeScale();
    g_live.active = false;
    updateHud(layer, nullptr, true);
    if (message) {
        Notification::create(message, static_cast<CCNode*>(nullptr), 3.0f)->show();
    }
}

class OmniPathPopup : public Popup {
protected:
    TextInput* m_nameInput = nullptr;
    CCLabelBMFont* m_candidatesLabel = nullptr;
    LevelInfoLayer* m_owner = nullptr;
    std::uint32_t m_candidates = 100;

    void refreshCandidates() {
        if (!m_candidatesLabel) return;
        auto text = std::to_string(m_candidates) + " real candidates";
        m_candidatesLabel->setString(text.c_str());
    }

    bool init(LevelInfoLayer* owner) {
        if (!Popup::init(360.f, 260.f)) return false;
        m_owner = owner;
        m_candidates = std::clamp<std::uint32_t>(g_pending.candidates, 10, 100);
        setTitle("OmniPath v0.3 FRONTIER");

        auto subtitle = CCLabelBMFont::create("object-aware planner / no random click spam", "goldFont.fnt");
        subtitle->setScale(.34f);
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

        auto popTitle = CCLabelBMFont::create("generation size", "goldFont.fnt");
        popTitle->setScale(.46f);
        popTitle->setPosition({180.f, 111.f});
        m_mainLayer->addChild(popTitle);

        m_candidatesLabel = CCLabelBMFont::create("100 real candidates", "bigFont.fnt");
        m_candidatesLabel->setScale(.42f);
        m_candidatesLabel->setPosition({180.f, 87.f});
        m_mainLayer->addChild(m_candidatesLabel);
        refreshCandidates();

        auto minus = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("-10", .58f), this, menu_selector(OmniPathPopup::onMinus)
        );
        minus->setPosition({92.f, 84.f});
        m_buttonMenu->addChild(minus);

        auto plus = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("+10", .58f), this, menu_selector(OmniPathPopup::onPlus)
        );
        plus->setPosition({268.f, 84.f});
        m_buttonMenu->addChild(plus);

        auto hint = CCLabelBMFont::create("speed / touch lock / ghosts are in pause", "bigFont.fnt");
        hint->setScale(.26f);
        hint->setPosition({180.f, 53.f});
        m_mainLayer->addChild(hint);

        auto start = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("START FRONTIER", .62f), this, menu_selector(OmniPathPopup::onStart)
        );
        start->setPosition({180.f, 25.f});
        m_buttonMenu->addChild(start);
        return true;
    }

    void onMinus(CCObject*) {
        if (m_candidates > 10) m_candidates -= 10;
        refreshCandidates();
    }

    void onPlus(CCObject*) {
        if (m_candidates < 100) m_candidates += 10;
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

        auto win = CCDirector::sharedDirector()->getWinSize();
        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->setID("omnipath-pause-menu");
        addChild(menu, 10000);

        auto title = CCLabelBMFont::create("OmniPath", "goldFont.fnt");
        title->setScale(.42f);
        title->setPosition({win.width * .5f, 54.f});
        addChild(title, 10000);

        auto speedText = std::string("speed x") + std::to_string(g_live.trainingSpeed);
        auto speed = CCMenuItemSpriteExtra::create(
            ButtonSprite::create(speedText.c_str(), .48f),
            this,
            menu_selector(OmniPathPauseLayer::onSpeed)
        );
        speed->setPosition({win.width * .5f - 105.f, 28.f});
        menu->addChild(speed);

        auto touch = CCMenuItemSpriteExtra::create(
            ButtonSprite::create(g_live.ignoreTouch ? "touch LOCK" : "touch ON", .48f),
            this,
            menu_selector(OmniPathPauseLayer::onTouch)
        );
        touch->setPosition({win.width * .5f, 28.f});
        menu->addChild(touch);

        auto ghosts = CCMenuItemSpriteExtra::create(
            ButtonSprite::create(g_live.ghosts ? "ghosts ON" : "ghosts OFF", .48f),
            this,
            menu_selector(OmniPathPauseLayer::onGhosts)
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
            case 4: g_live.trainingSpeed = 8; break;
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
        if (!g_live.ghosts && g_live.ghostDraw) g_live.ghostDraw->clear();
        updateButton(sender, g_live.ghosts ? "ghosts ON" : "ghosts OFF");
    }
};

class $modify(OmniPathPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        if (!g_pending.armed) return true;
        g_pending.armed = false;

        if (m_isPlatformer) {
            FLAlertLayer::create("OmniPath", "Platformer mode is not supported", "OK")->show();
            return true;
        }

        omnipath::SolverConfig cfg;
        cfg.population = g_pending.candidates;
        cfg.eliteCount = std::max<std::size_t>(3, cfg.population / 8);
        cfg.genomeLength = 18000;
        cfg.decisionEveryTicks = 1;
        cfg.frontierMarginTicks = 90;

        g_live = {};
        g_live.active = true;
        g_live.ignoreTouch = true;
        g_live.ghosts = true;
        g_live.trainingSpeed = 4;
        g_live.appliedSpeed = 1;
        g_live.macroName = g_pending.macroName;
        g_live.maxGenerations = g_pending.maxGenerations;
        g_live.solver = std::make_unique<omnipath::FrontierPlanner>(cfg);
        g_live.trails.resize(cfg.population);

        if (m_objects) {
            g_live.scanObjects.reserve(m_objects->count());
            for (auto obj : geode::cocos::CCArrayExt<GameObject, false>(m_objects)) {
                if (!obj) continue;
                auto type = obj->getType();
                if (isObstacle(type) || isInteractable(type))
                    g_live.scanObjects.push_back(obj);
            }
        }

        auto scheduler = CCDirector::sharedDirector()->getScheduler();
        g_live.baseTimeScale = scheduler ? scheduler->getTimeScale() : 1.0f;

        if (m_player1) {
            g_live.lastX = m_player1->getPositionX();
            g_live.lastY = m_player1->getPositionY();
            g_live.maxX = g_live.lastX;
        }

        releaseInputs(this);
        createHud(this);
        auto observation = observeWorld(this);
        updateHud(this, &observation);
        setSpeedMultiplier(g_live.trainingSpeed);

        log::info(
            "OmniPath FRONTIER started: name={}, candidates={}, maxGenerations={}, speed=x{}",
            g_live.macroName,
            cfg.population,
            g_live.maxGenerations,
            g_live.trainingSpeed
        );
        return true;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!g_live.active || !g_live.solver || !m_started) return;

        if (m_player1) {
            g_live.lastX = m_player1->getPositionX();
            g_live.lastY = m_player1->getPositionY();
            g_live.maxX = std::max(g_live.maxX, g_live.lastX);
        }

        auto observation = observeWorld(this);

        int effective = g_live.trainingSpeed;
        if (g_live.solver->hasBest()) {
            auto frontier = g_live.solver->frontierTick();
            if (g_live.tick + 220 >= frontier) effective = std::min(effective, 2);
        }
        if (observation.p1.hazardAhead && observation.p1.nearestHazardDx < 100.0f)
            effective = 1;
        if (observation.p1.mode == omnipath::PlayerMode::Wave && observation.p1.hasTarget)
            effective = std::min(effective, 2);
        setSpeedMultiplier(effective);

        if ((g_live.tick % 7) == 0 && m_player1) {
            if (g_live.currentTrail.size() < 72)
                g_live.currentTrail.push_back(m_player1->getPosition());
        }
        if ((g_live.tick % 21) == 0) redrawGhosts();
        if ((g_live.tick % 5) == 0) updateHud(this, &observation);

        if (getCurrentPercent() >= 99.999f) {
            auto oldIndex = g_live.solver->currentIndex();
            if (oldIndex < g_live.trails.size()) g_live.trails[oldIndex] = g_live.currentTrail;

            omnipath::AttemptResult result;
            result.progress = 100.0f;
            result.maxX = g_live.maxX;
            result.deathX = g_live.lastX;
            result.deathY = g_live.lastY;
            result.survivedTicks = g_live.tick;
            result.deathMode = observation.p1.mode;
            result.died = false;
            g_live.solver->submit(result);

            endTraining(this, "OmniPath: SOLVED - macro saved");
            redrawGhosts();
            return;
        }

        if (m_playerDied) {
            ++g_live.deadTicks;
            if (g_live.deadTicks >= 2) this->resetLevel();
            return;
        }

        g_live.deadTicks = 0;
        auto gene = g_live.solver->decide(g_live.tick, observation);
        applyGene(this, gene);
        ++g_live.tick;
    }

    void resetLevel() {
        bool finished = false;

        if (g_live.active && g_live.solver && g_live.tick > 0) {
            auto candidateIndex = g_live.solver->currentIndex();
            auto generationBefore = g_live.solver->generation();
            auto oldBest = g_live.solver->hasBest() ? g_live.solver->best().progress : -1.0f;
            auto progress = getCurrentPercent();

            if (candidateIndex < g_live.trails.size())
                g_live.trails[candidateIndex] = g_live.currentTrail;
            if (progress >= g_live.leaderProgress) {
                g_live.leaderProgress = progress;
                g_live.leaderIndex = candidateIndex;
            }

            omnipath::AttemptResult result;
            result.progress = progress;
            result.maxX = g_live.maxX;
            result.deathX = g_live.lastX;
            result.deathY = g_live.lastY;
            result.survivedTicks = g_live.tick;
            result.deathMode = playerMode(m_player1);
            result.died = m_playerDied;
            g_live.solver->submit(result);

            auto newBest = g_live.solver->hasBest() ? g_live.solver->best().progress : -1.0f;
            if (newBest > oldBest + 0.001f) saveBestMacro(false);

            auto generationAfter = g_live.solver->generation();
            bool generationClosed = generationAfter != generationBefore;
            if (generationClosed) {
                log::info(
                    "OmniPath generation {} complete: leader={} best={} frontier={} stuck={}",
                    generationBefore + 1,
                    g_live.leaderIndex + 1,
                    newBest,
                    g_live.solver->frontierTick(),
                    g_live.solver->stagnantGenerations()
                );
                if (generationAfter >= g_live.maxGenerations) finished = true;
            }
        }

        releaseInputs(this);
        g_live.tick = 0;
        g_live.deadTicks = 0;
        g_live.maxX = 0.0f;
        g_live.currentTrail.clear();
        PlayLayer::resetLevel();

        if (m_player1) g_live.maxX = m_player1->getPositionX();
        redrawGhosts();

        if (finished) {
            endTraining(this, "OmniPath: generation limit reached - best macro saved");
        }
    }

    void onQuit() {
        if (g_live.active) {
            saveBestMacro(true);
            releaseInputs(this);
            restoreTimeScale();
            g_live.active = false;
        }
        PlayLayer::onQuit();
    }
};
