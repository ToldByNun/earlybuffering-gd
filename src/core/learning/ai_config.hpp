#ifndef AI_CONFIG_HPP
#define AI_CONFIG_HPP

#include <algorithm>
#include <cstdint>
#include <string>

// Authoritative AI Mode configuration. Desktop UI + runtime + training all read this.
// Persisted as saveDir/ai/config-v1.json (not scattered Mod setting keys).
class EbAiConfig {
public:
    static constexpr int kConfigVersion = 1;

    enum class Backend : std::uint8_t {
        CandidateSigmoid = 0, // Nova-style single sigmoid (primary)
        DualHeadLegacy = 1,   // Experimental 49-way + confidence
    };
    enum class Policy : std::uint8_t {
        NeuralThenSim = 0,
        SimFirst = 1,
        HybridTeacher = 2,
        NeuralOnlyUnsafe = 3, // never default; gated off unless experimental
    };
    enum class DelayEncoding : std::uint8_t {
        LinearMax = 0, // delay / kMaxDelay
        Log1p = 1,     // log1p(delay) * 0.25 (Nova-confirmed)
    };
    enum class SearchAlgo : std::uint8_t {
        GreedyBeam = 0,
        DepthFirst = 1,
    };
    // AP-NL5 / DR1: dual neural seed strategies. Enhanced is the product default;
    // NovaParity is compare/research only (hard 0.6 + duty + subsample).
    enum class SeedStrategy : std::uint8_t {
        Enhanced = 0,
        NovaParity = 1,
    };
    enum class Preset : std::uint8_t {
        NovaReference = 0,
        Balanced = 1,
        Performance = 2,
        Accuracy = 3,
        Experimental = 4,
        Custom = 5,
    };

    static EbAiConfig& get();

    void load();
    void save() const;
    void applyPreset(Preset preset);
    void resetCategory(char const* category); // "general"|"neural"|"candidates"|"gating"|"sim"|"route"|"train"|"autoplay"|"perf"|"viz"
    void resetAll();
    Preset activePreset() const {
        return preset;
    }
    char const* presetName() const;
    void markCustom() {
        preset = Preset::Custom;
    }

    // --- General ---
    bool enabled = true;
    Backend backend = Backend::CandidateSigmoid;
    bool autoplay = false;
    Policy policy = Policy::NeuralThenSim;
    Preset preset = Preset::Balanced;
    bool preferAiFirst = true;

    // --- Neural ---
    int hidden1 = 128;
    int hidden2 = 64;
    DelayEncoding delayEncoding = DelayEncoding::LinearMax;
    float scoreCalibration = 1.0f;
    float confidenceThreshold = 0.6f; // Enhanced floor; NovaParity ignores (hard 0.6)
    int neuralEvalBudget = 32;
    bool freezeWeights = false;
    SeedStrategy seedStrategy = SeedStrategy::Enhanced;
    // NovaParity duty: seed only when planTick % period == phase (period>=1).
    int seedDutyPeriod = 1;
    int seedDutyPhase = 0;

    // --- Candidates ---
    int maxCandidates = 8;
    int candidateWindow = 48;
    int minDelay = 1;
    int maxDelay = 48;
    int sampleStep = 1;
    bool adaptiveSampling = true;
    bool generatePress = true;
    bool generateRelease = true;
    bool orbCandidates = true;
    float minCandidateConfidence = 0.0f;
    int fallbackCandidateCount = 2;

    // --- Gating ---
    bool gatingEnabled = true;
    bool adaptiveThreshold = false;
    float adaptivePercentile = 0.75f;
    int minRetained = 1;
    int maxRetained = 6;
    bool lowConfidenceFallback = true;
    float orbHintConfidence = 0.35f;
    int fallbackVerifyBudgetUs = 800;

    // --- Shadow ---
    bool simulationEnabled = true;
    int maxSimTicks = 240;
    int maxSimCalls = 8;
    // Nova-like soft walls: keep per-verify + per-plan budgets under a frame slice.
    int simBudgetUs = 1000;
    float safetyMarginPx = 2.0f;
    int snapshotCacheSize = 8;
    bool reuseSnapshots = true;
    bool rejectUnknown = true;
    float divergenceTolerancePx = 12.0f;
    bool strictVerify = true;

    // --- Route ---
    int planningHorizon = 80;
    int maxRouteDepth = 3;
    int beamWidth = 2;
    int maxBranches = 6;
    int maxRouteCandidates = 12;
    SearchAlgo searchAlgo = SearchAlgo::GreedyBeam;
    float weightProgress = 1.0f;
    float weightSafety = 1.0f;
    float weightNeural = 1.0f;
    float deathPenalty = 2.0f;
    bool pruneDeadBranches = true;
    int routeCacheSize = 4;
    int replanEveryTicks = 12;
    int minVerifiedPrefix = 1;
    int maxCommittedEvents = 4;
    int searchTimeoutUs = 1500;
    int cpuBudgetUsPerTick = 1200;

    // --- Training ---
    bool trainingEnabled = true;
    // When on, each attempt boundary writes new IL/RL samples to saveDir/ai/sessions/*.ebns.
    bool persistSessions = false;
    float learningRate = 0.01f;
    int batchSize = 16;
    int replayCapacity = 4096;
    int trainIntervalTicks = 240;
    int trainStepsPerUpdate = 4;
    float teacherWeight = 0.25f;
    float simVerifiedWeight = 1.0f;
    float survivedWeight = 1.35f;
    bool hardNegatives = true;
    float discountGamma = 0.95f;
    float targetTau = 0.05f;
    float gradClip = 1.0f;
    int checkpointInterval = 50;
    int trainCpuBudgetUs = 1500;

    // --- Autoplay execution ---
    bool restartOnDeath = true;
    float timeWarp = 8.0f;
    float exploreRate = 0.08f;
    int probeEveryTicks = 14;
    int gapTicks = 28;
    bool replanOnDivergence = true;
    float maxExecDriftPx = 20.0f;
    // AP-NL6: Autoplay-only knobs (never soft-lock Live windows/budgets).
    int maxRouteQueue = 2;     // early-plan queue depth (0 = active route only)
    int earlyPlanTicks = 8;    // enqueue next plan when remaining route delay ≤ this
    int maxAutoplayCps = 12;   // soft CPS cap over 240 physics ticks
    bool memoryReuseRoute = true;
    int memoryHoldTicks = 48;  // reuse last SV route if replan fails within window

    // --- Performance ---
    int maxAiCpuUsPerTick = 2000;
    bool backgroundTrain = false;
    int diagnosticSampleEvery = 30;

    // --- Visualization ---
    bool showOverlay = false;
    bool showConfidence = true;
    bool showCandidates = true;
    bool showRoute = true;
    bool showRejected = false;
    bool showSnapshots = false;
    bool showSafety = false;
    bool showTrainLoss = false;
    bool showCpu = false;

    // --- Experimental ---
    bool experimentalBackends = false;
    bool deterministicBenchmark = false;
    bool debugLogging = false;

    static bool selfCheck(std::string& report);

private:
    friend class EbServiceStore;
    EbAiConfig() = default;
    void clampAll();
};

#endif
