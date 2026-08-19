            }
        }

        if ((isSolid(type) || isHazard(type)) && rect.getMaxX() >= p.x + 4.0f && rect.getMinX() <= p.x + 190.0f) {
            if (rect.getMinY() >= p.y + 10.0f && rect.getMinY() < upper) {
                upper = rect.getMinY();
                foundUpper = true;
            }
            if (rect.getMaxY() <= p.y - 10.0f && rect.getMaxY() > lower) {
                lower = rect.getMaxY();
                foundLower = true;
            }
        }
    });

    if (!hazardIntervals.empty()) {
        std::sort(hazardIntervals.begin(), hazardIntervals.end());
        cluster.start = hazardIntervals.front().first;
        cluster.end = hazardIntervals.front().second;
        for (std::size_t i = 1; i < hazardIntervals.size(); ++i) {
            if (hazardIntervals[i].first - cluster.end > 20.0f) break;
            cluster.end = std::max(cluster.end, hazardIntervals[i].second);
        }
    }

    out.nearestHazardDx = nearestHazardDx;
    out.nearestHazardDy = nearestHazardDy;
    out.hazardAhead = nearestHazardDx < 99990.0f;
    out.hazardSpan = cluster.end > cluster.start ? cluster.end - cluster.start : 0.0f;
    out.nearestInteractableDx = nearestInteractableDx;
    out.interactableAhead = nearestInteractableDx < 99990.0f;

    if (foundLower || foundUpper) {
        if (!foundLower) lower = upper - 250.0f;
        if (!foundUpper) upper = lower + 250.0f;
        out.targetY = (lower + upper) * 0.5f;
        out.corridorHalfHeight = std::max(16.0f, (upper - lower) * 0.5f);
        out.hasTarget = true;
    }

    return out;
}

omnipath::Observation observeAgent(SimAgent const& agent) {
    omnipath::Observation out;
    out.p1 = observeSimPlayer(agent.p1);
    out.dual = agent.dual;
    if (agent.dual) out.p2 = observeSimPlayer(agent.p2);
    else out.p2.active = false;
    return out;
}

void setMode(SimPlayer& p, GameObjectType type) {
    switch (type) {
        case GameObjectType::ShipPortal: p.mode = omnipath::PlayerMode::Ship; break;
        case GameObjectType::CubePortal: p.mode = omnipath::PlayerMode::Cube; break;
        case GameObjectType::BallPortal: p.mode = omnipath::PlayerMode::Ball; break;
        case GameObjectType::UfoPortal: p.mode = omnipath::PlayerMode::Ufo; break;
        case GameObjectType::WavePortal: p.mode = omnipath::PlayerMode::Wave; break;
        case GameObjectType::RobotPortal: p.mode = omnipath::PlayerMode::Robot; break;
        case GameObjectType::SpiderPortal: p.mode = omnipath::PlayerMode::Spider; break;
        case GameObjectType::SwingPortal: p.mode = omnipath::PlayerMode::Swing; break;
        default: break;
    }
}

void applyPortal(SimPlayer& p, GameObject* obj) {
    auto type = obj->getType();
    if (obj == p.lastPortal) return;

    switch (type) {
        case GameObjectType::InverseGravityPortal: p.upsideDown = true; break;
        case GameObjectType::NormalGravityPortal: p.upsideDown = false; break;
        case GameObjectType::GravityTogglePortal: p.upsideDown = !p.upsideDown; break;
        default: setMode(p, type); break;
    }

    if (type == GameObjectType::CubePortal || type == GameObjectType::BallPortal ||
        type == GameObjectType::RobotPortal || type == GameObjectType::SpiderPortal) {
        p.vy *= 0.55;
    }

    p.lastPortal = obj;
}

void applyPad(SimPlayer& p, GameObject* obj) {
    if (obj == p.lastPad) return;
    auto type = obj->getType();
    double direction = p.upsideDown ? -1.0 : 1.0;

    switch (type) {
        case GameObjectType::PinkJumpPad: p.vy = direction * 8.0; break;
        case GameObjectType::YellowJumpPad: p.vy = direction * 11.2; break;
        case GameObjectType::RedJumpPad: p.vy = direction * 14.2; break;
        case GameObjectType::GravityPad:
            p.upsideDown = !p.upsideDown;
            p.vy = (p.upsideDown ? -1.0 : 1.0) * 10.0;
            break;
        case GameObjectType::SpiderPad:
            p.upsideDown = !p.upsideDown;
            p.vy = 0.0;
            break;
        default: break;
    }

    p.onGround = false;
    p.lastPad = obj;
}

void applyRing(SimPlayer& p, GameObject* obj, bool input) {
    if (!input || obj == p.lastRing) return;
    auto type = obj->getType();
    double direction = p.upsideDown ? -1.0 : 1.0;

    switch (type) {
        case GameObjectType::PinkJumpRing: p.vy = direction * 7.4; break;
        case GameObjectType::YellowJumpRing: p.vy = direction * 10.0; break;
        case GameObjectType::RedJumpRing: p.vy = direction * 12.8; break;
        case GameObjectType::GravityRing:
        case GameObjectType::GreenRing:
            p.upsideDown = !p.upsideDown;
            p.vy = (p.upsideDown ? -1.0 : 1.0) * 9.0;
            break;
        case GameObjectType::DropRing: p.vy = 0.0; break;
        case GameObjectType::SpiderOrb:
            p.upsideDown = !p.upsideDown;
            p.vy = 0.0;
            break;
        default: p.vy = direction * 9.6; break;
    }

    p.onGround = false;
    p.lastRing = obj;
}

void processInteractiveObjects(SimPlayer& p, bool input) {
    bool touchingPortal = false;
    bool touchingPad = false;
    bool touchingRing = false;
    auto hitbox = playerRect(p, 2.0f);

    forNearby(p.x, 35.0f, 48.0f, [&](GameObject* obj) {
        auto type = obj->getType();
        if (!(isPortal(type) || isPad(type) || isRing(type))) return;
        auto rect = insetRect(obj->getObjectRect(), 1.0f, 1.0f);
        if (!rectsIntersect(hitbox, rect)) return;

        if (isPortal(type)) {
            touchingPortal = true;
            applyPortal(p, obj);
        } else if (isPad(type)) {
            touchingPad = true;
            applyPad(p, obj);
        } else if (isRing(type)) {
            touchingRing = true;
            applyRing(p, obj, input);
        }
    });

    if (!touchingPortal) p.lastPortal = nullptr;
    if (!touchingPad) p.lastPad = nullptr;
    if (!touchingRing) p.lastRing = nullptr;
}

bool collideWorld(SimPlayer& p, float previousY) {
    if (!p.alive || !p.active) return false;

    auto hitbox = playerRect(p, 1.5f);
    bool landed = false;
    float half = 12.0f;
    double gravityDirection = p.upsideDown ? 1.0 : -1.0;

    forNearby(p.x, 38.0f, 38.0f, [&](GameObject* obj) {
        if (!p.alive) return;
        auto type = obj->getType();
        auto rect = obj->getObjectRect();

        if (isHazard(type)) {
            auto hazard = insetRect(rect, 5.0f, 6.0f);
            if (rectsIntersect(hitbox, hazard)) p.alive = false;
            return;
        }

        if (!isSolid(type) || !rectsIntersect(hitbox, rect)) return;

        float prevBottom = previousY - half;
        float prevTop = previousY + half;
        float nowBottom = p.y - half;
        float nowTop = p.y + half;

        if (!p.upsideDown && p.vy <= 0.0 && prevBottom >= rect.getMaxY() - 4.0f && nowBottom <= rect.getMaxY() + 4.0f) {
            p.y = rect.getMaxY() + half;
            p.vy = 0.0;
            p.onGround = true;
            landed = true;
            return;
        }

        if (p.upsideDown && p.vy >= 0.0 && prevTop <= rect.getMinY() + 4.0f && nowTop >= rect.getMinY() - 4.0f) {
            p.y = rect.getMinY() - half;
            p.vy = 0.0;
            p.onGround = true;
            landed = true;
            return;
        }

        if ((gravityDirection < 0.0 && p.vy > 0.0 && prevTop <= rect.getMinY() + 2.0f) ||
            (gravityDirection > 0.0 && p.vy < 0.0 && prevBottom >= rect.getMaxY() - 2.0f)) {
            p.vy = 0.0;
            return;
        }

        p.alive = false;
    });

    if (!p.upsideDown && !landed && p.mode != omnipath::PlayerMode::Ship &&
        p.mode != omnipath::PlayerMode::Wave && p.mode != omnipath::PlayerMode::Ufo &&
        p.y <= g_live.baseGroundY) {
        p.y = g_live.baseGroundY;
        p.vy = 0.0;
        p.onGround = true;
        landed = true;
    }

    if (p.y < -450.0f || p.y > 3300.0f) p.alive = false;
    return landed;
}

void simulatePlayer(SimPlayer& p, bool input, float dx, float dt) {
    if (!p.active || !p.alive) return;

    float frameScale = std::clamp(dt * 60.0f, 0.25f, 8.0f);
    int steps = std::clamp(static_cast<int>(std::ceil(frameScale)), 1, 8);
    float stepScale = frameScale / static_cast<float>(steps);
    float stepDx = dx / static_cast<float>(steps);

    bool pressed = input && !p.held;
    p.held = input;

    for (int step = 0; step < steps && p.alive; ++step) {
        processInteractiveObjects(p, input);
        float previousY = p.y;
        bool wasOnGround = p.onGround;
        p.onGround = false;
        double direction = p.upsideDown ? -1.0 : 1.0;

        switch (p.mode) {
            case omnipath::PlayerMode::Cube: {
                bool ground = (!p.upsideDown && p.y <= g_live.baseGroundY + 0.25f) || wasOnGround;
                if (input && (ground || (step == 0 && pressed && std::abs(p.vy) < 0.1)))
                    p.vy = direction * 11.45;
                p.vy += -direction * 0.92 * stepScale;
                p.y += static_cast<float>(p.vy * stepScale);
                break;
            }

            case omnipath::PlayerMode::Robot: {
                bool ground = (!p.upsideDown && p.y <= g_live.baseGroundY + 0.25f) || wasOnGround;
                if ((step == 0 && pressed) && ground) p.vy = direction * 10.5;
                if (input && p.vy * direction > 0.0) p.vy += direction * 0.24 * stepScale;
                p.vy += -direction * 0.92 * stepScale;
                p.y += static_cast<float>(p.vy * stepScale);
                break;
            }

            case omnipath::PlayerMode::Ship: {
                double acceleration = input ? 0.52 : -0.43;
                p.vy += direction * acceleration * stepScale;
                p.vy *= std::pow(0.985, stepScale);
                p.vy = std::clamp(p.vy, -8.6, 8.6);
                p.y += static_cast<float>(p.vy * stepScale);
                break;
            }

            case omnipath::PlayerMode::Wave: {
                float waveDirection = input ? 1.0f : -1.0f;
                if (p.upsideDown) waveDirection = -waveDirection;
                p.y += std::abs(stepDx) * waveDirection;
                p.vy = waveDirection * std::abs(stepDx) / std::max(0.01f, stepScale);
                break;
            }

            case omnipath::PlayerMode::Ufo: {
                if (step == 0 && pressed) p.vy = direction * 7.8;
                p.vy += -direction * 0.62 * stepScale;
