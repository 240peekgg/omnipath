                p.y += static_cast<float>(p.vy * stepScale);
                break;
            }

            case omnipath::PlayerMode::Ball: {
                if (step == 0 && pressed) {
                    p.upsideDown = !p.upsideDown;
                    direction = p.upsideDown ? -1.0 : 1.0;
                    p.vy = direction * 3.0;
                }
                p.vy += -direction * 0.84 * stepScale;
                p.y += static_cast<float>(p.vy * stepScale);
                break;
            }

            case omnipath::PlayerMode::Spider: {
                if (step == 0 && pressed) {
                    p.upsideDown = !p.upsideDown;
                    p.vy = p.upsideDown ? 15.0 : -15.0;
                }
                p.vy *= std::pow(0.72, stepScale);
                p.y += static_cast<float>(p.vy * stepScale);
                break;
            }

            case omnipath::PlayerMode::Swing: {
                if (step == 0 && pressed) p.upsideDown = !p.upsideDown;
                direction = p.upsideDown ? -1.0 : 1.0;
                p.vy += direction * 0.44 * stepScale;
                p.vy = std::clamp(p.vy, -8.0, 8.0);
                p.y += static_cast<float>(p.vy * stepScale);
                break;
            }

            case omnipath::PlayerMode::Unknown:
            default:
                p.vy += -0.92 * stepScale;
                p.y += static_cast<float>(p.vy * stepScale);
                break;
        }

        p.x += stepDx;
        collideWorld(p, previousY);
    }
}

SimPlayer fromRealPlayer(PlayerObject* player) {
    SimPlayer out;
    if (!player) {
        out.active = false;
        out.alive = false;
        return out;
    }

    out.active = true;
    out.alive = true;
    out.x = player->getPositionX();
    out.y = player->getPositionY();
    out.vy = player->m_yVelocity;
    out.mode = playerMode(player);
    out.onGround = player->m_isOnGround;
    out.upsideDown = player->m_isUpsideDown;
    out.dashing = player->m_isDashing;
    return out;
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

std::string percentText(float value, int precision = 1) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(precision) << value;
    return ss.str();
}

void clearGhosts() {
    for (auto& agent : g_live.agents) {
        if (agent.ghost1) {
            agent.ghost1->removeFromParentAndCleanup(true);
            agent.ghost1 = nullptr;
        }
        if (agent.ghost2) {
            agent.ghost2->removeFromParentAndCleanup(true);
            agent.ghost2 = nullptr;
        }
    }
}

CCSprite* createGhostSprite(PlayLayer* layer, std::size_t index, bool secondary) {
    if (!layer || !layer->m_objectLayer) return nullptr;
    auto sprite = CCSprite::createWithSpriteFrameName("player_01_001.png");
    if (!sprite) return nullptr;

    sprite->setScale(0.72f);
    sprite->setOpacity(secondary ? 38 : 52);
    auto tint = static_cast<GLubyte>(110 + (index * 23) % 120);
    sprite->setColor(ccColor3B{90, tint, 255});
    layer->m_objectLayer->addChild(sprite, 99990);
    return sprite;
}

void ensureGhostSprites(PlayLayer* layer, bool dual) {
    if (!layer) return;
    for (std::size_t i = 0; i < g_live.agents.size(); ++i) {
        auto& agent = g_live.agents[i];
        if (!agent.ghost1) agent.ghost1 = createGhostSprite(layer, i, false);
        if (dual && !agent.ghost2) agent.ghost2 = createGhostSprite(layer, i, true);
    }
}

void updateGhostSprites() {
    for (std::size_t i = 0; i < g_live.agents.size(); ++i) {
        auto& agent = g_live.agents[i];
        bool leader = i == g_live.leaderIndex;

        if (agent.ghost1) {
            agent.ghost1->setVisible(g_live.ghosts && !g_live.verifying && agent.alive && agent.p1.alive);
            agent.ghost1->setPosition({agent.p1.x, agent.p1.y});
            agent.ghost1->setOpacity(leader ? 185 : 48);
            agent.ghost1->setScale(leader ? 0.82f : 0.70f);
        }

        if (agent.ghost2) {
            agent.ghost2->setVisible(g_live.ghosts && !g_live.verifying && agent.alive && agent.p2.alive);
            agent.ghost2->setPosition({agent.p2.x, agent.p2.y});
            agent.ghost2->setOpacity(leader ? 150 : 34);
            agent.ghost2->setScale(leader ? 0.78f : 0.66f);
        }
    }
}

void createHud(PlayLayer* layer) {
    if (!layer) return;
    auto win = CCDirector::sharedDirector()->getWinSize();

    g_live.hudMain = CCLabelBMFont::create("OmniPath PARALLEL", "bigFont.fnt");
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
}

void updateHud(PlayLayer* layer) {
    if (!g_live.solver || !g_live.hudMain || !g_live.hudStats || !g_live.hudHistory) return;

    std::ostringstream main;
    if (g_live.verifying) {
        main << "VERIFY  G " << g_live.solver->generation()
             << "  real " << percentText(g_live.verifyProgress, 1) << "%";
    } else {
        main << "PARALLEL  G " << (g_live.solver->generation() + 1)
             << "  alive " << g_live.aliveCount << "/" << g_live.solver->populationSize();
    }
    g_live.hudMain->setString(main.str().c_str());

    auto simulatedBest = g_live.solver->hasBest() ? g_live.solver->best().progress : 0.0f;
    auto now = layer ? layer->getCurrentPercent() : 0.0f;
    auto mode = "-";
    if (!g_live.verifying && g_live.leaderIndex < g_live.agents.size())
        mode = omnipath::modeName(g_live.agents[g_live.leaderIndex].p1.mode);
    else if (layer && layer->m_player1)
        mode = omnipath::modeName(playerMode(layer->m_player1));

    std::ostringstream stats;
    stats << mode << "  now " << percentText(now) << "%"
          << "  sim-best " << percentText(simulatedBest) << "%"
          << "  verified " << percentText(g_live.bestVerifiedProgress) << "%"
          << "  x" << g_live.appliedSpeed;
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

void saveCandidateMacro(omnipath::Candidate const& candidate, std::string const& suffix = "") {
    if (!g_live.solver) return;

    auto dir = Mod::get()->getSaveDir() / "macros";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    auto name = sanitizeName(g_live.macroName + suffix);
    auto path = dir / (name + ".omnipath.json");

    std::ofstream out(path);
    if (!out) {
        log::error("failed to save OmniPath macro to {}", path.string());
        return;
    }

    auto const& p = candidate.policy;
    out << "{\n";
    out << "  \"format\": \"omnipath-v4\",\n";
    out << "  \"planner\": \"parallel-shadow-verified\",\n";
    out << "  \"population\": " << g_live.solver->populationSize() << ",\n";
    out << "  \"decisionTicks\": " << g_live.solver->decisionEveryTicks() << ",\n";
    out << "  \"progress\": " << candidate.progress << ",\n";
    out << "  \"maxX\": " << candidate.maxX << ",\n";
    out << "  \"verified\": " << (g_live.hasVerifiedCandidate ? "true" : "false") << ",\n";
    out << "  \"policy\": {"
        << "\"cubeLead\":" << p.cubeLead
        << ",\"cubeSpanGain\":" << p.cubeSpanGain
        << ",\"cubeHoldTicks\":" << p.cubeHoldTicks
        << ",\"orbLead\":" << p.orbLead
        << ",\"shipGain\":" << p.shipGain
        << ",\"shipVelocityGain\":" << p.shipVelocityGain
        << ",\"waveDeadzone\":" << p.waveDeadzone
        << ",\"robotLead\":" << p.robotLead << "},\n";
    out << "  \"events\": [\n";

    bool previous1 = false;
    bool previous2 = false;
    bool first = true;
    for (std::size_t i = 0; i < candidate.genes.size(); ++i) {
        auto const& gene = candidate.genes[i];
        if (gene.p1 == previous1 && gene.p2 == previous2) continue;
        if (!first) out << ",\n";
        first = false;
        out << "    {\"tick\":" << (i * g_live.solver->decisionEveryTicks())
            << ",\"p1\":" << (gene.p1 ? "true" : "false")
            << ",\"p2\":" << (gene.p2 ? "true" : "false") << "}";
        previous1 = gene.p1;
        previous2 = gene.p2;
    }

    out << "\n  ]\n}\n";
    log::info("saved OmniPath v4 macro to {}", path.string());
