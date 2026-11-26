#ifndef NEURAL_SEED_STRATEGY_HPP
#define NEURAL_SEED_STRATEGY_HPP

#include "ai_config.hpp"
#include <cstdint>
#include <string>
#include <vector>

// Assist Pack AP-NL5 — dual neural seed strategies (DR1).
// NovaParity: reconstructed Nova cadence (duty + subsample + hard 0.6).
// Enhanced (default): configurable threshold + top-k; 0.6 never implied.
// Neural score is a proposal ranker only — survival comes from Shadow.
class EbNeuralSeedStrategy {
public:
    enum class Kind : std::uint8_t { Enhanced = 0, NovaParity = 1 };

    static constexpr float kNovaParityGate = 0.6f;

    class Proposal {
    public:
        int delay = 0;
        float neural = 0.f;
        bool press = true;

    private:
    };

    class Counters {
    public:
        int seedsEvaluated = 0;
        int novaParityAccepts = 0;
        int enhancedProposes = 0;
        int postVerifyArms = 0;
        int modelNotReady = 0;
        int dutySkipped = 0;

        void clear() {
            seedsEvaluated = 0;
            novaParityAccepts = 0;
            enhancedProposes = 0;
            postVerifyArms = 0;
            modelNotReady = 0;
            dutySkipped = 0;
        }

    private:
    };

    static char const* kindName(Kind kind);
    static Kind kindFromConfig(EbAiConfig const& cfg);

    // Model-ready gate — false ⇒ non-neural fallthrough (Adaptive/classic).
    static bool modelReady(bool neuralActive, bool modelLoaded);

    // NovaParity duty: seed only when planTick % period == phase (period>=1).
    static bool dutyAllows(int planTick, int dutyPeriod, int dutyPhase);

    // Window subsample step: Nova uses max((hi-lo)/60, 1).
    static int novaSubsampleStep(int lo, int hi);

    // Delays for the active strategy (NovaParity forces linear subsample; Enhanced
    // uses caller-provided adaptive/linear list).
    static std::vector<int> delaysForStrategy(Kind kind, int lo, int hi,
                                              std::vector<int> const& enhancedDelays);

    // Hard gate for NovaParity (always 0.6). Enhanced uses cfg thresholds.
    static bool passesStrategyGate(Kind kind, float neural, EbAiConfig const& cfg);

    // Build proposals from scored (delay, neural) pairs already evaluated.
    // NovaParity: keep single best if >= 0.6.
    // Enhanced: all that pass configurable gate, sorted desc, capped at topK.
    static std::vector<Proposal> selectProposals(Kind kind,
                                                 std::vector<Proposal> const& evaluated,
                                                 EbAiConfig const& cfg, Counters& counters);

    // "% sure" style log token from sigmoid score (NovaParity diagnostics).
    static std::string percentSureLabel(float neural);

    static bool selfCheck(std::string& report);

private:
    EbNeuralSeedStrategy() = delete;
};

#endif
