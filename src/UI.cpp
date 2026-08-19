#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

#include "Session.hpp"

#include <algorithm>
#include <string>

using namespace geode::prelude;
using namespace omnipath::app;

class OmniPathPopup : public Popup {
protected:
    TextInput* m_nameInput = nullptr;
    CCLabelBMFont* m_populationLabel = nullptr;
    CCLabelBMFont* m_generationLabel = nullptr;
    LevelInfoLayer* m_owner = nullptr;
    std::uint32_t m_population = 32;
    std::uint32_t m_generations = 250;

    void refresh() {
        if (m_populationLabel) {
            auto text = std::to_string(m_population) + " agents";
            m_populationLabel->setString(text.c_str());
        }
        if (m_generationLabel) {
            auto text = std::to_string(m_generations) + " generations";
            m_generationLabel->setString(text.c_str());
        }
    }

    CCLayerColor* addCard(float y, float height) {
        auto card = CCLayerColor::create(ccColor4B{8, 12, 23, 95}, 326.0f, height);
        card->setPosition({17.0f, y});
        m_mainLayer->addChild(card, -1);
        return card;
    }

    bool init(LevelInfoLayer* owner) {
        if (!Popup::init(360.0f, 286.0f)) return false;
        m_owner = owner;
        m_population = std::clamp<std::uint32_t>(g_pending.candidates, 8, 128);
        m_generations = std::clamp<std::uint32_t>(g_pending.maxGenerations, 50, 1000);
        setTitle("OmniPath 0.5");

        auto subtitle = CCLabelBMFont::create("parallel evolution / strict collision shadows", "goldFont.fnt");
        subtitle->setScale(0.30f);
        subtitle->setPosition({180.0f, 231.0f});
        m_mainLayer->addChild(subtitle);

        addCard(166.0f, 49.0f);
        auto nameLabel = CCLabelBMFont::create("run name", "goldFont.fnt");
        nameLabel->setAnchorPoint({0.0f, 0.5f});
        nameLabel->setScale(0.36f);
        nameLabel->setPosition({28.0f, 202.0f});
        m_mainLayer->addChild(nameLabel);

        m_nameInput = TextInput::create(205.0f, "macro name", "bigFont.fnt");
        m_nameInput->setString(g_pending.macroName, false);
        m_nameInput->setPosition({222.0f, 191.0f});
        m_mainLayer->addChild(m_nameInput);

        addCard(105.0f, 51.0f);
        auto populationTitle = CCLabelBMFont::create("population", "goldFont.fnt");
        populationTitle->setScale(0.36f);
        populationTitle->setPosition({72.0f, 142.0f});
        m_mainLayer->addChild(populationTitle);

        m_populationLabel = CCLabelBMFont::create("32 agents", "bigFont.fnt");
        m_populationLabel->setScale(0.37f);
        m_populationLabel->setPosition({180.0f, 126.0f});
        m_mainLayer->addChild(m_populationLabel);

        auto popMinus = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("-8", 0.50f), this, menu_selector(OmniPathPopup::onPopulationMinus)
        );
        popMinus->setPosition({74.0f, 125.0f});
        m_buttonMenu->addChild(popMinus);

        auto popPlus = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("+8", 0.50f), this, menu_selector(OmniPathPopup::onPopulationPlus)
        );
        popPlus->setPosition({286.0f, 125.0f});
        m_buttonMenu->addChild(popPlus);

        auto generationTitle = CCLabelBMFont::create("limit", "goldFont.fnt");
        generationTitle->setScale(0.36f);
        generationTitle->setPosition({72.0f, 93.0f});
        m_mainLayer->addChild(generationTitle);

        m_generationLabel = CCLabelBMFont::create("250 generations", "bigFont.fnt");
        m_generationLabel->setScale(0.35f);
        m_generationLabel->setPosition({180.0f, 77.0f});
        m_mainLayer->addChild(m_generationLabel);

        auto genMinus = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("-50", 0.47f), this, menu_selector(OmniPathPopup::onGenerationMinus)
        );
        genMinus->setPosition({74.0f, 77.0f});
        m_buttonMenu->addChild(genMinus);

        auto genPlus = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("+50", 0.47f), this, menu_selector(OmniPathPopup::onGenerationPlus)
        );
        genPlus->setPosition({286.0f, 77.0f});
        m_buttonMenu->addChild(genPlus);

        auto explainer = CCLabelBMFont::create("gen 1: pure random taps  >  later: selection + mutation", "bigFont.fnt");
        explainer->setScale(0.245f);
        explainer->setOpacity(205);
        explainer->setPosition({180.0f, 49.0f});
        m_mainLayer->addChild(explainer);

        auto start = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("EVOLVE", 0.64f), this, menu_selector(OmniPathPopup::onStart)
        );
        start->setPosition({180.0f, 23.0f});
        m_buttonMenu->addChild(start);

        refresh();
        return true;
    }

    void onPopulationMinus(CCObject*) {
        if (m_population > 8) m_population = std::max<std::uint32_t>(8, m_population - 8);
        refresh();
    }

    void onPopulationPlus(CCObject*) {
        if (m_population < 128) m_population = std::min<std::uint32_t>(128, m_population + 8);
        refresh();
    }

    void onGenerationMinus(CCObject*) {
        if (m_generations > 50) m_generations = std::max<std::uint32_t>(50, m_generations - 50);
        refresh();
    }

    void onGenerationPlus(CCObject*) {
        if (m_generations < 1000) m_generations = std::min<std::uint32_t>(1000, m_generations + 50);
        refresh();
    }

    void onStart(CCObject*) {
        if (!m_owner || !m_owner->m_level) return;
        g_pending.armed = true;
        g_pending.macroName = sanitizeName(std::string(m_nameInput->getString()));
        g_pending.candidates = m_population;
        g_pending.maxGenerations = m_generations;
        this->onClose(nullptr);
        m_owner->onPlay(nullptr);
    }

public:
    static OmniPathPopup* create(LevelInfoLayer* owner) {
        auto result = new OmniPathPopup;
        if (result->init(owner)) {
            result->autorelease();
            return result;
        }
        delete result;
        return nullptr;
    }
};

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

        auto button = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("AI", 0.70f),
            this,
            menu_selector(OmniPathLevelInfoLayer::onOmniPath)
        );
        button->setID("omnipath-button");
        if (m_playBtnMenu) {
            button->setPosition({68.0f, 0.0f});
            m_playBtnMenu->addChild(button);
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
        menu->setPosition({0.0f, 0.0f});
        menu->setID("omnipath-pause-menu");
        addChild(menu, 10000);

        auto title = CCLabelBMFont::create("OmniPath 0.5", "goldFont.fnt");
        title->setScale(0.40f);
        title->setPosition({win.width * 0.5f, 58.0f});
        addChild(title, 10000);

        auto phase = CCLabelBMFont::create(phaseName(), "bigFont.fnt");
        phase->setScale(0.24f);
        phase->setOpacity(205);
        phase->setPosition({win.width * 0.5f, 45.0f});
        addChild(phase, 10000);

        auto speedText = std::string("speed x") + std::to_string(g_live.trainingSpeed);
        auto speed = CCMenuItemSpriteExtra::create(
            ButtonSprite::create(speedText.c_str(), 0.47f),
            this,
            menu_selector(OmniPathPauseLayer::onSpeed)
        );
        speed->setPosition({win.width * 0.5f - 105.0f, 23.0f});
        menu->addChild(speed);

        auto touch = CCMenuItemSpriteExtra::create(
            ButtonSprite::create(g_live.ignoreTouch ? "touch LOCK" : "touch ON", 0.47f),
            this,
            menu_selector(OmniPathPauseLayer::onTouch)
        );
        touch->setPosition({win.width * 0.5f, 23.0f});
        menu->addChild(touch);

        auto ghosts = CCMenuItemSpriteExtra::create(
            ButtonSprite::create(g_live.ghosts ? "ghosts ON" : "ghosts OFF", 0.47f),
            this,
            menu_selector(OmniPathPauseLayer::onGhosts)
        );
        ghosts->setPosition({win.width * 0.5f + 105.0f, 23.0f});
        menu->addChild(ghosts);
    }

    void updateButton(CCObject* sender, std::string const& text) {
        auto item = static_cast<CCMenuItemSpriteExtra*>(sender);
        if (!item) return;
        auto sprite = static_cast<ButtonSprite*>(item->getNormalImage());
        if (!sprite) return;
        sprite->setString(text.c_str());
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
        updateButton(sender, g_live.ghosts ? "ghosts ON" : "ghosts OFF");
        if (auto layer = PlayLayer::get()) updateGhosts(layer);
    }
};

