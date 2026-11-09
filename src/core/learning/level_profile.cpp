#include "../../app/class_manager.hpp"
#include "level_profile.hpp"
#include "../../debug/assist_log.hpp"
#include "../level/level_content_key.hpp"
#include "assist_learning.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>

float EbLevelProfileImplementation::clamp01f(float value, float lowerBound, float upperBound) {
    return std::max(lowerBound, std::min(upperBound, value));
}

std::filesystem::path EbLevelProfileImplementation::levelsDir() {
    return geode::prelude::Mod::get()->getSaveDir() / "levels";
}

std::filesystem::path EbLevelProfileImplementation::profilePath(std::string const& key) {
    return EbLevelProfileImplementation::levelsDir() / (key + ".json");
}

EbLevelProfile& EbLevelProfile::get() {
    return EbClassManager::get().service<EbLevelProfile>();
}

int EbLevelProfile::spotBucket(float x) {
    if (!(!std::isfinite(x))) {
        return static_cast<int>(std::floor(x / SPOT_SIZE)) * static_cast<int>(SPOT_SIZE);
    }
    return 0;
}

std::string EbLevelProfile::makeKey(GJGameLevel* level) {
    return EbLevelContentKey::fullKey(level);
}

void EbLevelProfile::clear() {
    m_key.clear();
    m_loaded = false;
    m_dirty = false;
    m_deathsSinceSave = 0;
    for (auto& m : m_modes) {
        m.capMul = 1.f;
        m.spots.clear();
    }
}

int EbLevelProfile::spotCount() const {
    int count = 0;
    for (auto const& m : m_modes)
        count += static_cast<int>(m.spots.size());
    return count;
}

EbModeLearn& EbLevelProfile::mode(GameModeKind kind) {
    int index = static_cast<int>(kind);
    if (index < 0 || index >= EbStageConfig::MODE_COUNT)
        index = 0;
    return m_modes[index];
}

EbModeLearn const* EbLevelProfile::tryMode(GameModeKind kind) const {
    int index = static_cast<int>(kind);
    if (!(index < 0 || index >= EbStageConfig::MODE_COUNT)) {
        return &m_modes[index];
    }
    return nullptr;
}

EbSpotStats& EbLevelProfile::spot(GameModeKind kind, float x) {
    int const bucket = spotBucket(x);
    return mode(kind).spots[bucket];
}

EbSpotStats const* EbLevelProfile::trySpot(GameModeKind kind, float x) const {
    auto* m = tryMode(kind);
    if (!m)
        return nullptr;
    auto it = m->spots.find(spotBucket(x));
    if (!(it == m->spots.end())) {
        return &it->second;
    }
    return nullptr;
}

float EbLevelProfile::capMul(GameModeKind kind) const {
    auto* m = tryMode(kind);
    return m ? m->capMul : 1.f;
}

void EbLevelProfile::setCapMul(GameModeKind kind, float multiplier) {
    mode(kind).capMul = EbLevelProfileImplementation::clamp01f(multiplier, 0.5f, 2.f);
    m_dirty = true;
}

EbSpotStats EbLevelProfile::overlaySpot(GameModeKind kind, float x) const {
    if (auto* s = trySpot(kind, x))
        return *s;
    return {};
}

void EbLevelProfile::recomputeCapMul(GameModeKind kind) {
    auto& m = mode(kind);
    if (m.spots.empty()) {
        m.capMul = 1.f;
        return;
    }
    int pess = 0;
    int total = 0;
    for (auto const& [_, s] : m.spots) {
        pess += s.pessimism;
        total += std::max(1, s.n);
    }
    // Many false "you'll die" predictions → shorten horizon.
    float rate = static_cast<float>(pess) / static_cast<float>(total);
    if (rate > 0.25f)
        m.capMul = EbLevelProfileImplementation::clamp01f(0.6f, 0.5f, 2.f);
    else if (rate < 0.05f && total >= 20)
        m.capMul = EbLevelProfileImplementation::clamp01f(1.2f, 0.5f, 2.f);
    else
        m.capMul = EbLevelProfileImplementation::clamp01f(m.capMul, 0.5f, 2.f);
}

void EbLevelProfile::noteBias(GameModeKind kind, float x, float delta, char const* reason) {
    auto& s = spot(kind, x);
    s.bias = EbLevelProfileImplementation::clamp01f(s.bias + delta, -2.f, 2.f);
    s.n += 1;
    m_dirty = true;
    EbAssistLog::debug(fmt::format("LEARN: spot {} bias {:+.1f} ({})", spotBucket(x), s.bias,
                                 reason ? reason : ""));
}

void EbLevelProfile::noteTrust(GameModeKind kind, float x, float factor, char const* reason) {
    auto& s = spot(kind, x);
    s.trust = EbLevelProfileImplementation::clamp01f(s.trust * factor, 0.f, 1.f);
    s.n += 1;
    m_dirty = true;
    EbAssistLog::debug(fmt::format("LEARN: spot {} trust {:.2f} ({})", spotBucket(x), s.trust,
                                 reason ? reason : ""));
}

void EbLevelProfile::noteTrustUp(GameModeKind kind, float x, float amount, char const* reason) {
    auto& s = spot(kind, x);
    s.trust = EbLevelProfileImplementation::clamp01f(s.trust + amount, 0.f, 1.f);
    m_dirty = true;
    if (reason) {
        EbAssistLog::debug(
            fmt::format("LEARN: spot {} trust {:.2f} ({})", spotBucket(x), s.trust, reason));
    }
}

void EbLevelProfile::notePessimism(GameModeKind kind, float x) {
    auto& s = spot(kind, x);
    s.pessimism += 1;
    s.n += 1;
    if (s.pessimism >= 3) {
        // Local: shorten look-ahead via mode capMul
        mode(kind).capMul = 0.6f;
    }
    recomputeCapMul(kind);
    m_dirty = true;
    EbAssistLog::debug(fmt::format("LEARN: spot {} pessimism {} (sim said die, lived)", spotBucket(x),
                                 s.pessimism));
}

void EbLevelProfile::noteSaved(GameModeKind kind, float x) {
    auto& s = spot(kind, x);
    s.saved += 1;
    // Never reinforce trust from assist-inject "saves" (self-reinforcing loop).
    if (EbAssistLearning::allowTrustUpFromAssistInject()) {
        noteTrustUp(kind, x, 0.1f, "survived horizon");
    }
}

void EbLevelProfile::noteDiedAfterAssist(GameModeKind kind, float x) {
    auto& s = spot(kind, x);
    s.diedAfterAssist += 1;
    s.n += 1;
    m_dirty = true;
}

void EbLevelProfile::noteEarliness(GameModeKind kind, float x, int offset, char const* reason) {
    auto& s = spot(kind, x);
    int const clamped = std::max(-EbSpotStats::HIST_RADIUS, std::min(EbSpotStats::HIST_RADIUS, offset));
    int const bin = clamped + EbSpotStats::HIST_RADIUS;
    s.earlyHist[static_cast<size_t>(bin)] += 1.f;
    s.histN += 1;
    m_dirty = true;
    EbAssistLog::debug(fmt::format("LEARN: spot {} earliness {:+d} histN={} ({})", spotBucket(x),
                                 clamped, s.histN, reason ? reason : ""));
}

int EbLevelProfile::preferredDelayOffset(GameModeKind kind, float x) const {
    auto const* state = trySpot(kind, x);
    if (!state || state->histN < 3) {
        if (!state)
            return 0;
        // Fall back to classic ±2 bias.
        if (state->bias >= 0.f)
            return static_cast<int>(std::lround(state->bias));
        return static_cast<int>(std::lround(state->bias));
    }
    int bestBin = EbSpotStats::HIST_RADIUS;
    float bestW = -1.f;
    for (int index = 0; index < EbSpotStats::HIST_BINS; ++index) {
        float const width = state->earlyHist[static_cast<size_t>(index)] + 1.f; // Laplace
        if (width > bestW) {
            bestW = width;
            bestBin = index;
        }
    }
    return bestBin - EbSpotStats::HIST_RADIUS;
}

void EbLevelProfile::decayHistograms() {
    for (auto& m : m_modes) {
        for (auto& [_, s] : m.spots) {
            for (float& w : s.earlyHist)
                w *= 0.95f;
        }
    }
    m_dirty = true;
}

void EbLevelProfile::maybeAutosave() {
    ++m_deathsSinceSave;
    if (m_deathsSinceSave >= 5) {
        m_deathsSinceSave = 0;
        decayHistograms();
        // Do not sync-write during destroyPlayer — quit / complete persist.
        // m_dirty is already set by note* / decayHistograms.
    }
}

void EbLevelProfile::loadForLevel(GJGameLevel* level) {
    clear();
    m_key = makeKey(level);
    m_loaded = true;
    auto result = readFile();
    if (result.isErr()) {
        EbAssistLog::debug(fmt::format("LEARN: no profile for {} (fresh)", m_key));
    } else {
        EbAssistLog::debug(fmt::format("LEARN: loaded {} ({} spots)", m_key, spotCount()));
    }
}

void EbLevelProfile::resetCurrent() {
    if (!m_loaded)
        return;
    for (auto& m : m_modes) {
        m.capMul = 1.f;
        m.spots.clear();
    }
    m_dirty = true;
    save();
    EbAssistLog::info(fmt::format("LEARN: reset profile {}", m_key));
}

void EbLevelProfile::save() {
    if (!m_loaded || m_key.empty())
        return;
    if (!m_dirty)
        return;
    auto result = writeFile();
    if (result.isOk()) {
        m_dirty = false;
        EbAssistLog::debug(fmt::format("LEARN: profile saved ({} spots)", spotCount()));
    } else {
        EbAssistLog::debug(fmt::format("LEARN: save failed: {}", result.unwrapErr()));
    }
}

geode::Result<> EbLevelProfile::writeFile() const {
    auto directory = EbLevelProfileImplementation::levelsDir();
    (void)geode::utils::file::createDirectoryAll(directory);

    matjson::Value root = matjson::Value::object();
    root["key"] = m_key;
    root["version"] = 1;

    matjson::Value modes = matjson::Value::object();
    for (int index = 0; index < EbStageConfig::MODE_COUNT; ++index) {
        auto const kind = static_cast<GameModeKind>(index);
        auto const& m = m_modes[index];
        if (m.spots.empty() && std::abs(m.capMul - 1.f) < 0.01f)
            continue;

        matjson::Value mj = matjson::Value::object();
        mj["capMul"] = m.capMul;
        matjson::Value spots = matjson::Value::object();
        for (auto const& [bucket, s] : m.spots) {
            matjson::Value sj = matjson::Value::object();
            sj["bias"] = s.bias;
            sj["trust"] = s.trust;
            sj["pessimism"] = s.pessimism;
            sj["n"] = s.n;
            sj["saved"] = s.saved;
            sj["diedAfterAssist"] = s.diedAfterAssist;
            sj["histN"] = s.histN;
            if (s.histN > 0) {
                std::vector<matjson::Value> hist;
                hist.reserve(EbSpotStats::HIST_BINS);
                for (float width : s.earlyHist)
                    hist.push_back(width);
                sj["earlyHist"] = hist;
            }
            spots[std::to_string(bucket)] = sj;
        }
        mj["spots"] = spots;
        modes[EbStageConfig::kindName(kind)] = mj;
    }
    root["modes"] = modes;

    auto path = EbLevelProfileImplementation::profilePath(m_key);
    return geode::utils::file::writeToJson(path, root);
}

geode::Result<> EbLevelProfile::readFile() {
    auto path = EbLevelProfileImplementation::profilePath(m_key);
    auto result = geode::utils::file::readJson(path);
    if (result.isErr())
        return geode::Err(result.unwrapErr());

    auto root = result.unwrap();
    if (!root.contains("modes") || !root["modes"].isObject())
        return geode::Ok();

    auto& modes = root["modes"];
    for (int index = 0; index < EbStageConfig::MODE_COUNT; ++index) {
        auto const* name = EbStageConfig::kindName(static_cast<GameModeKind>(index));
        if (!modes.contains(name))
            continue;
        auto& mj = modes[name];
        auto& m = m_modes[index];
        if (mj.contains("capMul")) {
            m.capMul = EbLevelProfileImplementation::clamp01f(
                static_cast<float>(mj["capMul"].asDouble().unwrapOr(1.0)), 0.5f, 2.f);
        }
        if (mj.contains("spots") && mj["spots"].isObject()) {
            for (auto const& [k, sj] : mj["spots"]) {
                int bucket = 0;
                try {
                    bucket = std::stoi(k);
                } catch (...) {
                    continue;
                }
                EbSpotStats s;
                if (sj.contains("bias"))
                    s.bias = EbLevelProfileImplementation::clamp01f(
                        static_cast<float>(sj["bias"].asDouble().unwrapOr(0.0)), -2.f, 2.f);
                if (sj.contains("trust"))
                    s.trust = EbLevelProfileImplementation::clamp01f(
                        static_cast<float>(sj["trust"].asDouble().unwrapOr(1.0)), 0.f, 1.f);
                if (sj.contains("pessimism"))
                    s.pessimism = static_cast<int>(sj["pessimism"].asInt().unwrapOr(0));
                if (sj.contains("n"))
                    s.n = static_cast<int>(sj["n"].asInt().unwrapOr(0));
                if (sj.contains("saved"))
                    s.saved = static_cast<int>(sj["saved"].asInt().unwrapOr(0));
                if (sj.contains("diedAfterAssist"))
                    s.diedAfterAssist = static_cast<int>(sj["diedAfterAssist"].asInt().unwrapOr(0));
                if (sj.contains("histN"))
                    s.histN = static_cast<int>(sj["histN"].asInt().unwrapOr(0));
                if (sj.contains("earlyHist") && sj["earlyHist"].isArray()) {
                    size_t bi = 0;
                    for (auto const& cell : sj["earlyHist"]) {
                        if (bi >= EbSpotStats::HIST_BINS)
                            break;
                        s.earlyHist[bi] = static_cast<float>(cell.asDouble().unwrapOr(0.0));
                        ++bi;
                    }
                }
                m.spots[bucket] = s;
            }
        }
    }
    m_dirty = false;
    return geode::Ok();
}
