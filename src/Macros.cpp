#include "Session.hpp"

#include <filesystem>
#include <fstream>

namespace omnipath::app {

void saveCandidateMacro(
    omnipath::Candidate const& candidate,
    bool verified,
    std::string const& suffix
) {
    if (!g_live.evolution) return;

    auto dir = Mod::get()->getSaveDir() / "macros";
    std::error_code error;
    std::filesystem::create_directories(dir, error);
    auto path = dir / (sanitizeName(g_live.macroName + suffix) + ".omnipath.json");

    std::ofstream out(path);
    if (!out) {
        log::error("failed to save OmniPath macro to {}", path.string());
        return;
    }

    out << "{\n";
    out << "  \"format\": \"omnipath-v5\",\n";
    out << "  \"strategy\": \"random-genome-evolution\",\n";
    out << "  \"verified\": " << (verified ? "true" : "false") << ",\n";
    out << "  \"generation\": " << g_live.verificationGeneration << ",\n";
    out << "  \"population\": " << g_live.evolution->populationSize() << ",\n";
    out << "  \"seed\": " << g_live.evolution->seed() << ",\n";
    out << "  \"decisionTicks\": " << g_live.evolution->decisionEveryTicks() << ",\n";
    out << "  \"progress\": " << candidate.progress << ",\n";
    out << "  \"maxX\": " << candidate.maxX << ",\n";
    out << "  \"events\": [\n";

    bool previous1 = false;
    bool previous2 = false;
    bool first = true;
    for (std::size_t i = 0; i < candidate.genes.size(); ++i) {
        auto const& gene = candidate.genes[i];
        if (gene.p1 == previous1 && gene.p2 == previous2) continue;
        if (!first) out << ",\n";
        first = false;
        out << "    {\"tick\":" << (i * g_live.evolution->decisionEveryTicks())
            << ",\"p1\":" << (gene.p1 ? "true" : "false")
            << ",\"p2\":" << (gene.p2 ? "true" : "false") << "}";
        previous1 = gene.p1;
        previous2 = gene.p2;
    }

    out << "\n  ]\n}\n";
    log::info("saved OmniPath v5 macro to {}", path.string());
}

void saveBestMacro() {
    if (g_live.hasVerifiedCandidate) {
        saveCandidateMacro(g_live.verifiedCandidate, true);
    } else if (g_live.evolution && g_live.evolution->hasBest()) {
        saveCandidateMacro(g_live.evolution->best(), false, "-simulated");
    }
}


} // namespace omnipath::app
