#include "Evolution.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <unordered_set>

static std::uint64_t fingerprint(omnipath::Candidate const& candidate) {
    std::uint64_t h = 1469598103934665603ull;
    auto count = std::min<std::size_t>(candidate.genes.size(), 2048);
    for (std::size_t i = 0; i < count; ++i) {
        auto bits = static_cast<std::uint64_t>(candidate.genes[i].p1) |
            (static_cast<std::uint64_t>(candidate.genes[i].p2) << 1);
        h ^= bits + i * 17;
        h *= 1099511628211ull;
    }
    return h;
}

int main() {
    omnipath::EvolutionConfig cfg;
    cfg.population = 32;
    cfg.eliteCount = 6;
    cfg.genomeLength = 4096;
    cfg.decisionEveryTicks = 1;

    omnipath::EvolutionEngine engine(cfg);
    engine.reset(0xC0FFEE);

    assert(engine.generation() == 0);
    assert(engine.explorationGeneration());
    assert(engine.populationSize() == 32);

    std::unordered_set<std::uint64_t> fingerprints;
    std::size_t candidatesWithInput = 0;
    for (auto const& candidate : engine.population()) {
        fingerprints.insert(fingerprint(candidate));
        bool anyInput = false;
        bool anyReleaseAfterPress = false;
        bool seenPress = false;
        for (auto const& gene : candidate.genes) {
            if (gene.p1 || gene.p2) {
                anyInput = true;
                seenPress = true;
            } else if (seenPress) {
                anyReleaseAfterPress = true;
            }
        }
        if (anyInput && anyReleaseAfterPress) ++candidatesWithInput;
    }

    // Random exploration must actually be independent, not 32 copies of one policy.
    assert(fingerprints.size() >= 30);
    assert(candidatesWithInput == 32);

    // decide() must be a direct read of that candidate's own genome.
    for (std::size_t i = 0; i < engine.populationSize(); ++i) {
        for (std::uint32_t tick : {0u, 1u, 17u, 255u, 1024u}) {
            auto gene = engine.decide(i, tick);
            auto expected = engine.candidate(i).genes[tick];
            assert(gene.p1 == expected.p1);
            assert(gene.p2 == expected.p2);
        }
    }

    std::vector<omnipath::AttemptResult> results(engine.populationSize());
    for (std::size_t i = 0; i < results.size(); ++i) {
        results[i].progress = static_cast<float>(i) * 1.25f;
        results[i].maxX = 100.0f + static_cast<float>(i) * 25.0f;
        results[i].survivedTicks = 80u + static_cast<std::uint32_t>(i) * 9u;
    }
    results.back().progress = 55.0f;
    results.back().maxX = 1800.0f;
    results.back().survivedTicks = 900;

    engine.submitGeneration(results);
    assert(engine.generation() == 1);
    assert(!engine.explorationGeneration());
    assert(engine.hasBest());
    assert(engine.best().progress == 55.0f);

    fingerprints.clear();
    for (auto const& candidate : engine.population())
        fingerprints.insert(fingerprint(candidate));
    assert(fingerprints.size() >= 12);

    std::cout << "evolution smoke ok: random=" << candidatesWithInput
              << " unique-next=" << fingerprints.size() << '\n';
    return 0;
}
