#include "ai_config.hpp"
#include "../../app/class_manager.hpp"
#include <Geode/Geode.hpp>
#include <cmath>
#include <fstream>
#include <matjson.hpp>

EbAiConfig& EbAiConfig::get() {
    return EbClassManager::get().service<EbAiConfig>();
}

char const* EbAiConfig::presetName() const {
    switch (preset) {
    case Preset::NovaReference:
        return "Nova Reference";
    case Preset::Balanced:
        return "Balanced";
    case Preset::Performance:
        return "Performance";
    case Preset::Accuracy:
        return "Accuracy";
    case Preset::Experimental:
        return "Experimental";
    default:
        return "Custom";
    }
}

void EbAiConfig::clampAll() {
    hidden1 = std::clamp(hidden1, 16, 512);
    hidden2 = std::clamp(hidden2, 8, 256);
    scoreCalibration = std::clamp(scoreCalibration, 0.25f, 4.0f);
    confidenceThreshold = std::clamp(confidenceThreshold, 0.05f, 0.95f);
    neuralEvalBudget = std::clamp(neuralEvalBudget, 4, 128);
    maxCandidates = std::clamp(maxCandidates, 1, 32);
    candidateWindow = std::clamp(candidateWindow, 8, 96);
    minDelay = std::clamp(minDelay, 0, 48);
    maxDelay = std::clamp(maxDelay, minDelay, 48);
    sampleStep = std::clamp(sampleStep, 1, 8);
    minCandidateConfidence = std::clamp(minCandidateConfidence, 0.0f, 1.0f);
    fallbackCandidateCount = std::clamp(fallbackCandidateCount, 0, 8);
    adaptivePercentile = std::clamp(adaptivePercentile, 0.5f, 0.95f);
    minRetained = std::clamp(minRetained, 1, 16);
    maxRetained = std::clamp(maxRetained, minRetained, 32);
    orbHintConfidence = std::clamp(orbHintConfidence, 0.05f, 0.9f);
    fallbackVerifyBudgetUs = std::clamp(fallbackVerifyBudgetUs, 200, 5000);
    maxSimTicks = std::clamp(maxSimTicks, 32, 800);
    maxSimCalls = std::clamp(maxSimCalls, 1, 64);
    simBudgetUs = std::clamp(simBudgetUs, 200, 8000);
    safetyMarginPx = std::clamp(safetyMarginPx, 0.0f, 12.0f);
    snapshotCacheSize = std::clamp(snapshotCacheSize, 2, 16);
    divergenceTolerancePx = std::clamp(divergenceTolerancePx, 1.0f, 64.0f);
    planningHorizon = std::clamp(planningHorizon, 16, 480);
    maxRouteDepth = std::clamp(maxRouteDepth, 1, 8);
    beamWidth = std::clamp(beamWidth, 1, 8);
    maxBranches = std::clamp(maxBranches, 1, 32);
    maxRouteCandidates = std::clamp(maxRouteCandidates, 1, 64);
    weightProgress = std::clamp(weightProgress, 0.0f, 4.0f);
    weightSafety = std::clamp(weightSafety, 0.0f, 4.0f);
    weightNeural = std::clamp(weightNeural, 0.0f, 4.0f);
    deathPenalty = std::clamp(deathPenalty, 0.0f, 8.0f);
    routeCacheSize = std::clamp(routeCacheSize, 1, 16);
    replanEveryTicks = std::clamp(replanEveryTicks, 1, 60);
    minVerifiedPrefix = std::clamp(minVerifiedPrefix, 1, maxRouteDepth);
    maxCommittedEvents = std::clamp(maxCommittedEvents, 1, maxRouteDepth);
    searchTimeoutUs = std::clamp(searchTimeoutUs, 400, 12000);
    cpuBudgetUsPerTick = std::clamp(cpuBudgetUsPerTick, 200, 8000);
    learningRate = std::clamp(learningRate, 0.0001f, 0.1f);
    batchSize = std::clamp(batchSize, 1, 128);
    replayCapacity = std::clamp(replayCapacity, 64, 65536);
    trainIntervalTicks = std::clamp(trainIntervalTicks, 30, 2400);
    trainStepsPerUpdate = std::clamp(trainStepsPerUpdate, 1, 32);
    teacherWeight = std::clamp(teacherWeight, 0.0f, 2.0f);
    simVerifiedWeight = std::clamp(simVerifiedWeight, 0.0f, 3.0f);
    survivedWeight = std::clamp(survivedWeight, 0.0f, 3.0f);
    discountGamma = std::clamp(discountGamma, 0.5f, 0.999f);
    targetTau = std::clamp(targetTau, 0.001f, 0.5f);
    gradClip = std::clamp(gradClip, 0.1f, 10.0f);
    checkpointInterval = std::clamp(checkpointInterval, 5, 500);
    trainCpuBudgetUs = std::clamp(trainCpuBudgetUs, 200, 8000);
    timeWarp = std::clamp(timeWarp, 1.0f, 32.0f);
    exploreRate = std::clamp(exploreRate, 0.0f, 0.25f);
    probeEveryTicks = std::clamp(probeEveryTicks, 4, 60);
    gapTicks = std::clamp(gapTicks, 4, 120);
    maxExecDriftPx = std::clamp(maxExecDriftPx, 2.0f, 80.0f);
    maxAiCpuUsPerTick = std::clamp(maxAiCpuUsPerTick, 500, 12000);
    diagnosticSampleEvery = std::clamp(diagnosticSampleEvery, 1, 240);
    if (!experimentalBackends && backend == Backend::DualHeadLegacy)
        backend = Backend::CandidateSigmoid;
    if (!experimentalBackends && policy == Policy::NeuralOnlyUnsafe)
        policy = Policy::NeuralThenSim;
}

void EbAiConfig::applyPreset(Preset value) {
    resetAll();
    preset = value;
    switch (value) {
    case Preset::NovaReference:
        backend = Backend::CandidateSigmoid;
        policy = Policy::NeuralThenSim;
        delayEncoding = DelayEncoding::Log1p;
        confidenceThreshold = 0.6f;
        gatingEnabled = true;
        adaptiveThreshold = false;
        maxCandidates = 12;
        sampleStep = 1;
        maxRouteDepth = 6;
        beamWidth = 4;
        simBudgetUs = 2000;
        searchTimeoutUs = 4000;
        generateRelease = true;
        break;
    case Preset::Performance:
        maxCandidates = 4;
        maxRouteDepth = 2;
        beamWidth = 2;
        simBudgetUs = 800;
        searchTimeoutUs = 1200;
        neuralEvalBudget = 12;
        cpuBudgetUsPerTick = 1000;
        maxAiCpuUsPerTick = 1500;
        break;
    case Preset::Accuracy:
        maxCandidates = 16;
        maxRouteDepth = 6;
        beamWidth = 5;
        maxBranches = 16;
        simBudgetUs = 3500;
        searchTimeoutUs = 6000;
        maxSimTicks = 600;
        strictVerify = true;
        break;
    case Preset::Experimental:
        experimentalBackends = true;
        backend = Backend::DualHeadLegacy;
        adaptiveThreshold = true;
        debugLogging = true;
        maxRouteDepth = 8;
        break;
    case Preset::Balanced:
    default:
        // defaults from resetAll already Balanced-ish
        break;
    }
    clampAll();
    preset = value;
}

void EbAiConfig::resetAll() {
    *this = EbAiConfig{};
    preset = Preset::Balanced;
    clampAll();
}

void EbAiConfig::resetCategory(char const* category) {
    if (!category)
        return;
    std::string const name = category;
    EbAiConfig fresh;
    fresh.clampAll();
    if (name == "general") {
        enabled = fresh.enabled;
        backend = fresh.backend;
        autoplay = fresh.autoplay;
        policy = fresh.policy;
        preferAiFirst = fresh.preferAiFirst;
    } else if (name == "neural") {
        hidden1 = fresh.hidden1;
        hidden2 = fresh.hidden2;
        delayEncoding = fresh.delayEncoding;
        scoreCalibration = fresh.scoreCalibration;
        confidenceThreshold = fresh.confidenceThreshold;
        neuralEvalBudget = fresh.neuralEvalBudget;
        freezeWeights = fresh.freezeWeights;
    } else if (name == "candidates") {
        maxCandidates = fresh.maxCandidates;
        candidateWindow = fresh.candidateWindow;
        minDelay = fresh.minDelay;
        maxDelay = fresh.maxDelay;
        sampleStep = fresh.sampleStep;
        adaptiveSampling = fresh.adaptiveSampling;
        generatePress = fresh.generatePress;
        generateRelease = fresh.generateRelease;
        orbCandidates = fresh.orbCandidates;
        minCandidateConfidence = fresh.minCandidateConfidence;
        fallbackCandidateCount = fresh.fallbackCandidateCount;
    } else if (name == "gating") {
        gatingEnabled = fresh.gatingEnabled;
        adaptiveThreshold = fresh.adaptiveThreshold;
        adaptivePercentile = fresh.adaptivePercentile;
        minRetained = fresh.minRetained;
        maxRetained = fresh.maxRetained;
        lowConfidenceFallback = fresh.lowConfidenceFallback;
        orbHintConfidence = fresh.orbHintConfidence;
        fallbackVerifyBudgetUs = fresh.fallbackVerifyBudgetUs;
    } else if (name == "sim") {
        simulationEnabled = fresh.simulationEnabled;
        maxSimTicks = fresh.maxSimTicks;
        maxSimCalls = fresh.maxSimCalls;
        simBudgetUs = fresh.simBudgetUs;
        safetyMarginPx = fresh.safetyMarginPx;
        snapshotCacheSize = fresh.snapshotCacheSize;
        reuseSnapshots = fresh.reuseSnapshots;
        rejectUnknown = fresh.rejectUnknown;
        divergenceTolerancePx = fresh.divergenceTolerancePx;
        strictVerify = fresh.strictVerify;
    } else if (name == "route") {
        planningHorizon = fresh.planningHorizon;
        maxRouteDepth = fresh.maxRouteDepth;
        beamWidth = fresh.beamWidth;
        maxBranches = fresh.maxBranches;
        maxRouteCandidates = fresh.maxRouteCandidates;
        searchAlgo = fresh.searchAlgo;
        weightProgress = fresh.weightProgress;
        weightSafety = fresh.weightSafety;
        weightNeural = fresh.weightNeural;
        deathPenalty = fresh.deathPenalty;
        pruneDeadBranches = fresh.pruneDeadBranches;
        routeCacheSize = fresh.routeCacheSize;
        replanEveryTicks = fresh.replanEveryTicks;
        minVerifiedPrefix = fresh.minVerifiedPrefix;
        maxCommittedEvents = fresh.maxCommittedEvents;
        searchTimeoutUs = fresh.searchTimeoutUs;
        cpuBudgetUsPerTick = fresh.cpuBudgetUsPerTick;
    } else if (name == "train") {
        trainingEnabled = fresh.trainingEnabled;
        persistSessions = fresh.persistSessions;
        learningRate = fresh.learningRate;
        batchSize = fresh.batchSize;
        replayCapacity = fresh.replayCapacity;
        trainIntervalTicks = fresh.trainIntervalTicks;
        trainStepsPerUpdate = fresh.trainStepsPerUpdate;
        teacherWeight = fresh.teacherWeight;
        simVerifiedWeight = fresh.simVerifiedWeight;
        survivedWeight = fresh.survivedWeight;
        hardNegatives = fresh.hardNegatives;
        discountGamma = fresh.discountGamma;
        targetTau = fresh.targetTau;
        gradClip = fresh.gradClip;
        checkpointInterval = fresh.checkpointInterval;
        trainCpuBudgetUs = fresh.trainCpuBudgetUs;
    } else if (name == "autoplay") {
        restartOnDeath = fresh.restartOnDeath;
        timeWarp = fresh.timeWarp;
        exploreRate = fresh.exploreRate;
        probeEveryTicks = fresh.probeEveryTicks;
        gapTicks = fresh.gapTicks;
        replanOnDivergence = fresh.replanOnDivergence;
        maxExecDriftPx = fresh.maxExecDriftPx;
    } else if (name == "perf") {
        maxAiCpuUsPerTick = fresh.maxAiCpuUsPerTick;
        backgroundTrain = fresh.backgroundTrain;
        diagnosticSampleEvery = fresh.diagnosticSampleEvery;
    } else if (name == "viz") {
        showOverlay = fresh.showOverlay;
        showConfidence = fresh.showConfidence;
        showCandidates = fresh.showCandidates;
        showRoute = fresh.showRoute;
        showRejected = fresh.showRejected;
        showSnapshots = fresh.showSnapshots;
        showSafety = fresh.showSafety;
        showTrainLoss = fresh.showTrainLoss;
        showCpu = fresh.showCpu;
    }
    markCustom();
    clampAll();
}

static matjson::Value dumpConfig(EbAiConfig const& c) {
    auto o = matjson::Value::object();
    o["version"] = EbAiConfig::kConfigVersion;
    o["preset"] = static_cast<int>(c.preset);
    o["enabled"] = c.enabled;
    o["backend"] = static_cast<int>(c.backend);
    o["autoplay"] = c.autoplay;
    o["policy"] = static_cast<int>(c.policy);
    o["preferAiFirst"] = c.preferAiFirst;
    o["hidden1"] = c.hidden1;
    o["hidden2"] = c.hidden2;
    o["delayEncoding"] = static_cast<int>(c.delayEncoding);
    o["scoreCalibration"] = c.scoreCalibration;
    o["confidenceThreshold"] = c.confidenceThreshold;
    o["neuralEvalBudget"] = c.neuralEvalBudget;
    o["freezeWeights"] = c.freezeWeights;
    o["maxCandidates"] = c.maxCandidates;
    o["candidateWindow"] = c.candidateWindow;
    o["minDelay"] = c.minDelay;
    o["maxDelay"] = c.maxDelay;
    o["sampleStep"] = c.sampleStep;
    o["adaptiveSampling"] = c.adaptiveSampling;
    o["generatePress"] = c.generatePress;
    o["generateRelease"] = c.generateRelease;
    o["orbCandidates"] = c.orbCandidates;
    o["minCandidateConfidence"] = c.minCandidateConfidence;
    o["fallbackCandidateCount"] = c.fallbackCandidateCount;
    o["gatingEnabled"] = c.gatingEnabled;
    o["adaptiveThreshold"] = c.adaptiveThreshold;
    o["adaptivePercentile"] = c.adaptivePercentile;
    o["minRetained"] = c.minRetained;
    o["maxRetained"] = c.maxRetained;
    o["lowConfidenceFallback"] = c.lowConfidenceFallback;
    o["orbHintConfidence"] = c.orbHintConfidence;
    o["fallbackVerifyBudgetUs"] = c.fallbackVerifyBudgetUs;
    o["simulationEnabled"] = c.simulationEnabled;
    o["maxSimTicks"] = c.maxSimTicks;
    o["maxSimCalls"] = c.maxSimCalls;
    o["simBudgetUs"] = c.simBudgetUs;
    o["safetyMarginPx"] = c.safetyMarginPx;
    o["snapshotCacheSize"] = c.snapshotCacheSize;
    o["reuseSnapshots"] = c.reuseSnapshots;
    o["rejectUnknown"] = c.rejectUnknown;
    o["divergenceTolerancePx"] = c.divergenceTolerancePx;
    o["strictVerify"] = c.strictVerify;
    o["planningHorizon"] = c.planningHorizon;
    o["maxRouteDepth"] = c.maxRouteDepth;
    o["beamWidth"] = c.beamWidth;
    o["maxBranches"] = c.maxBranches;
    o["maxRouteCandidates"] = c.maxRouteCandidates;
    o["searchAlgo"] = static_cast<int>(c.searchAlgo);
    o["weightProgress"] = c.weightProgress;
    o["weightSafety"] = c.weightSafety;
    o["weightNeural"] = c.weightNeural;
    o["deathPenalty"] = c.deathPenalty;
    o["pruneDeadBranches"] = c.pruneDeadBranches;
    o["routeCacheSize"] = c.routeCacheSize;
    o["replanEveryTicks"] = c.replanEveryTicks;
    o["minVerifiedPrefix"] = c.minVerifiedPrefix;
    o["maxCommittedEvents"] = c.maxCommittedEvents;
    o["searchTimeoutUs"] = c.searchTimeoutUs;
    o["cpuBudgetUsPerTick"] = c.cpuBudgetUsPerTick;
    o["trainingEnabled"] = c.trainingEnabled;
    o["persistSessions"] = c.persistSessions;
    o["learningRate"] = c.learningRate;
    o["batchSize"] = c.batchSize;
    o["replayCapacity"] = c.replayCapacity;
    o["trainIntervalTicks"] = c.trainIntervalTicks;
    o["trainStepsPerUpdate"] = c.trainStepsPerUpdate;
    o["teacherWeight"] = c.teacherWeight;
    o["simVerifiedWeight"] = c.simVerifiedWeight;
    o["survivedWeight"] = c.survivedWeight;
    o["hardNegatives"] = c.hardNegatives;
    o["discountGamma"] = c.discountGamma;
    o["targetTau"] = c.targetTau;
    o["gradClip"] = c.gradClip;
    o["checkpointInterval"] = c.checkpointInterval;
    o["trainCpuBudgetUs"] = c.trainCpuBudgetUs;
    o["restartOnDeath"] = c.restartOnDeath;
    o["timeWarp"] = c.timeWarp;
    o["exploreRate"] = c.exploreRate;
    o["probeEveryTicks"] = c.probeEveryTicks;
    o["gapTicks"] = c.gapTicks;
    o["replanOnDivergence"] = c.replanOnDivergence;
    o["maxExecDriftPx"] = c.maxExecDriftPx;
    o["maxAiCpuUsPerTick"] = c.maxAiCpuUsPerTick;
    o["backgroundTrain"] = c.backgroundTrain;
    o["diagnosticSampleEvery"] = c.diagnosticSampleEvery;
    o["showOverlay"] = c.showOverlay;
    o["showConfidence"] = c.showConfidence;
    o["showCandidates"] = c.showCandidates;
    o["showRoute"] = c.showRoute;
    o["showRejected"] = c.showRejected;
    o["showSnapshots"] = c.showSnapshots;
    o["showSafety"] = c.showSafety;
    o["showTrainLoss"] = c.showTrainLoss;
    o["showCpu"] = c.showCpu;
    o["experimentalBackends"] = c.experimentalBackends;
    o["deterministicBenchmark"] = c.deterministicBenchmark;
    o["debugLogging"] = c.debugLogging;
    return o;
}

static void loadField(matjson::Value const& o, char const* key, bool& out) {
    if (o.contains(key) && o[key].isBool())
        out = o[key].asBool().unwrapOr(out);
}
static void loadField(matjson::Value const& o, char const* key, int& out) {
    if (o.contains(key) && o[key].isNumber())
        out = static_cast<int>(o[key].asDouble().unwrapOr(out));
}
static void loadField(matjson::Value const& o, char const* key, float& out) {
    if (o.contains(key) && o[key].isNumber())
        out = static_cast<float>(o[key].asDouble().unwrapOr(out));
}

void EbAiConfig::load() {
    auto* mod = geode::Mod::get();
    if (!mod)
        return;
    auto path = mod->getSaveDir() / "ai" / "config-v1.json";
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        applyPreset(Preset::Balanced);
        return;
    }
    std::ifstream input(path);
    if (!input)
        return;
    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    auto parsed = matjson::parse(text);
    if (parsed.isErr())
        return;
    matjson::Value root = parsed.unwrap();
    if (!root.isObject())
        return;
    loadField(root, "enabled", enabled);
    loadField(root, "autoplay", autoplay);
    loadField(root, "preferAiFirst", preferAiFirst);
    loadField(root, "hidden1", hidden1);
    loadField(root, "hidden2", hidden2);
    loadField(root, "scoreCalibration", scoreCalibration);
    loadField(root, "confidenceThreshold", confidenceThreshold);
    loadField(root, "neuralEvalBudget", neuralEvalBudget);
    loadField(root, "freezeWeights", freezeWeights);
    loadField(root, "maxCandidates", maxCandidates);
    loadField(root, "candidateWindow", candidateWindow);
    loadField(root, "minDelay", minDelay);
    loadField(root, "maxDelay", maxDelay);
    loadField(root, "sampleStep", sampleStep);
    loadField(root, "adaptiveSampling", adaptiveSampling);
    loadField(root, "generatePress", generatePress);
    loadField(root, "generateRelease", generateRelease);
    loadField(root, "orbCandidates", orbCandidates);
    loadField(root, "minCandidateConfidence", minCandidateConfidence);
    loadField(root, "fallbackCandidateCount", fallbackCandidateCount);
    loadField(root, "gatingEnabled", gatingEnabled);
    loadField(root, "adaptiveThreshold", adaptiveThreshold);
    loadField(root, "adaptivePercentile", adaptivePercentile);
    loadField(root, "minRetained", minRetained);
    loadField(root, "maxRetained", maxRetained);
    loadField(root, "lowConfidenceFallback", lowConfidenceFallback);
    loadField(root, "orbHintConfidence", orbHintConfidence);
    loadField(root, "fallbackVerifyBudgetUs", fallbackVerifyBudgetUs);
    loadField(root, "simulationEnabled", simulationEnabled);
    loadField(root, "maxSimTicks", maxSimTicks);
    loadField(root, "maxSimCalls", maxSimCalls);
    loadField(root, "simBudgetUs", simBudgetUs);
    loadField(root, "safetyMarginPx", safetyMarginPx);
    loadField(root, "snapshotCacheSize", snapshotCacheSize);
    loadField(root, "reuseSnapshots", reuseSnapshots);
    loadField(root, "rejectUnknown", rejectUnknown);
    loadField(root, "divergenceTolerancePx", divergenceTolerancePx);
    loadField(root, "strictVerify", strictVerify);
    loadField(root, "planningHorizon", planningHorizon);
    loadField(root, "maxRouteDepth", maxRouteDepth);
    loadField(root, "beamWidth", beamWidth);
    loadField(root, "maxBranches", maxBranches);
    loadField(root, "maxRouteCandidates", maxRouteCandidates);
    loadField(root, "weightProgress", weightProgress);
    loadField(root, "weightSafety", weightSafety);
    loadField(root, "weightNeural", weightNeural);
    loadField(root, "deathPenalty", deathPenalty);
    loadField(root, "pruneDeadBranches", pruneDeadBranches);
    loadField(root, "routeCacheSize", routeCacheSize);
    loadField(root, "replanEveryTicks", replanEveryTicks);
    loadField(root, "minVerifiedPrefix", minVerifiedPrefix);
    loadField(root, "maxCommittedEvents", maxCommittedEvents);
    loadField(root, "searchTimeoutUs", searchTimeoutUs);
    loadField(root, "cpuBudgetUsPerTick", cpuBudgetUsPerTick);
    loadField(root, "trainingEnabled", trainingEnabled);
    loadField(root, "persistSessions", persistSessions);
    loadField(root, "learningRate", learningRate);
    loadField(root, "batchSize", batchSize);
    loadField(root, "replayCapacity", replayCapacity);
    loadField(root, "trainIntervalTicks", trainIntervalTicks);
    loadField(root, "trainStepsPerUpdate", trainStepsPerUpdate);
    loadField(root, "teacherWeight", teacherWeight);
    loadField(root, "simVerifiedWeight", simVerifiedWeight);
    loadField(root, "survivedWeight", survivedWeight);
    loadField(root, "hardNegatives", hardNegatives);
    loadField(root, "discountGamma", discountGamma);
    loadField(root, "targetTau", targetTau);
    loadField(root, "gradClip", gradClip);
    loadField(root, "checkpointInterval", checkpointInterval);
    loadField(root, "trainCpuBudgetUs", trainCpuBudgetUs);
    loadField(root, "restartOnDeath", restartOnDeath);
    loadField(root, "timeWarp", timeWarp);
    loadField(root, "exploreRate", exploreRate);
    loadField(root, "probeEveryTicks", probeEveryTicks);
    loadField(root, "gapTicks", gapTicks);
    loadField(root, "replanOnDivergence", replanOnDivergence);
    loadField(root, "maxExecDriftPx", maxExecDriftPx);
    loadField(root, "maxAiCpuUsPerTick", maxAiCpuUsPerTick);
    loadField(root, "backgroundTrain", backgroundTrain);
    loadField(root, "diagnosticSampleEvery", diagnosticSampleEvery);
    loadField(root, "showOverlay", showOverlay);
    loadField(root, "showConfidence", showConfidence);
    loadField(root, "showCandidates", showCandidates);
    loadField(root, "showRoute", showRoute);
    loadField(root, "showRejected", showRejected);
    loadField(root, "showSnapshots", showSnapshots);
    loadField(root, "showSafety", showSafety);
    loadField(root, "showTrainLoss", showTrainLoss);
    loadField(root, "showCpu", showCpu);
    loadField(root, "experimentalBackends", experimentalBackends);
    loadField(root, "deterministicBenchmark", deterministicBenchmark);
    loadField(root, "debugLogging", debugLogging);
    int presetInt = static_cast<int>(preset);
    int backendInt = static_cast<int>(backend);
    int policyInt = static_cast<int>(policy);
    int encInt = static_cast<int>(delayEncoding);
    int algoInt = static_cast<int>(searchAlgo);
    loadField(root, "preset", presetInt);
    loadField(root, "backend", backendInt);
    loadField(root, "policy", policyInt);
    loadField(root, "delayEncoding", encInt);
    loadField(root, "searchAlgo", algoInt);
    preset = static_cast<Preset>(std::clamp(presetInt, 0, 5));
    backend = static_cast<Backend>(std::clamp(backendInt, 0, 1));
    policy = static_cast<Policy>(std::clamp(policyInt, 0, 3));
    delayEncoding = static_cast<DelayEncoding>(std::clamp(encInt, 0, 1));
    searchAlgo = static_cast<SearchAlgo>(std::clamp(algoInt, 0, 1));
    clampAll();
}

void EbAiConfig::save() const {
    auto* mod = geode::Mod::get();
    if (!mod)
        return;
    auto dir = mod->getSaveDir() / "ai";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    auto path = dir / "config-v1.json";
    auto tmp = dir / "config-v1.json.tmp";
    std::ofstream output(tmp, std::ios::trunc);
    if (!output)
        return;
    output << dumpConfig(*this).dump(2);
    output.close();
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmp, path, ec);
    }
}

bool EbAiConfig::selfCheck(std::string& report) {
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };
    EbAiConfig cfg;
    cfg.applyPreset(Preset::NovaReference);
    expect(cfg.confidenceThreshold >= 0.59f && cfg.confidenceThreshold <= 0.61f, "nova-thresh");
    expect(cfg.delayEncoding == DelayEncoding::Log1p, "nova-log1p");
    expect(cfg.maxRouteDepth >= 4, "nova-depth");
    cfg.applyPreset(Preset::Performance);
    expect(cfg.maxRouteDepth <= 2, "perf-shallow");
    cfg.resetAll();
    expect(cfg.preset == Preset::Balanced, "reset-balanced");
    if (fails == 0) {
        report = "AiConfig OK";
        return true;
    }
    report = std::string("AiConfig fails: ") + report;
    return false;
}
