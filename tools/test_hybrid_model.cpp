#include "test_hybrid_model.hpp"
#include "../src/core/learning/hybrid_model.hpp"
#include "../src/core/learning/hybrid_observation.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>

int HybridModelTests::run() {
    int failures = 0;
    auto check = [&](bool ok, char const* name) {
        if (!ok) {
            ++failures;
            std::cerr << name << '\n';
        }
    };
    EbHybridObservation outcome;
    outcome.tick = 100;
    outcome.touched = true;
    check(outcome.begin(4, 2, 17, true, false), "track orb");
    outcome.fire(1, 4, 0);
    outcome.fire(2, 3, 0);
    outcome.fire(2, 4, -1.f);
    check(outcome.rewardAt(119, false) == 0 && !outcome.delivered, "wrong delivery");
    outcome.fire(2, 4, 0);
    outcome.physics(false, true, 0);
    check(outcome.rewardAt(119, false) == 0, "latched ring is ambiguous");
    outcome.physics(false, true, 1);
    check(outcome.rewardAt(117, false) == 0 && outcome.rewardAt(129, false) == 3,
          "survived past baseline with activation");
    check(outcome.rewardAt(106, true) < 0 && outcome.rewardAt(130, true) == 0, "local death only");
    outcome = {};
    check(outcome.rewardAt(119, true) == 0, "reset cancels attribution");
    check(!outcome.begin(3, 4, 110, false, false), "distant baseline excluded");
    outcome.begin(2, 8, -1, false, true);
    outcome.fire(8, 2, 0);
    outcome.physics(true, false, 0);
    check(outcome.rewardAt(28, false) == 1, "grounded survival");
    outcome.physics(false, false, 0);
    outcome.physics(true, false, 0);
    check(outcome.rewardAt(28, false) == 3, "return to ground");
    outcome = {};
    outcome.begin(0, 1, -1, false, false);
    check(outcome.rewardAt(28, false) == 1, "safe immediate input");
    outcome = {};
    outcome.begin(3, 91, 8, false, false);
    outcome.fire(91, 3, 0.5f);
    check(outcome.delivered && outcome.phaseBin == 4,
          "integer assist dispatched during real update is delivered");
    check(outcome.rewardAt(40, false) == 3, "successful delayed dispatch earns assist evidence");
    EbHybridModel model;
    uint64_t const key = EbHybridModel::mix(42, 17);
    check(model.proposal(key) < 0, "cold");
    for (int click = 0; click < 100; ++click)
        model.observe(key, 5, 3);
    check(model.proposal(key) < 0, "duplicate observations in one attempt");
    for (int attempt = 0; attempt < 2; ++attempt) {
        model.nextAttempt();
        model.observe(key, 5, 3);
    }
    check(model.proposal(key) == 5 && model.familiar() == 1, "held-out familiar click");
    model.setFrozen(true);
    for (int attempt = 0; attempt < 8; ++attempt) {
        model.nextAttempt();
        model.observe(key, 5, -4);
        model.observe(key + 1, 3, 3);
        check(model.proposal(key) == 5 && model.proposal(key + 1) < 0,
              "held-out attempts cannot update the model");
    }
    model.setFrozen(false);
    check(model.proposal(EbHybridModel::mix(42, 18)) < 0, "unseen route");
    model.nextAttempt();
    model.observe(key, 5, 0);
    check(model.proposal(key) == 5, "unknown has no weight");
    model.observe(key, 5, -4);
    check(model.proposal(key) < 0, "drift");
    for (int attempt = 0; attempt < 4; ++attempt) {
        model.nextAttempt();
        model.observe(key, 2, 3);
    }
    check(model.proposal(key) == 2, "changed timing");
    EbHybridModel mature;
    for (int i = 0; i < 40; ++i) {
        mature.nextAttempt();
        mature.observe(key, 5, 3);
    }
    mature.nextAttempt();
    mature.observe(key, 5, -4);
    check(mature.proposal(key, 3, 5) == 5, "established timing survives isolated death");
    check(mature.proposal(key, 3, 4) < 0, "current window excludes learned timing");
    for (int i = 0; i < 8; ++i) {
        mature.nextAttempt();
        mature.observe(key, 5, -4);
    }
    check(mature.proposal(key) < 0, "repeated deaths suppress established timing");
    EbHybridModel explore;
    uint64_t const exploreKey = EbHybridModel::mix(99, 7);
    explore.setUcbCoefficient(2.f);
    explore.setNeighborBleed(.5f);
    for (int attempt = 0; attempt < 4; ++attempt) {
        explore.nextAttempt();
        explore.observe(exploreKey, 4, 3);
    }
    explore.noteProbeOutcome(exploreKey, 2, false);
    explore.noteProbeOutcome(exploreKey, 6, true);
    auto ranked = explore.rankedDelays(exploreKey, 3, 96, true);
    check(!ranked.empty() && ranked.front() == 4, "mature delay still leads verify order");
    check(std::find(ranked.begin(), ranked.end(), 6) != ranked.end(),
          "verified-safe probe enters explore ranking");
    check(std::find(ranked.begin(), ranked.end(), 3) != ranked.end() ||
              std::find(ranked.begin(), ranked.end(), 5) != ranked.end(),
          "neighbor bleed seeds adjacent delays");
    auto path = std::filesystem::temp_directory_path() /
                ("eb-hybrid-test-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    EbHybridModel::Evidence ambiguous;
    ambiguous.unknownCount = 1;
    ambiguous.interveningInputs = 2;
    ambiguous.deathDeltaTicks = 13;
    ambiguous.timingPhase = 6;
    ambiguous.inputSequenceSignature = 5;
    model.observe(key + 7, 4, 0, &ambiguous);
    check(model.proposal(key + 7) < 0, "ambiguous death has no learned penalty or proposal");
    check(model.save(path), "save");
    EbHybridModel restored;
    check(restored.load(path) && restored.proposal(key) == 2, "restart");
    bool evidenceFound = false;
    for (auto const& row : restored.inspect(3))
        if (row.key == key + 7)
            evidenceFound = row.weight == 0 && row.evidence.unknownCount == 1 &&
                            row.evidence.interveningInputs == 2 &&
                            row.evidence.deathDeltaTicks == 13 && row.evidence.timingPhase == 6;
    check(evidenceFound, "ambiguous death evidence survives restart");
    restored.reset();
    check(restored.save(path) && model.load(path) && model.familiar() == 0, "replace and reset");
    for (auto body : {"EBHY2 1\n", "EBHY1 1\n123 2", "EBHY1 1", ""}) {
        std::ofstream(path, std::ios::binary) << body;
        check(!restored.load(path) && restored.familiar() == 0, "damaged or incompatible");
    }
    for (int index = 1; index < 8192; ++index) {
        restored.nextAttempt();
        restored.observe(index, 1, 3);
    }
    check(restored.save(path) && std::filesystem::file_size(path) < 256 * 1024 && model.load(path),
          "bounded churn");
    std::filesystem::remove(path);
    return failures ? 1 : 0;
}
int main() {
    return HybridModelTests::run();
}
