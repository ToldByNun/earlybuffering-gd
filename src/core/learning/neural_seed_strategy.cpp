#include "neural_seed_strategy.hpp"
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

char const* EbNeuralSeedStrategy::kindName(Kind kind) {
    switch (kind) {
    case Kind::NovaParity:
        return "NovaParity";
    case Kind::Enhanced:
        return "Enhanced";
    }
    return "Enhanced";
}

EbNeuralSeedStrategy::Kind EbNeuralSeedStrategy::kindFromConfig(EbAiConfig const& cfg) {
    return cfg.seedStrategy == EbAiConfig::SeedStrategy::NovaParity ? Kind::NovaParity
                                                                    : Kind::Enhanced;
}

bool EbNeuralSeedStrategy::modelReady(bool neuralActive, bool modelLoaded) {
    return neuralActive && modelLoaded;
}

bool EbNeuralSeedStrategy::dutyAllows(int planTick, int dutyPeriod, int dutyPhase) {
    int const period = std::max(1, dutyPeriod);
    int const phase = ((dutyPhase % period) + period) % period;
    int const tick = planTick < 0 ? 0 : planTick;
    return (tick % period) == phase;
}

int EbNeuralSeedStrategy::novaSubsampleStep(int lo, int hi) {
    if (hi < lo)
        std::swap(lo, hi);
    int const span = hi - lo;
    int step = span / 60;
    if (step < 1)
        step = 1;
    return step;
}

std::vector<int> EbNeuralSeedStrategy::delaysForStrategy(Kind kind, int lo, int hi,
                                                         std::vector<int> const& enhancedDelays) {
    if (kind == Kind::Enhanced)
        return enhancedDelays;
    // NovaParity: linear subsample over [lo, hi].
    if (hi < lo)
        std::swap(lo, hi);
    if (lo < 0)
        lo = 0;
    int const step = novaSubsampleStep(lo, hi);
    std::vector<int> out;
    for (int d = lo; d <= hi; d += step)
        out.push_back(d);
    if (out.empty())
        out.push_back(lo);
    // Always include hi if not hit by the stride (Nova window edge).
    if (!out.empty() && out.back() != hi && hi >= lo)
        out.push_back(hi);
    return out;
}

bool EbNeuralSeedStrategy::passesStrategyGate(Kind kind, float neural, EbAiConfig const& cfg) {
    if (kind == Kind::NovaParity)
        return neural >= kNovaParityGate;
    if (!cfg.gatingEnabled)
        return true;
    // Enhanced: configurable floors only — never a hardcoded 0.6.
    return neural >= cfg.confidenceThreshold && neural >= cfg.minCandidateConfidence;
}

std::vector<EbNeuralSeedStrategy::Proposal>
EbNeuralSeedStrategy::selectProposals(Kind kind, std::vector<Proposal> const& evaluated,
                                      EbAiConfig const& cfg, Counters& counters) {
    std::vector<Proposal> accepted;
    accepted.reserve(evaluated.size());
    for (auto const& p : evaluated) {
        ++counters.seedsEvaluated;
        if (!passesStrategyGate(kind, p.neural, cfg))
            continue;
        accepted.push_back(p);
    }
    std::sort(accepted.begin(), accepted.end(),
              [](Proposal const& a, Proposal const& b) { return a.neural > b.neural; });

    if (kind == Kind::NovaParity) {
        // Single best above hard 0.6 — expand handoff is one seed.
        if (!accepted.empty()) {
            accepted.resize(1);
            ++counters.novaParityAccepts;
        }
        return accepted;
    }

    // Enhanced: top-k (maxRetained), score is proposal rank only.
    int const topK = std::max(1, cfg.maxRetained);
    if (static_cast<int>(accepted.size()) > topK)
        accepted.resize(static_cast<size_t>(topK));
    counters.enhancedProposes += static_cast<int>(accepted.size());
    return accepted;
}

std::string EbNeuralSeedStrategy::percentSureLabel(float neural) {
    float const pct = std::clamp(neural, 0.f, 1.f) * 100.f;
    return fmt::format("{:.0f}% sure", pct);
}

bool EbNeuralSeedStrategy::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok)
            return;
        ++fails;
        if (!report.empty())
            report += "; ";
        report += name;
    };

    expect(std::string(kindName(Kind::NovaParity)) == "NovaParity", "name-np");
    expect(std::string(kindName(Kind::Enhanced)) == "Enhanced", "name-en");
    expect(kNovaParityGate == 0.6f, "gate-06");

    expect(modelReady(true, true), "ready");
    expect(!modelReady(true, false), "not-loaded");
    expect(!modelReady(false, true), "not-active");

    expect(dutyAllows(0, 2, 0), "duty-0");
    expect(!dutyAllows(1, 2, 0), "duty-1");
    expect(dutyAllows(2, 2, 0), "duty-2");
    expect(dutyAllows(1, 2, 1), "duty-phase");

    expect(novaSubsampleStep(0, 60) == 1, "sub-60");
    expect(novaSubsampleStep(0, 120) == 2, "sub-120");
    expect(novaSubsampleStep(10, 10) == 1, "sub-eq");

    auto npDelays = delaysForStrategy(Kind::NovaParity, 0, 60, {});
    expect(!npDelays.empty() && npDelays.front() == 0 && npDelays.back() == 60, "np-delays");
    std::vector<int> enh = {1, 4, 8};
    expect(delaysForStrategy(Kind::Enhanced, 0, 60, enh) == enh, "enh-delays");

    auto& cfg = EbAiConfig::get();
    bool const savedGate = cfg.gatingEnabled;
    float const savedThresh = cfg.confidenceThreshold;
    float const savedMin = cfg.minCandidateConfidence;
    int const savedRetained = cfg.maxRetained;
    auto const savedStrategy = cfg.seedStrategy;
    cfg.gatingEnabled = true;
    cfg.confidenceThreshold = 0.45f;
    cfg.minCandidateConfidence = 0.0f;
    cfg.maxRetained = 3;
    cfg.seedStrategy = EbAiConfig::SeedStrategy::Enhanced;

    expect(passesStrategyGate(Kind::NovaParity, 0.59f, cfg) == false, "np-block");
    expect(passesStrategyGate(Kind::NovaParity, 0.60f, cfg), "np-pass");
    // Enhanced must NOT imply 0.6 when threshold is 0.45.
    expect(passesStrategyGate(Kind::Enhanced, 0.50f, cfg), "enh-pass-045");
    expect(!passesStrategyGate(Kind::Enhanced, 0.40f, cfg), "enh-block");
    expect(kindFromConfig(cfg) == Kind::Enhanced, "kind-enh");
    cfg.seedStrategy = EbAiConfig::SeedStrategy::NovaParity;
    expect(kindFromConfig(cfg) == Kind::NovaParity, "kind-np");

    Counters counters;
    std::vector<Proposal> evaluated = {{2, 0.55f, true}, {5, 0.72f, true}, {8, 0.61f, true},
                                       {10, 0.40f, true}};
    auto np = selectProposals(Kind::NovaParity, evaluated, cfg, counters);
    expect(np.size() == 1 && np[0].delay == 5 && std::fabs(np[0].neural - 0.72f) < 1e-4f,
           "np-best");
    expect(counters.novaParityAccepts == 1, "np-counter");

    Counters c2;
    auto en = selectProposals(Kind::Enhanced, evaluated, cfg, c2);
    expect(en.size() == 3, "enh-topk"); // 0.72, 0.61, 0.55 — 0.40 gated out
    expect(en[0].delay == 5 && en[1].delay == 8, "enh-order");
    expect(c2.enhancedProposes == 3, "enh-counter");

    expect(percentSureLabel(0.73f).find("73% sure") != std::string::npos, "pct-sure");

    cfg.gatingEnabled = savedGate;
    cfg.confidenceThreshold = savedThresh;
    cfg.minCandidateConfidence = savedMin;
    cfg.maxRetained = savedRetained;
    cfg.seedStrategy = savedStrategy;

    if (fails == 0) {
        report = "NeuralSeedStrategy selfCheck OK";
        return true;
    }
    report = fmt::format("NeuralSeedStrategy selfCheck {} fails: {}", fails, report);
    return false;
}
