#include "LevelProfile.hpp"
#include "AssistLearning.hpp"
#include "LevelContentKey.hpp"
#include "../debug/AssistLog.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>

using namespace geode::prelude;



static float clamp01f(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

static std::filesystem::path levelsDir() {
    return Mod::get()->getSaveDir() / "levels";
}

static std::filesystem::path profilePath(std::string const& key) {
    return levelsDir() / (key + ".json");
}



LevelProfile& LevelProfile::get() {
    static LevelProfile inst;
    return inst;
}

int LevelProfile::spotBucket(float x) {
    if (!std::isfinite(x)) return 0;
    return static_cast<int>(std::floor(x / SPOT_SIZE)) * static_cast<int>(SPOT_SIZE);
}

std::string LevelProfile::makeKey(GJGameLevel* level) {
    return LevelContentKey::fullKey(level);
}

void LevelProfile::clear() {
    m_key.clear();
    m_loaded = false;
    m_dirty = false;
    m_deathsSinceSave = 0;
    for (auto& m : m_modes) {
        m.capMul = 1.f;
        m.spots.clear();
    }
}

int LevelProfile::spotCount() const {
    int n = 0;
    for (auto const& m : m_modes) n += static_cast<int>(m.spots.size());
    return n;
}

ModeLearn& LevelProfile::mode(GameModeKind kind) {
    int i = static_cast<int>(kind);
    if (i < 0 || i >= StageConfig::MODE_COUNT) i = 0;
    return m_modes[i];
}

ModeLearn const* LevelProfile::tryMode(GameModeKind kind) const {
    int i = static_cast<int>(kind);
    if (i < 0 || i >= StageConfig::MODE_COUNT) return nullptr;
    return &m_modes[i];
}

SpotStats& LevelProfile::spot(GameModeKind kind, float x) {
    int const bucket = spotBucket(x);
    return mode(kind).spots[bucket];
}

SpotStats const* LevelProfile::trySpot(GameModeKind kind, float x) const {
    auto* m = tryMode(kind);
    if (!m) return nullptr;
    auto it = m->spots.find(spotBucket(x));
    if (it == m->spots.end()) return nullptr;
    return &it->second;
}

float LevelProfile::capMul(GameModeKind kind) const {
    auto* m = tryMode(kind);
    return m ? m->capMul : 1.f;
}

void LevelProfile::setCapMul(GameModeKind kind, float mul) {
    mode(kind).capMul = clamp01f(mul, 0.5f, 2.f);
    m_dirty = true;
}

SpotStats LevelProfile::overlaySpot(GameModeKind kind, float x) const {
    if (auto* s = trySpot(kind, x)) return *s;
    return {};
}

void LevelProfile::recomputeCapMul(GameModeKind kind) {
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
    if (rate > 0.25f) m.capMul = clamp01f(0.6f, 0.5f, 2.f);
    else if (rate < 0.05f && total >= 20) m.capMul = clamp01f(1.2f, 0.5f, 2.f);
    else m.capMul = clamp01f(m.capMul, 0.5f, 2.f);
}

void LevelProfile::noteBias(GameModeKind kind, float x, float delta, char const* reason) {
    auto& s = spot(kind, x);
    s.bias = clamp01f(s.bias + delta, -2.f, 2.f);
    s.n += 1;
    m_dirty = true;
    AssistLog::debug(fmt::format(
        "LEARN: spot {} bias {:+.1f} ({})",
        spotBucket(x), s.bias, reason ? reason : ""
    ));
}

void LevelProfile::noteTrust(GameModeKind kind, float x, float factor, char const* reason) {
    auto& s = spot(kind, x);
    s.trust = clamp01f(s.trust * factor, 0.f, 1.f);
    s.n += 1;
    m_dirty = true;
    AssistLog::debug(fmt::format(
        "LEARN: spot {} trust {:.2f} ({})",
        spotBucket(x), s.trust, reason ? reason : ""
    ));
}

void LevelProfile::noteTrustUp(GameModeKind kind, float x, float amount, char const* reason) {
    auto& s = spot(kind, x);
    s.trust = clamp01f(s.trust + amount, 0.f, 1.f);
    m_dirty = true;
    if (reason) {
        AssistLog::debug(fmt::format(
            "LEARN: spot {} trust {:.2f} ({})",
            spotBucket(x), s.trust, reason
        ));
    }
}

void LevelProfile::notePessimism(GameModeKind kind, float x) {
    auto& s = spot(kind, x);
    s.pessimism += 1;
    s.n += 1;
    if (s.pessimism >= 3) {
        // Local: shorten look-ahead via mode capMul
        mode(kind).capMul = 0.6f;
    }
    recomputeCapMul(kind);
    m_dirty = true;
    AssistLog::debug(fmt::format(
        "LEARN: spot {} pessimism {} (sim said die, lived)",
        spotBucket(x), s.pessimism
    ));
}

void LevelProfile::noteSaved(GameModeKind kind, float x) {
    auto& s = spot(kind, x);
    s.saved += 1;
    // Never reinforce trust from assist-inject "saves" (self-reinforcing loop).
    if (AssistLearning::allowTrustUpFromAssistInject()) {
        noteTrustUp(kind, x, 0.1f, "survived horizon");
    }
}

void LevelProfile::noteDiedAfterAssist(GameModeKind kind, float x) {
    auto& s = spot(kind, x);
    s.diedAfterAssist += 1;
    s.n += 1;
    m_dirty = true;
}

void LevelProfile::maybeAutosave() {
    ++m_deathsSinceSave;
    if (m_deathsSinceSave >= 5) {
        m_deathsSinceSave = 0;
        save();
    }
}

void LevelProfile::loadForLevel(GJGameLevel* level) {
    clear();
    m_key = makeKey(level);
    m_loaded = true;
    auto res = readFile();
    if (res.isErr()) {
        AssistLog::debug(fmt::format("LEARN: no profile for {} (fresh)", m_key));
    }
    else {
        AssistLog::debug(fmt::format(
            "LEARN: loaded {} ({} spots)", m_key, spotCount()
        ));
    }
}

void LevelProfile::resetCurrent() {
    if (!m_loaded) return;
    for (auto& m : m_modes) {
        m.capMul = 1.f;
        m.spots.clear();
    }
    m_dirty = true;
    save();
    AssistLog::info(fmt::format("LEARN: reset profile {}", m_key));
}

void LevelProfile::save() {
    if (!m_loaded || m_key.empty()) return;
    if (!m_dirty) return;
    auto res = writeFile();
    if (res.isOk()) {
        m_dirty = false;
        AssistLog::debug(fmt::format(
            "LEARN: profile saved ({} spots)", spotCount()
        ));
    }
    else {
        AssistLog::debug(fmt::format(
            "LEARN: save failed: {}", res.unwrapErr()
        ));
    }
}

Result<> LevelProfile::writeFile() const {
    auto dir = levelsDir();
    (void)file::createDirectoryAll(dir);

    matjson::Value root = matjson::Value::object();
    root["key"] = m_key;
    root["version"] = 1;

    matjson::Value modes = matjson::Value::object();
    for (int i = 0; i < StageConfig::MODE_COUNT; ++i) {
        auto const kind = static_cast<GameModeKind>(i);
        auto const& m = m_modes[i];
        if (m.spots.empty() && std::abs(m.capMul - 1.f) < 0.01f) continue;

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
            spots[std::to_string(bucket)] = sj;
        }
        mj["spots"] = spots;
        modes[StageConfig::kindName(kind)] = mj;
    }
    root["modes"] = modes;

    auto path = profilePath(m_key);
    return file::writeToJson(path, root);
}

Result<> LevelProfile::readFile() {
    auto path = profilePath(m_key);
    auto res = file::readJson(path);
    if (res.isErr()) return Err(res.unwrapErr());

    auto root = res.unwrap();
    if (!root.contains("modes") || !root["modes"].isObject()) return Ok();

    auto& modes = root["modes"];
    for (int i = 0; i < StageConfig::MODE_COUNT; ++i) {
        auto const* name = StageConfig::kindName(static_cast<GameModeKind>(i));
        if (!modes.contains(name)) continue;
        auto& mj = modes[name];
        auto& m = m_modes[i];
        if (mj.contains("capMul")) {
            m.capMul = clamp01f(static_cast<float>(mj["capMul"].asDouble().unwrapOr(1.0)), 0.5f, 2.f);
        }
        if (mj.contains("spots") && mj["spots"].isObject()) {
            for (auto const& [k, sj] : mj["spots"]) {
                int bucket = 0;
                try { bucket = std::stoi(k); }
                catch (...) { continue; }
                SpotStats s;
                if (sj.contains("bias")) s.bias = clamp01f(static_cast<float>(sj["bias"].asDouble().unwrapOr(0.0)), -2.f, 2.f);
                if (sj.contains("trust")) s.trust = clamp01f(static_cast<float>(sj["trust"].asDouble().unwrapOr(1.0)), 0.f, 1.f);
                if (sj.contains("pessimism")) s.pessimism = static_cast<int>(sj["pessimism"].asInt().unwrapOr(0));
                if (sj.contains("n")) s.n = static_cast<int>(sj["n"].asInt().unwrapOr(0));
                if (sj.contains("saved")) s.saved = static_cast<int>(sj["saved"].asInt().unwrapOr(0));
                if (sj.contains("diedAfterAssist")) s.diedAfterAssist = static_cast<int>(sj["diedAfterAssist"].asInt().unwrapOr(0));
                m.spots[bucket] = s;
            }
        }
    }
    m_dirty = false;
    return Ok();
}


