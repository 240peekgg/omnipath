            bool desired = control > p.shipDeadzone * 0.01f;
            return setHeld(desired, p.shipDwell);
        }

        case PlayerMode::Wave: {
            bool desired = error > p.waveDeadzone;
            if (std::abs(error) <= p.waveDeadzone) desired = rt.held;
            return setHeld(desired, p.waveDwell);
        }

        case PlayerMode::Ufo: {
            if (rt.held && tick >= rt.heldUntil) {
                rt.held = false;
                rt.lastChangeTick = tick;
            }
            if (error > p.ufoDeadzone && tick >= rt.cooldownUntil)
                return pulse(1, p.ufoCooldown);
            return rt.held;
        }

        case PlayerMode::Ball:
        case PlayerMode::Spider:
        case PlayerMode::Swing: {
            if (rt.held && tick >= rt.heldUntil) {
                rt.held = false;
                rt.lastChangeTick = tick;
            }
            bool hazardSwitch = o.hazardAhead && o.nearestHazardDx <= 62.0f;
            bool targetSwitch = std::abs(error) > p.toggleDeadzone;
            if ((hazardSwitch || targetSwitch) && tick >= rt.cooldownUntil)
                return pulse(1, p.toggleCooldown);
            return rt.held;
        }

        case PlayerMode::Unknown:
        default: {
            if (rt.held && tick >= rt.heldUntil) rt.held = false;
            if (o.onGround && o.hazardAhead && o.nearestHazardDx <= p.cubeLead && tick >= rt.cooldownUntil)
                return pulse(3, 4);
            return rt.held;
        }
    }
}

Gene ParallelEvolution::decide(std::size_t candidateIndex, std::uint32_t tick, Observation const& obs) {
    if (candidateIndex >= m_population.size()) return {};

    auto& candidate = m_population[candidateIndex];
    auto& runtime = m_runtime[candidateIndex];

    Gene gene;
    gene.p1 = decidePlayer(tick, obs.p1, candidate.policy, runtime.p1);
    gene.p2 = obs.dual ? decidePlayer(tick, obs.p2, candidate.policy, runtime.p2) : false;

    auto geneIndex = static_cast<std::size_t>(tick / m_cfg.decisionEveryTicks);
    if (geneIndex < candidate.genes.size()) candidate.genes[geneIndex] = gene;
    return gene;
}

std::uint32_t ParallelEvolution::countToggles(Candidate const& c, std::uint32_t survivedTicks) const {
    auto count = std::min<std::size_t>(
        c.genes.size(),
        static_cast<std::size_t>(survivedTicks / m_cfg.decisionEveryTicks + 2)
    );

    Gene previous{};
    std::uint32_t toggles = 0;
    for (std::size_t i = 0; i < count; ++i) {
        auto const& gene = c.genes[i];
        if (gene.p1 != previous.p1) ++toggles;
        if (gene.p2 != previous.p2) ++toggles;
        previous = gene;
    }
    return toggles;
}

double ParallelEvolution::score(AttemptResult const& result, std::uint32_t toggles) const {
    auto progress = clampValue<double>(result.progress, 0.0, 100.0);
    auto completionBonus = result.died ? 0.0 : 1.0e12;
    return completionBonus
        + progress * progress * progress * 10000.0
        + static_cast<double>(result.maxX) * 250.0
        + static_cast<double>(result.survivedTicks) * 3.0
        - static_cast<double>(toggles) * 1.25;
}

PolicyParams ParallelEvolution::mutatePolicy(PolicyParams const& parent, std::size_t childIndex) {
    PolicyParams p = parent;
    double spread = 1.0 + std::min<std::size_t>(m_stagnantGenerations, 10) * 0.20;
    std::normal_distribution<double> n(0.0, spread);
    auto bias = lane(childIndex, 13);

    p.cubeLead = clampValue<float>(p.cubeLead + static_cast<float>(n(m_rng) * 7.0) + bias * 5.0f, 30.0f, 170.0f);
    p.cubeSpanGain = clampValue<float>(p.cubeSpanGain + static_cast<float>(n(m_rng) * 0.035), 0.05f, 0.85f);
    p.cubeHoldTicks = clampValue<std::uint16_t>(static_cast<std::uint16_t>(std::max(1, static_cast<int>(p.cubeHoldTicks) + static_cast<int>(std::round(n(m_rng) * 0.8)))), 1, 9);
    p.cubeCooldown = clampValue<std::uint16_t>(static_cast<std::uint16_t>(std::max(1, static_cast<int>(p.cubeCooldown) + static_cast<int>(std::round(n(m_rng) * 0.5)))), 1, 8);

    p.orbLead = clampValue<float>(p.orbLead + static_cast<float>(n(m_rng) * 4.0), 10.0f, 80.0f);
    p.orbCooldown = clampValue<std::uint16_t>(static_cast<std::uint16_t>(std::max(2, static_cast<int>(p.orbCooldown) + static_cast<int>(std::round(n(m_rng) * 0.5)))), 2, 12);

    p.targetBias = clampValue<float>(p.targetBias + static_cast<float>(n(m_rng) * 7.0), -90.0f, 90.0f);
    p.shipGain = clampValue<float>(p.shipGain + static_cast<float>(n(m_rng) * 0.006), 0.015f, 0.18f);
    p.shipVelocityGain = clampValue<float>(p.shipVelocityGain + static_cast<float>(n(m_rng) * 0.05), 0.05f, 1.2f);
    p.shipDeadzone = clampValue<float>(p.shipDeadzone + static_cast<float>(n(m_rng) * 1.1), 0.5f, 24.0f);
    p.shipDwell = clampValue<std::uint16_t>(static_cast<std::uint16_t>(std::max(1, static_cast<int>(p.shipDwell) + static_cast<int>(std::round(n(m_rng) * 0.35)))), 1, 6);

    p.waveDeadzone = clampValue<float>(p.waveDeadzone + static_cast<float>(n(m_rng) * 1.4), 1.0f, 28.0f);
    p.waveDwell = clampValue<std::uint16_t>(static_cast<std::uint16_t>(std::max(1, static_cast<int>(p.waveDwell) + static_cast<int>(std::round(n(m_rng) * 0.25)))), 1, 4);

    p.ufoDeadzone = clampValue<float>(p.ufoDeadzone + static_cast<float>(n(m_rng) * 1.7), 2.0f, 36.0f);
    p.ufoCooldown = clampValue<std::uint16_t>(static_cast<std::uint16_t>(std::max(3, static_cast<int>(p.ufoCooldown) + static_cast<int>(std::round(n(m_rng))))), 3, 20);

    p.robotLead = clampValue<float>(p.robotLead + static_cast<float>(n(m_rng) * 7.0), 35.0f, 190.0f);
    p.robotHoldTicks = clampValue<std::uint16_t>(static_cast<std::uint16_t>(std::max(3, static_cast<int>(p.robotHoldTicks) + static_cast<int>(std::round(n(m_rng) * 1.5)))), 3, 34);

    p.toggleDeadzone = clampValue<float>(p.toggleDeadzone + static_cast<float>(n(m_rng) * 2.0), 4.0f, 48.0f);
    p.toggleCooldown = clampValue<std::uint16_t>(static_cast<std::uint16_t>(std::max(3, static_cast<int>(p.toggleCooldown) + static_cast<int>(std::round(n(m_rng))))), 3, 22);
    return p;
}

void ParallelEvolution::breedNextGeneration() {
    std::sort(m_population.begin(), m_population.end(), [](Candidate const& a, Candidate const& b) {
        return a.fitness > b.fitness;
    });

    auto elites = std::min<std::size_t>(m_cfg.eliteCount, m_population.size());
    std::vector<Candidate> next;
    next.reserve(m_cfg.population);

    for (std::size_t i = 0; i < elites; ++i) {
        Candidate elite;
        elite.policy = m_population[i].policy;
        elite.genes.assign(m_cfg.genomeLength, {});
        next.push_back(std::move(elite));
    }

    std::uniform_int_distribution<std::size_t> choose(0, elites - 1);
    while (next.size() < m_cfg.population) {
        auto parent = choose(m_rng);
        Candidate child;
        child.policy = mutatePolicy(m_population[parent].policy, next.size());
        child.genes.assign(m_cfg.genomeLength, {});
        next.push_back(std::move(child));
    }

    m_population = std::move(next);
    m_runtime.resize(m_population.size());
}

void ParallelEvolution::submitGeneration(std::vector<AttemptResult> const& results) {
    if (results.size() != m_population.size()) return;

    m_generationLeaderIndex = 0;
    m_generationLeaderProgress = 0.0f;
    float generationBest = 0.0f;

    for (std::size_t i = 0; i < m_population.size(); ++i) {
        auto& candidate = m_population[i];
        auto const& result = results[i];

        candidate.progress = clampValue<float>(result.progress, 0.0f, 100.0f);
        candidate.maxX = result.maxX;
        candidate.survivedTicks = result.survivedTicks;
        candidate.deathMode = result.deathMode;
        candidate.toggles = countToggles(candidate, result.survivedTicks);
        candidate.fitness = score(result, candidate.toggles);

        generationBest = std::max(generationBest, candidate.progress);
        if (candidate.progress > m_generationLeaderProgress || i == 0) {
            m_generationLeaderProgress = candidate.progress;
            m_generationLeaderIndex = i;
        }

        if (!m_hasBest || candidate.fitness > m_best.fitness) {
            m_best = candidate;
            m_hasBest = true;
        }
    }

    float previous = m_generationHistory.empty() ? -1.0f : m_generationHistory.back();
    m_generationHistory.push_back(generationBest);
    if (m_generationHistory.size() > 120)
        m_generationHistory.erase(m_generationHistory.begin());

    if (generationBest <= previous + 0.02f) ++m_stagnantGenerations;
    else m_stagnantGenerations = 0;

    breedNextGeneration();
    ++m_generation;
    beginGeneration();
}

std::size_t ParallelEvolution::generation() const { return m_generation; }
std::size_t ParallelEvolution::populationSize() const { return m_population.size(); }
std::size_t ParallelEvolution::generationLeaderIndex() const { return m_generationLeaderIndex; }
float ParallelEvolution::generationLeaderProgress() const { return m_generationLeaderProgress; }
std::size_t ParallelEvolution::stagnantGenerations() const { return m_stagnantGenerations; }
const std::vector<float>& ParallelEvolution::generationHistory() const { return m_generationHistory; }
const std::vector<Candidate>& ParallelEvolution::population() const { return m_population; }
const Candidate& ParallelEvolution::candidate(std::size_t index) const { return m_population.at(index); }
const Candidate& ParallelEvolution::best() const { return m_best; }
bool ParallelEvolution::hasBest() const { return m_hasBest; }
std::uint32_t ParallelEvolution::decisionEveryTicks() const { return m_cfg.decisionEveryTicks; }

} // namespace omnipath
