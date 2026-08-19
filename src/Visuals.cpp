#include "Session.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace omnipath::app {

ccColor3B ghostColor(std::size_t index) {
    static constexpr ccColor3B palette[] = {
        {76, 201, 240},
        {114, 239, 221},
        {144, 224, 95},
        {255, 209, 102},
        {255, 126, 145},
        {190, 142, 255},
    };
    return palette[index % (sizeof(palette) / sizeof(palette[0]))];
}

CCSprite* createGhostSprite(PlayLayer* layer, std::size_t index, bool secondary) {
    if (!layer || !layer->m_objectLayer) return nullptr;
    auto sprite = CCSprite::createWithSpriteFrameName("player_01_001.png");
    if (!sprite) return nullptr;

    sprite->setColor(ghostColor(index + (secondary ? 3 : 0)));
    sprite->setOpacity(secondary ? 42 : 62);
    sprite->setScale(secondary ? 0.61f : 0.68f);
    layer->m_objectLayer->addChild(sprite, 99990);
    return sprite;
}

void clearGhosts() {
    for (auto& visual : g_live.visuals) {
        if (visual.p1) {
            visual.p1->removeFromParentAndCleanup(true);
            visual.p1 = nullptr;
        }
        if (visual.p2) {
            visual.p2->removeFromParentAndCleanup(true);
            visual.p2 = nullptr;
        }
    }
}

void ensureGhosts(PlayLayer* layer) {
    if (!layer) return;
    if (g_live.visuals.size() != g_live.agents.size())
        g_live.visuals.resize(g_live.agents.size());

    for (std::size_t i = 0; i < g_live.agents.size(); ++i) {
        if (!g_live.visuals[i].p1)
            g_live.visuals[i].p1 = createGhostSprite(layer, i, false);
        if (g_live.agents[i].dual && !g_live.visuals[i].p2)
            g_live.visuals[i].p2 = createGhostSprite(layer, i, true);
    }
}

void updateGhosts(PlayLayer* layer) {
    if (!layer) return;
    ensureGhosts(layer);

    for (std::size_t i = 0; i < g_live.agents.size(); ++i) {
        auto const& agent = g_live.agents[i];
        auto& visual = g_live.visuals[i];
        bool leader = i == g_live.leaderIndex;
        bool showP1 = g_live.ghosts && !g_live.verifying && agent.alive && agent.p1.alive;
        bool showP2 = showP1 && agent.dual && agent.p2.alive;

        if (visual.p1) {
            visual.p1->setVisible(showP1);
            if (showP1) {
                visual.p1->setPosition({agent.p1.x, agent.p1.y});
                visual.p1->setFlipY(agent.p1.upsideDown);
                visual.p1->setColor(leader ? ccColor3B{255, 226, 106} : ghostColor(i));
                visual.p1->setOpacity(leader ? 225 : (agent.lastGene.p1 ? 96 : 58));
                visual.p1->setScale(leader ? 0.84f : (agent.lastGene.p1 ? 0.72f : 0.67f));
                if (agent.p1.mode == omnipath::PlayerMode::Cube)
                    visual.p1->setRotation(std::fmod(agent.p1.x * 0.42f, 360.0f));
                else
                    visual.p1->setRotation(0.0f);
            }
        }

        if (agent.dual && !visual.p2)
            visual.p2 = createGhostSprite(layer, i, true);
        if (visual.p2) {
            visual.p2->setVisible(showP2);
            if (showP2) {
                visual.p2->setPosition({agent.p2.x, agent.p2.y});
                visual.p2->setFlipY(agent.p2.upsideDown);
                visual.p2->setColor(leader ? ccColor3B{255, 196, 96} : ghostColor(i + 3));
                visual.p2->setOpacity(leader ? 185 : (agent.lastGene.p2 ? 78 : 42));
                visual.p2->setScale(leader ? 0.76f : 0.61f);
            }
        }
    }
}

void createHud(PlayLayer* layer) {
    if (!layer) return;
    auto win = CCDirector::sharedDirector()->getWinSize();

    g_live.hudPanel = CCLayerColor::create(ccColor4B{7, 11, 20, 205}, 266.0f, 62.0f);
    g_live.hudPanel->setPosition({6.0f, win.height - 68.0f});
    layer->addChild(g_live.hudPanel, 100000);

    g_live.hudTitle = CCLabelBMFont::create("OMNIPATH", "bigFont.fnt");
    g_live.hudTitle->setAnchorPoint({0.0f, 0.5f});
    g_live.hudTitle->setScale(0.34f);
    g_live.hudTitle->setPosition({8.0f, 50.0f});
    g_live.hudPanel->addChild(g_live.hudTitle);

    g_live.hudPhase = CCLabelBMFont::create("EXPLORE / RANDOM INPUTS", "goldFont.fnt");
    g_live.hudPhase->setAnchorPoint({1.0f, 0.5f});
    g_live.hudPhase->setScale(0.25f);
    g_live.hudPhase->setPosition({258.0f, 50.0f});
    g_live.hudPanel->addChild(g_live.hudPhase);

    g_live.hudStats = CCLabelBMFont::create("initializing", "bigFont.fnt");
    g_live.hudStats->setAnchorPoint({0.0f, 0.5f});
    g_live.hudStats->setScale(0.26f);
    g_live.hudStats->setPosition({8.0f, 31.0f});
    g_live.hudPanel->addChild(g_live.hudStats);

    g_live.hudHistory = CCLabelBMFont::create("best: -", "bigFont.fnt");
    g_live.hudHistory->setAnchorPoint({0.0f, 0.5f});
    g_live.hudHistory->setScale(0.21f);
    g_live.hudHistory->setOpacity(190);
    g_live.hudHistory->setPosition({8.0f, 13.0f});
    g_live.hudPanel->addChild(g_live.hudHistory);
}

void updateHud() {
    if (!g_live.evolution || !g_live.hudTitle || !g_live.hudPhase ||
        !g_live.hudStats || !g_live.hudHistory) return;

    std::ostringstream title;
    if (g_live.verifying)
        title << "OMNIPATH  VERIFY";
    else
        title << "OMNIPATH  G" << (g_live.evolution->generation() + 1);
    g_live.hudTitle->setString(title.str().c_str());
    g_live.hudPhase->setString(phaseName());

    float leaderProgress = 0.0f;
    auto mode = "-";
    if (!g_live.verifying && g_live.leaderIndex < g_live.agents.size()) {
        leaderProgress = g_live.agents[g_live.leaderIndex].progress;
        mode = omnipath::modeName(g_live.agents[g_live.leaderIndex].p1.mode);
    } else if (g_live.verifying) {
        leaderProgress = g_live.verifyProgress;
    }

    auto allTimeBest = g_live.evolution->hasBest() ? g_live.evolution->best().progress : 0.0f;
    std::ostringstream stats;
    if (g_live.verifying) {
        stats << "real " << percentText(g_live.verifyProgress)
              << "%   verified-best " << percentText(g_live.bestVerifiedProgress) << "%";
    } else {
        stats << "alive " << g_live.aliveCount << "/" << g_live.evolution->populationSize()
              << "   lead " << percentText(leaderProgress) << "%"
              << "   " << mode << "   x" << g_live.appliedSpeed;
    }
    g_live.hudStats->setString(stats.str().c_str());

    std::ostringstream history;
    history << "all-time " << percentText(allTimeBest) << "%   gen history ";
    auto const& values = g_live.evolution->generationHistory();
    if (values.empty()) {
        history << "-";
    } else {
        auto begin = values.size() > 6 ? values.size() - 6 : 0;
        for (std::size_t i = begin; i < values.size(); ++i) {
            if (i != begin) history << " / ";
            history << static_cast<int>(std::round(values[i]));
        }
    }
    g_live.hudHistory->setString(history.str().c_str());
}


} // namespace omnipath::app
