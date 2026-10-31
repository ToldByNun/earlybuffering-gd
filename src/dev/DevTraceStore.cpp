#include "DevTraceStore.hpp"
#include "../config/StageConfig.hpp"
#include "../core/PlatformLimits.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fmt/format.h>
#include <fstream>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace geode::prelude;

DevTraceStore& DevTraceStore::get() {
    static DevTraceStore inst;
    return inst;
}

char const* DevTraceStore::kindId(DevEventKind kind) {
    switch (kind) {
        case DevEventKind::PressObserved: return "pressObserved";
        case DevEventKind::GatePass: return "gatePass";
        case DevEventKind::PlanFinished: return "planFinished";
        case DevEventKind::DelayArm: return "delayArm";
        case DevEventKind::PassThrough: return "passThrough";
        case DevEventKind::Recheck: return "recheck";
        case DevEventKind::Hus: return "hus";
        case DevEventKind::Fire: return "fire";
        case DevEventKind::Checkpoint: return "checkpoint";
        case DevEventKind::Outcome: return "outcome";
    }
    return "pressObserved";
}

bool DevTraceStore::parseKind(std::string const& s, DevEventKind& out) {
    if (s == "pressObserved") { out = DevEventKind::PressObserved; return true; }
    if (s == "gatePass") { out = DevEventKind::GatePass; return true; }
    if (s == "planFinished") { out = DevEventKind::PlanFinished; return true; }
    if (s == "delayArm") { out = DevEventKind::DelayArm; return true; }
    if (s == "passThrough") { out = DevEventKind::PassThrough; return true; }
    if (s == "recheck") { out = DevEventKind::Recheck; return true; }
    if (s == "hus") { out = DevEventKind::Hus; return true; }
    if (s == "fire") { out = DevEventKind::Fire; return true; }
    if (s == "checkpoint") { out = DevEventKind::Checkpoint; return true; }
    if (s == "outcome") { out = DevEventKind::Outcome; return true; }
    return false;
}

char const* DevTraceStore::sourceId(DevEventSource source) {
    switch (source) {
        case DevEventSource::Bake: return "bake";
        case DevEventSource::Live: break;
    }
    return "live";
}

bool DevTraceStore::parseSource(std::string const& s, DevEventSource& out) {
    if (s == "bake") { out = DevEventSource::Bake; return true; }
    if (s == "live") { out = DevEventSource::Live; return true; }
    return false;
}

bool DevTraceStore::validatePressChain(
    std::vector<DevDecisionEvent> const& events,
    int64_t pressId,
    std::string& err
) {
    err.clear();
    if (pressId <= 0) {
        err = "pressId<=0";
        return false;
    }

    std::vector<DevDecisionEvent const*> chain;
    chain.reserve(events.size());
    for (auto const& e : events) {
        if (e.pressId == pressId) {
            chain.push_back(&e);
        }
    }
    if (chain.empty()) {
        err = "no-events-for-pressId";
        return false;
    }

    // eventId must be strictly increasing along the chain order we validate.
    for (size_t i = 1; i < chain.size(); ++i) {
        if (chain[i]->eventId <= chain[i - 1]->eventId) {
            err = "eventId-not-monotonic";
            return false;
        }
    }

    std::unordered_map<int64_t, DevDecisionEvent const*> byId;
    byId.reserve(chain.size());

    DevDecisionEvent const* press = nullptr;
    for (auto const* e : chain) {
        if (e->eventId <= 0) {
            err = "eventId<=0";
            return false;
        }
        if (e->cycleId != e->pressId) {
            err = "cycleId!=pressId";
            return false;
        }
        if (byId.count(e->eventId)) {
            err = "duplicate-eventId";
            return false;
        }
        byId[e->eventId] = e;

        if (e->kind == DevEventKind::PressObserved) {
            if (press) {
                err = "duplicate-pressObserved";
                return false;
            }
            if (e->parentEventId != 0) {
                err = "pressObserved-parent-not-0";
                return false;
            }
            press = e;
        }
        else {
            if (e->parentEventId <= 0) {
                err = "missing-parentEventId";
                return false;
            }
            auto it = byId.find(e->parentEventId);
            if (it == byId.end()) {
                // Parent must already appear earlier in the same press chain.
                err = "parent-not-in-chain";
                return false;
            }
            if (it->second->pressId != pressId) {
                err = "parent-pressId-mismatch";
                return false;
            }
        }
    }

    if (!press) {
        err = "missing-pressObserved";
        return false;
    }

    // Required terminal shape for a Delay arm cycle: … → fire → outcome
    // Gate-only cycles: press → gatePass → outcome (parent = gatePass)
    bool hasFire = false;
    bool hasOutcome = false;
    bool hasGate = false;
    bool hasPlan = false;
    bool hasArm = false;
    bool hasPassThrough = false;
    for (auto const* e : chain) {
        switch (e->kind) {
            case DevEventKind::GatePass: hasGate = true; break;
            case DevEventKind::PlanFinished: hasPlan = true; break;
            case DevEventKind::DelayArm: hasArm = true; break;
            case DevEventKind::PassThrough: hasPassThrough = true; break;
            case DevEventKind::Fire: hasFire = true; break;
            case DevEventKind::Outcome: hasOutcome = true; break;
            default: break;
        }
        if (e->kind != DevEventKind::PressObserved && e->attemptId != press->attemptId) {
            err = "attemptId-mismatch";
            return false;
        }
    }

    if (!hasOutcome) {
        err = "missing-outcome";
        return false;
    }

    if (hasArm) {
        if (!hasPlan) {
            err = "delayArm-without-planFinished";
            return false;
        }
        if (!hasFire) {
            err = "delayArm-without-fire";
            return false;
        }
    }

    if (hasGate && hasPlan) {
        err = "gatePass-and-planFinished";
        return false;
    }

    if (hasPassThrough && hasArm) {
        err = "passThrough-and-delayArm";
        return false;
    }

    // Outcome parent must be fire, passThrough, or gatePass.
    for (auto const* e : chain) {
        if (e->kind != DevEventKind::Outcome) continue;
        auto it = byId.find(e->parentEventId);
        if (it == byId.end()) {
            err = "outcome-parent-missing";
            return false;
        }
        auto const pk = it->second->kind;
        if (pk != DevEventKind::Fire
            && pk != DevEventKind::PassThrough
            && pk != DevEventKind::GatePass) {
            err = "outcome-parent-kind";
            return false;
        }
    }

    // Fire parent must be delayArm, recheck, or hus.
    for (auto const* e : chain) {
        if (e->kind != DevEventKind::Fire) continue;
        auto it = byId.find(e->parentEventId);
        if (it == byId.end()) {
            err = "fire-parent-missing";
            return false;
        }
        auto const pk = it->second->kind;
        if (pk != DevEventKind::DelayArm
            && pk != DevEventKind::Recheck
            && pk != DevEventKind::Hus) {
            err = "fire-parent-kind";
            return false;
        }
    }

    return true;
}

static matjson::Value clockStampToJson(DevClockContract::Stamp const& st) {
    matjson::Value c = matjson::Value::object();
    c["clockSchema"] = st.clockSchema;
    c["timeBasis"] = st.timeBasis;
    c["attemptTick"] = st.attemptTick;
    c["effectTick"] = st.effectTick;
    c["simDt"] = st.simDt;
    c["firstDt"] = st.firstDt;
    c["midStep"] = st.midStep;
    c["continuous"] = st.continuous;
    c["timeWarp"] = st.timeWarp;
    c["simTps"] = st.simTps;
    c["wallDt"] = st.wallDt;
    c["speedScaleSuspect"] = st.speedScaleSuspect;
    c["liveSimPathId"] = DevClockContract::simPathId(st.liveSimPathId);
    return c;
}

static bool clockStampFromJson(
    matjson::Value const& j, DevClockContract::Stamp& st, std::string& err
) {
    st = {};
    st.clockSchema = j.contains("clockSchema")
        ? static_cast<int>(j["clockSchema"].asInt().unwrapOr(0))
        : 0;
    st.timeBasis = j.contains("timeBasis")
        ? std::string(j["timeBasis"].asString().unwrapOr(""))
        : "";
    st.attemptTick = j.contains("attemptTick")
        ? static_cast<int>(j["attemptTick"].asInt().unwrapOr(0))
        : 0;
    st.effectTick = j.contains("effectTick")
        ? static_cast<int>(j["effectTick"].asInt().unwrapOr(0))
        : 0;
    st.simDt = j.contains("simDt")
        ? static_cast<float>(j["simDt"].asDouble().unwrapOr(0.0))
        : 0.f;
    st.firstDt = j.contains("firstDt")
        ? static_cast<float>(j["firstDt"].asDouble().unwrapOr(0.0))
        : 0.f;
    st.midStep = j.contains("midStep") ? j["midStep"].asBool().unwrapOr(false) : false;
    st.continuous = j.contains("continuous")
        ? j["continuous"].asBool().unwrapOr(false)
        : false;
    st.timeWarp = j.contains("timeWarp")
        ? static_cast<float>(j["timeWarp"].asDouble().unwrapOr(1.0))
        : 1.f;
    st.simTps = j.contains("simTps") ? j["simTps"].asDouble().unwrapOr(0.0) : 0.0;
    st.wallDt = j.contains("wallDt")
        ? static_cast<float>(j["wallDt"].asDouble().unwrapOr(0.0))
        : 0.f;
    st.speedScaleSuspect = j.contains("speedScaleSuspect")
        ? j["speedScaleSuspect"].asBool().unwrapOr(false)
        : false;
    auto path = j.contains("liveSimPathId")
        ? std::string(j["liveSimPathId"].asString().unwrapOr("none"))
        : "none";
    if (!DevClockContract::parseSimPathId(path, st.liveSimPathId)) {
        err = "bad-liveSimPathId";
        return false;
    }
    st.valid = true;
    return true;
}

static matjson::Value playerBlobToJson(DevPlayerBlob const& b) {
    matjson::Value j = matjson::Value::object();
    j["present"] = b.present;
    if (!b.present) return j;
    j["x"] = b.x;
    j["y"] = b.y;
    j["rotation"] = b.rotation;
    j["scaleX"] = b.scaleX;
    j["scaleY"] = b.scaleY;
    j["yVelocity"] = b.yVelocity;
    j["fallSpeed"] = b.fallSpeed;
    j["gravity"] = b.gravity;
    j["gravityMod"] = b.gravityMod;
    j["speedMultiplier"] = b.speedMultiplier;
    j["playerSpeed"] = b.playerSpeed;
    j["vehicleSize"] = b.vehicleSize;
    j["platformerXVelocity"] = b.platformerXVelocity;
    j["isUpsideDown"] = b.isUpsideDown;
    j["isSideways"] = b.isSideways;
    j["isGoingLeft"] = b.isGoingLeft;
    j["isShip"] = b.isShip;
    j["isBall"] = b.isBall;
    j["isBird"] = b.isBird;
    j["isSwing"] = b.isSwing;
    j["isDart"] = b.isDart;
    j["isRobot"] = b.isRobot;
    j["isSpider"] = b.isSpider;
    j["isSecondPlayer"] = b.isSecondPlayer;
    j["isPlatformer"] = b.isPlatformer;
    j["isDashing"] = b.isDashing;
    j["isOnGround"] = b.isOnGround;
    j["isOnGround2"] = b.isOnGround2;
    j["isOnSlope"] = b.isOnSlope;
    j["wasOnSlope"] = b.wasOnSlope;
    j["jumpBuffered"] = b.jumpBuffered;
    j["wasJumpBuffered"] = b.wasJumpBuffered;
    j["stateRingJump"] = b.stateRingJump;
    j["hasEverJumped"] = b.hasEverJumped;
    j["hasEverHitRing"] = b.hasEverHitRing;
    j["isLocked"] = b.isLocked;
    j["controlsDisabled"] = b.controlsDisabled;
    j["maybeIsFalling"] = b.maybeIsFalling;
    j["maybeIsBoosted"] = b.maybeIsBoosted;
    return j;
}

static void playerBlobFromJson(matjson::Value const& j, DevPlayerBlob& b) {
    b = {};
    b.present = j.contains("present") ? j["present"].asBool().unwrapOr(false) : false;
    if (!b.present) return;
    auto f = [&](char const* k, float& out) {
        if (j.contains(k)) out = static_cast<float>(j[k].asDouble().unwrapOr(0.0));
    };
    auto bo = [&](char const* k, bool& out) {
        if (j.contains(k)) out = j[k].asBool().unwrapOr(false);
    };
    f("x", b.x); f("y", b.y); f("rotation", b.rotation);
    f("scaleX", b.scaleX); f("scaleY", b.scaleY);
    f("yVelocity", b.yVelocity); f("fallSpeed", b.fallSpeed);
    f("gravity", b.gravity); f("gravityMod", b.gravityMod);
    f("speedMultiplier", b.speedMultiplier); f("playerSpeed", b.playerSpeed);
    f("vehicleSize", b.vehicleSize); f("platformerXVelocity", b.platformerXVelocity);
    bo("isUpsideDown", b.isUpsideDown); bo("isSideways", b.isSideways);
    bo("isGoingLeft", b.isGoingLeft); bo("isShip", b.isShip); bo("isBall", b.isBall);
    bo("isBird", b.isBird); bo("isSwing", b.isSwing); bo("isDart", b.isDart);
    bo("isRobot", b.isRobot); bo("isSpider", b.isSpider);
    bo("isSecondPlayer", b.isSecondPlayer); bo("isPlatformer", b.isPlatformer);
    bo("isDashing", b.isDashing); bo("isOnGround", b.isOnGround);
    bo("isOnGround2", b.isOnGround2); bo("isOnSlope", b.isOnSlope);
    bo("wasOnSlope", b.wasOnSlope); bo("jumpBuffered", b.jumpBuffered);
    bo("wasJumpBuffered", b.wasJumpBuffered); bo("stateRingJump", b.stateRingJump);
    bo("hasEverJumped", b.hasEverJumped); bo("hasEverHitRing", b.hasEverHitRing);
    bo("isLocked", b.isLocked); bo("controlsDisabled", b.controlsDisabled);
    bo("maybeIsFalling", b.maybeIsFalling); bo("maybeIsBoosted", b.maybeIsBoosted);
}

static matjson::Value witnessToJson(DevLiveWitness const& w) {
    matjson::Value j = matjson::Value::object();
    j["playerBlobSchema"] = fmt::format("{:016x}", w.playerBlobSchema);
    j["freezeWorld"] = w.freezeWorld;
    j["player"] = playerBlobToJson(w.player);
    if (w.dual.present) j["dual"] = playerBlobToJson(w.dual);
    if (w.clock.valid) j["clock"] = clockStampToJson(w.clock);

    std::vector<matjson::Value> cats;
    cats.reserve(w.categories.size());
    for (auto const& c : w.categories) {
        matjson::Value cj = matjson::Value::object();
        cj["category"] = DevLiveWitness::categoryId(c.category);
        cj["status"] = DevLiveWitness::statusId(c.status);
        if (c.digest != 0) {
            cj["digest"] = fmt::format("{:016x}", c.digest);
        }
        if (c.digestMismatch) cj["digestMismatch"] = true;
        cats.push_back(cj);
    }
    j["categories"] = cats;

    std::vector<matjson::Value> deps;
    for (int d : w.worldDeps) {
        deps.push_back(DevLiveWitness::categoryId(
            static_cast<DevWitnessCategory>(d)
        ));
    }
    j["worldDeps"] = deps;

    if (!w.orbIds.empty()) {
        std::vector<matjson::Value> orbs;
        orbs.reserve(w.orbIds.size());
        for (int id : w.orbIds) orbs.push_back(id);
        j["orbIds"] = orbs;
    }
    if (!w.moverPoses.empty()) {
        std::vector<matjson::Value> poses;
        poses.reserve(w.moverPoses.size());
        for (auto const& p : w.moverPoses) {
            matjson::Value pj = matjson::Value::object();
            pj["id"] = p.uniqueId;
            pj["x"] = p.x;
            pj["y"] = p.y;
            pj["r"] = p.rotation;
            pj["sx"] = p.scaleX;
            pj["sy"] = p.scaleY;
            pj["a"] = p.active;
            poses.push_back(pj);
        }
        j["moverPoses"] = poses;
    }
    if (!w.triggerStoreKey.empty()) j["triggerStoreKey"] = w.triggerStoreKey;
    j["triggerStoreVersion"] = w.triggerStoreVersion;
    j["triggerApplyTick"] = w.triggerApplyTick;
    j["triggerCanSpool"] = w.triggerCanSpool;
    j["triggerReadyTracks"] = w.triggerReadyTracks;
    j["triggerTrackCount"] = w.triggerTrackCount;
    j["triggerBakeEffects"] = w.triggerBakeEffects;
    j["triggerBakeChildren"] = w.triggerBakeChildren;
    j["triggerCollectedEffects"] = w.triggerCollectedEffects;
    j["triggerSkipFollow"] = w.triggerSkipFollow;
    j["triggerSkipDynamic"] = w.triggerSkipDynamic;
    j["triggerSkipNonSpawn"] = w.triggerSkipNonSpawn;
    if (w.hasHazardDigest) {
        j["hazardDigest"] = fmt::format("{:016x}", w.hazardDigest);
    }
    return j;
}

static bool witnessFromJson(
    matjson::Value const& j, DevLiveWitness& w, std::string& err
) {
    w = {};
    w.valid = true;
    if (j.contains("playerBlobSchema")) {
        auto s = std::string(j["playerBlobSchema"].asString().unwrapOr("0"));
        w.playerBlobSchema = std::strtoull(s.c_str(), nullptr, 16);
    }
    w.freezeWorld = j.contains("freezeWorld")
        ? j["freezeWorld"].asBool().unwrapOr(false)
        : false;
    if (j.contains("player")) playerBlobFromJson(j["player"], w.player);
    if (j.contains("dual")) playerBlobFromJson(j["dual"], w.dual);
    if (j.contains("clock")) {
        std::string clockErr;
        if (!clockStampFromJson(j["clock"], w.clock, clockErr)) {
            err = clockErr.empty() ? "bad-witness-clock" : clockErr;
            return false;
        }
    }
    if (j.contains("categories") && j["categories"].isArray()) {
        for (auto const& cj : j["categories"].asArray().unwrap()) {
            DevWitnessCategoryEntry e;
            auto cat = std::string(cj["category"].asString().unwrapOr(""));
            auto st = std::string(cj["status"].asString().unwrapOr("absent"));
            if (!DevLiveWitness::parseCategory(cat, e.category)) {
                err = "bad-witness-category";
                return false;
            }
            if (!DevLiveWitness::parseStatus(st, e.status)) {
                err = "bad-witness-status";
                return false;
            }
            if (cj.contains("digest")) {
                auto ds = std::string(cj["digest"].asString().unwrapOr("0"));
                e.digest = std::strtoull(ds.c_str(), nullptr, 16);
            }
            e.digestMismatch = cj.contains("digestMismatch")
                ? cj["digestMismatch"].asBool().unwrapOr(false)
                : false;
            w.categories.push_back(e);
        }
    }
    if (j.contains("worldDeps") && j["worldDeps"].isArray()) {
        for (auto const& dj : j["worldDeps"].asArray().unwrap()) {
            auto name = std::string(dj.asString().unwrapOr(""));
            DevWitnessCategory c;
            if (!DevLiveWitness::parseCategory(name, c)) {
                err = "bad-worldDep";
                return false;
            }
            w.worldDeps.insert(static_cast<int>(c));
        }
    }
    if (j.contains("orbIds") && j["orbIds"].isArray()) {
        for (auto const& oj : j["orbIds"].asArray().unwrap()) {
            w.orbIds.push_back(static_cast<int>(oj.asInt().unwrapOr(0)));
        }
    }
    if (j.contains("moverPoses") && j["moverPoses"].isArray()) {
        for (auto const& pj : j["moverPoses"].asArray().unwrap()) {
            DevObjectPose p;
            p.uniqueId = static_cast<int>(pj["id"].asInt().unwrapOr(0));
            p.x = static_cast<float>(pj["x"].asDouble().unwrapOr(0.0));
            p.y = static_cast<float>(pj["y"].asDouble().unwrapOr(0.0));
            p.rotation = static_cast<float>(pj["r"].asDouble().unwrapOr(0.0));
            p.scaleX = static_cast<float>(pj["sx"].asDouble().unwrapOr(1.0));
            p.scaleY = static_cast<float>(pj["sy"].asDouble().unwrapOr(1.0));
            p.active = pj["a"].asBool().unwrapOr(true);
            w.moverPoses.push_back(p);
        }
    }
    if (j.contains("triggerStoreKey")) {
        w.triggerStoreKey = std::string(j["triggerStoreKey"].asString().unwrapOr(""));
    }
    w.triggerStoreVersion = j.contains("triggerStoreVersion")
        ? static_cast<int>(j["triggerStoreVersion"].asInt().unwrapOr(0))
        : 0;
    w.triggerApplyTick = j.contains("triggerApplyTick")
        ? static_cast<int>(j["triggerApplyTick"].asInt().unwrapOr(-1))
        : -1;
    w.triggerCanSpool = j.contains("triggerCanSpool")
        ? j["triggerCanSpool"].asBool().unwrapOr(false)
        : false;
    auto ti = [&](char const* k, int& out) {
        if (j.contains(k)) out = static_cast<int>(j[k].asInt().unwrapOr(0));
    };
    ti("triggerReadyTracks", w.triggerReadyTracks);
    ti("triggerTrackCount", w.triggerTrackCount);
    ti("triggerBakeEffects", w.triggerBakeEffects);
    ti("triggerBakeChildren", w.triggerBakeChildren);
    ti("triggerCollectedEffects", w.triggerCollectedEffects);
    ti("triggerSkipFollow", w.triggerSkipFollow);
    ti("triggerSkipDynamic", w.triggerSkipDynamic);
    ti("triggerSkipNonSpawn", w.triggerSkipNonSpawn);
    if (j.contains("hazardDigest")) {
        auto hs = std::string(j["hazardDigest"].asString().unwrapOr("0"));
        w.hazardDigest = std::strtoull(hs.c_str(), nullptr, 16);
        w.hasHazardDigest = true;
    }
    return true;
}

static matjson::Value eventToJson(DevDecisionEvent const& e) {
    matjson::Value j = matjson::Value::object();
    j["eventId"] = e.eventId;
    j["attemptId"] = e.attemptId;
    j["pressId"] = e.pressId;
    j["cycleId"] = e.cycleId;
    j["planId"] = e.planId;
    j["parentEventId"] = e.parentEventId;
    j["playerIndex"] = e.playerIndex;
    j["down"] = e.down;
    j["source"] = DevTraceStore::sourceId(e.source);
    j["kind"] = DevTraceStore::kindId(e.kind);
    j["effectTick"] = e.effectTick;
    j["pressTick"] = e.pressTick;
    j["plannedDelay"] = e.plannedDelay;
    j["plannedFireTick"] = e.plannedFireTick;
    j["actualFireTick"] = e.actualFireTick;
    if (!e.assistReason.empty()) j["assistReason"] = e.assistReason;
    if (!e.endReason.empty()) j["endReason"] = e.endReason;
    if (e.hasAssistDiag) {
        j["armVerifyOk"] = e.armVerifyOk;
        j["budgetHitAtArm"] = e.budgetHitAtArm;
        j["recheckDelta"] = e.recheckDelta;
        j["holdExtendMax"] = e.holdExtendMax;
        j["deathAfterFire"] = e.deathAfterFire;
        j["ticksToDeath"] = e.ticksToDeath;
        j["liveVsEvalMismatch"] = e.liveVsEvalMismatch;
    }
    if (e.hasClockStamp && e.clockStamp.valid) {
        j["clock"] = clockStampToJson(e.clockStamp);
    }
    if (e.hasWitness && e.witness.valid) {
        j["witness"] = witnessToJson(e.witness);
    }
    if (e.hasFidelity) {
        j["fidelity"] = DevStateDiff::fidelityId(e.fidelity);
        j["claimsAllowed"] = e.claimsAllowed;
        j["approx"] = e.approx;
        if (!e.evalSuppressedReason.empty()) {
            j["evalSuppressedReason"] = e.evalSuppressedReason;
        }
        if (!e.diffs.empty()) {
            std::vector<matjson::Value> dj;
            dj.reserve(e.diffs.size());
            for (auto const& d : e.diffs) {
                matjson::Value item = matjson::Value::object();
                item["category"] = DevLiveWitness::categoryId(d.category);
                item["fieldOrObjectId"] = d.fieldOrObjectId;
                item["liveValue"] = d.liveValue;
                item["restoredValue"] = d.restoredValue;
                item["kind"] = DevStateDiff::diffKindId(d.kind);
                dj.push_back(item);
            }
            j["diffs"] = dj;
        }
    }
    if (!e.candNotes.empty() || e.candNotesTruncated) {
        std::vector<matjson::Value> notes;
        notes.reserve(e.candNotes.size());
        for (auto const& n : e.candNotes) {
            matjson::Value nj = matjson::Value::object();
            nj["delay"] = n.delay;
            nj["status"] = DevRejectLedger::statusId(n.status);
            nj["rejectCode"] = DevRejectLedger::rejectCodeId(n.rejectCode);
            if (!n.detail.empty()) nj["detail"] = n.detail;
            if (n.hasScores) {
                nj["surviveBin"] = n.surviveBin;
                nj["endX"] = n.endX;
                nj["latencyTerm"] = n.latencyTerm;
                nj["threatCleared"] = n.threatCleared;
                nj["score"] = n.score;
            }
            notes.push_back(nj);
        }
        j["candNotes"] = notes;
        if (e.candNotesTruncated) j["candNotesTruncated"] = true;
    }
    if (!e.attribution.empty()) {
        std::vector<matjson::Value> attrs;
        attrs.reserve(e.attribution.size());
        for (auto const& a : e.attribution) {
            matjson::Value aj = matjson::Value::object();
            aj["code"] = DevRejectLedger::attributionId(a.code);
            if (!a.detail.empty()) aj["detail"] = a.detail;
            attrs.push_back(aj);
        }
        j["attribution"] = attrs;
    }
    return j;
}

static bool eventFromJson(matjson::Value const& j, DevDecisionEvent& e, std::string& err) {
    e = {};
    e.eventId = j.contains("eventId") ? j["eventId"].asInt().unwrapOr(0) : 0;
    e.attemptId = j.contains("attemptId") ? j["attemptId"].asInt().unwrapOr(0) : 0;
    e.pressId = j.contains("pressId") ? j["pressId"].asInt().unwrapOr(0) : 0;
    e.cycleId = j.contains("cycleId") ? j["cycleId"].asInt().unwrapOr(0) : 0;
    e.planId = j.contains("planId") ? j["planId"].asInt().unwrapOr(0) : 0;
    e.parentEventId = j.contains("parentEventId") ? j["parentEventId"].asInt().unwrapOr(0) : 0;
    e.playerIndex = j.contains("playerIndex")
        ? static_cast<int>(j["playerIndex"].asInt().unwrapOr(0))
        : 0;
    e.down = j.contains("down") ? j["down"].asBool().unwrapOr(true) : true;
    auto src = j.contains("source") ? std::string(j["source"].asString().unwrapOr("live")) : "live";
    if (!DevTraceStore::parseSource(src, e.source)) {
        err = "bad-source";
        return false;
    }
    auto kind = j.contains("kind") ? std::string(j["kind"].asString().unwrapOr("")) : "";
    if (!DevTraceStore::parseKind(kind, e.kind)) {
        err = "bad-kind";
        return false;
    }
    e.effectTick = j.contains("effectTick")
        ? static_cast<int>(j["effectTick"].asInt().unwrapOr(0))
        : 0;
    e.pressTick = j.contains("pressTick")
        ? static_cast<int>(j["pressTick"].asInt().unwrapOr(0))
        : 0;
    e.plannedDelay = j.contains("plannedDelay")
        ? static_cast<int>(j["plannedDelay"].asInt().unwrapOr(0))
        : 0;
    e.plannedFireTick = j.contains("plannedFireTick")
        ? static_cast<int>(j["plannedFireTick"].asInt().unwrapOr(-1))
        : -1;
    e.actualFireTick = j.contains("actualFireTick")
        ? static_cast<int>(j["actualFireTick"].asInt().unwrapOr(-1))
        : -1;
    if (j.contains("assistReason")) {
        e.assistReason = std::string(j["assistReason"].asString().unwrapOr(""));
    }
    if (j.contains("endReason")) {
        e.endReason = std::string(j["endReason"].asString().unwrapOr(""));
    }
    if (j.contains("armVerifyOk") || j.contains("deathAfterFire")
        || j.contains("budgetHitAtArm") || j.contains("recheckDelta")
        || j.contains("holdExtendMax") || j.contains("liveVsEvalMismatch")) {
        e.hasAssistDiag = true;
        e.armVerifyOk = j.contains("armVerifyOk")
            ? j["armVerifyOk"].asBool().unwrapOr(true)
            : true;
        e.budgetHitAtArm = j.contains("budgetHitAtArm")
            ? j["budgetHitAtArm"].asBool().unwrapOr(false)
            : false;
        e.recheckDelta = j.contains("recheckDelta")
            ? static_cast<int>(j["recheckDelta"].asInt().unwrapOr(0))
            : 0;
        e.holdExtendMax = j.contains("holdExtendMax")
            ? static_cast<int>(j["holdExtendMax"].asInt().unwrapOr(-1))
            : -1;
        e.deathAfterFire = j.contains("deathAfterFire")
            ? j["deathAfterFire"].asBool().unwrapOr(false)
            : false;
        e.ticksToDeath = j.contains("ticksToDeath")
            ? static_cast<int>(j["ticksToDeath"].asInt().unwrapOr(-1))
            : -1;
        e.liveVsEvalMismatch = j.contains("liveVsEvalMismatch")
            ? j["liveVsEvalMismatch"].asBool().unwrapOr(false)
            : false;
    }
    if (j.contains("clock")) {
        std::string clockErr;
        if (!clockStampFromJson(j["clock"], e.clockStamp, clockErr)) {
            err = clockErr.empty() ? "bad-clock" : clockErr;
            return false;
        }
        e.hasClockStamp = true;
    }
    if (j.contains("witness")) {
        std::string witnessErr;
        if (!witnessFromJson(j["witness"], e.witness, witnessErr)) {
            err = witnessErr.empty() ? "bad-witness" : witnessErr;
            return false;
        }
        e.hasWitness = true;
    }
    if (j.contains("fidelity")) {
        e.hasFidelity = true;
        auto fid = std::string(j["fidelity"].asString().unwrapOr(""));
        if (!DevStateDiff::parseFidelity(fid, e.fidelity)) {
            err = "bad-fidelity";
            return false;
        }
        e.claimsAllowed = j.contains("claimsAllowed")
            ? j["claimsAllowed"].asBool().unwrapOr(false)
            : false;
        e.approx = j.contains("approx") ? j["approx"].asBool().unwrapOr(false) : false;
        if (j.contains("evalSuppressedReason")) {
            e.evalSuppressedReason = std::string(
                j["evalSuppressedReason"].asString().unwrapOr("")
            );
        }
        if (j.contains("diffs") && j["diffs"].isArray()) {
            for (auto const& item : j["diffs"].asArray().unwrap()) {
                DevDiffEntry d;
                auto cat = std::string(item["category"].asString().unwrapOr(""));
                if (!DevLiveWitness::parseCategory(cat, d.category)) {
                    err = "bad-diff-category";
                    return false;
                }
                d.fieldOrObjectId = std::string(
                    item["fieldOrObjectId"].asString().unwrapOr("")
                );
                d.liveValue = std::string(item["liveValue"].asString().unwrapOr(""));
                d.restoredValue = std::string(
                    item["restoredValue"].asString().unwrapOr("")
                );
                auto kind = std::string(item["kind"].asString().unwrapOr("mismatch"));
                if (kind == "unrestored") d.kind = DevDiffKind::Unrestored;
                else if (kind == "digestOnly") d.kind = DevDiffKind::DigestOnly;
                else d.kind = DevDiffKind::Mismatch;
                e.diffs.push_back(d);
            }
        }
    }
    if (j.contains("candNotes") && j["candNotes"].isArray()) {
        for (auto const& nj : j["candNotes"].asArray().unwrap()) {
            DevRejectLedger::CandNote n;
            n.delay = static_cast<int>(nj["delay"].asInt().unwrapOr(0));
            auto st = std::string(nj["status"].asString().unwrapOr("skipped"));
            auto rc = std::string(nj["rejectCode"].asString().unwrapOr("not-viable"));
            if (!DevRejectLedger::parseStatus(st, n.status)) {
                err = "bad-cand-status";
                return false;
            }
            if (!DevRejectLedger::parseRejectCode(rc, n.rejectCode)) {
                err = "bad-reject-code";
                return false;
            }
            if (nj.contains("detail")) {
                n.detail = std::string(nj["detail"].asString().unwrapOr(""));
            }
            if (nj.contains("surviveBin") || nj.contains("endX") || nj.contains("score")) {
                n.hasScores = true;
                n.surviveBin = static_cast<int>(nj["surviveBin"].asInt().unwrapOr(0));
                n.endX = static_cast<float>(nj["endX"].asDouble().unwrapOr(0.0));
                n.latencyTerm = static_cast<float>(
                    nj["latencyTerm"].asDouble().unwrapOr(0.0)
                );
                n.threatCleared = nj.contains("threatCleared")
                    ? nj["threatCleared"].asBool().unwrapOr(false)
                    : false;
                n.score = static_cast<float>(nj["score"].asDouble().unwrapOr(0.0));
            }
            e.candNotes.push_back(std::move(n));
        }
    }
    e.candNotesTruncated = j.contains("candNotesTruncated")
        ? j["candNotesTruncated"].asBool().unwrapOr(false)
        : false;
    if (j.contains("attribution") && j["attribution"].isArray()) {
        for (auto const& aj : j["attribution"].asArray().unwrap()) {
            DevRejectLedger::AttributionNote a;
            auto code = std::string(aj["code"].asString().unwrapOr(""));
            if (!DevRejectLedger::parseAttribution(code, a.code)) {
                err = "bad-attribution";
                return false;
            }
            if (aj.contains("detail")) {
                a.detail = std::string(aj["detail"].asString().unwrapOr(""));
            }
            e.attribution.push_back(std::move(a));
        }
    }
    return true;
}

static matjson::Value sessionToJsonValue(DevSessionFile const& session) {
    matjson::Value root = matjson::Value::object();
    root["traceSchema"] = session.traceSchema;
    root["modVersion"] = session.modVersion;
    root["sessionId"] = session.sessionId;
    root["levelKey"] = session.levelKey;
    root["levelName"] = session.levelName;
    root["startPos"] = session.startPos;
    root["practice"] = session.practice;
    root["settingsFingerprint"] = session.settingsFingerprint;
    root["recordingsTarget"] = session.recordingsTarget;
    root["recordingsDone"] = session.recordingsDone;

    std::vector<matjson::Value> attempts;
    attempts.reserve(session.attempts.size());
    for (auto const& a : session.attempts) {
        matjson::Value aj = matjson::Value::object();
        aj["attemptId"] = a.attemptId;
        aj["startTick"] = a.startTick;
        aj["endReason"] = a.endReason;
        aj["sealed"] = a.sealed;
        aj["counted"] = a.counted;
        std::vector<matjson::Value> evs;
        evs.reserve(a.events.size());
        for (auto const& e : a.events) {
            evs.push_back(eventToJson(e));
        }
        aj["events"] = evs;
        attempts.push_back(std::move(aj));
    }
    root["attempts"] = attempts;
    return root;
}

std::string DevTraceStore::sessionToJsonString(DevSessionFile const& session) {
    return sessionToJsonValue(session).dump();
}

bool DevTraceStore::sessionFromJsonString(
    std::string const& body, DevSessionFile& out, std::string& err
) {
    err.clear();
    out = {};
    auto parsed = matjson::parse(body);
    if (parsed.isErr()) {
        err = "json-parse";
        return false;
    }
    auto root = parsed.unwrap();
    out.traceSchema = root.contains("traceSchema")
        ? static_cast<int>(root["traceSchema"].asInt().unwrapOr(0))
        : 0;
    if (out.traceSchema != kTraceSchema) {
        err = "traceSchema";
        return false;
    }
    out.modVersion = root.contains("modVersion")
        ? std::string(root["modVersion"].asString().unwrapOr(""))
        : "";
    out.sessionId = root.contains("sessionId")
        ? std::string(root["sessionId"].asString().unwrapOr(""))
        : "";
    out.levelKey = root.contains("levelKey")
        ? std::string(root["levelKey"].asString().unwrapOr(""))
        : "";
    out.levelName = root.contains("levelName")
        ? std::string(root["levelName"].asString().unwrapOr(""))
        : "";
    out.startPos = root.contains("startPos") ? root["startPos"].asBool().unwrapOr(false) : false;
    out.practice = root.contains("practice") ? root["practice"].asBool().unwrapOr(false) : false;
    out.settingsFingerprint = root.contains("settingsFingerprint")
        ? std::string(root["settingsFingerprint"].asString().unwrapOr(""))
        : "";
    out.recordingsTarget = root.contains("recordingsTarget")
        ? static_cast<int>(root["recordingsTarget"].asInt().unwrapOr(10))
        : 10;
    out.recordingsDone = root.contains("recordingsDone")
        ? static_cast<int>(root["recordingsDone"].asInt().unwrapOr(0))
        : 0;

    if (!root.contains("attempts") || !root["attempts"].isArray()) {
        return true;
    }
    for (auto const& aj : root["attempts"]) {
        DevAttempt a;
        a.attemptId = aj.contains("attemptId") ? aj["attemptId"].asInt().unwrapOr(0) : 0;
        a.startTick = aj.contains("startTick")
            ? static_cast<int>(aj["startTick"].asInt().unwrapOr(0))
            : 0;
        a.endReason = aj.contains("endReason")
            ? std::string(aj["endReason"].asString().unwrapOr(""))
            : "";
        a.sealed = aj.contains("sealed") ? aj["sealed"].asBool().unwrapOr(false) : false;
        a.counted = aj.contains("counted") ? aj["counted"].asBool().unwrapOr(false) : false;
        if (aj.contains("events") && aj["events"].isArray()) {
            for (auto const& ej : aj["events"]) {
                DevDecisionEvent e;
                std::string eerr;
                if (!eventFromJson(ej, e, eerr)) {
                    err = eerr.empty() ? "bad-event" : eerr;
                    return false;
                }
                a.events.push_back(std::move(e));
            }
        }
        out.attempts.push_back(std::move(a));
    }
    return true;
}

bool DevTraceStore::isOpen() const {
    return this->m_open;
}

bool DevTraceStore::hasOpenAttempt() const {
    if (!this->m_open || this->m_session.attempts.empty()) return false;
    return !this->m_session.attempts.back().sealed;
}

std::filesystem::path DevTraceStore::tracesDir() const {
    auto* mod = Mod::get();
    if (!mod) return std::filesystem::path("devtraces");
    return mod->getSaveDir() / "devtraces";
}

std::filesystem::path DevTraceStore::sessionPath() const {
    auto id = this->m_session.sessionId.empty() ? std::string("unknown") : this->m_session.sessionId;
    return this->tracesDir() / ("session-" + id + ".json");
}

bool DevTraceStore::beginSession(std::string sessionId, int recordingsTarget) {
    this->closeSession();
    if (sessionId.empty()) {
        auto const ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        sessionId = fmt::format("{}", ms);
    }
    if (recordingsTarget < 1) recordingsTarget = 1;
    if (recordingsTarget > 50) recordingsTarget = 50;

    this->m_session = {};
    this->m_session.traceSchema = kTraceSchema;
    this->m_session.sessionId = std::move(sessionId);
    this->m_session.recordingsTarget = recordingsTarget;
    this->m_session.recordingsDone = 0;
    if (auto* mod = Mod::get()) {
        this->m_session.modVersion = mod->getVersion().toVString();
    }
    this->m_nextPressId = 1;
    this->m_nextEventId = 1;
    this->m_open = true;
    return this->flush();
}

void DevTraceStore::setLevelMeta(
    std::string levelKey,
    std::string levelName,
    bool startPos,
    bool practice,
    std::string settingsFingerprint
) {
    if (!this->m_open) return;
    this->m_session.levelKey = std::move(levelKey);
    this->m_session.levelName = std::move(levelName);
    this->m_session.startPos = startPos;
    this->m_session.practice = practice;
    this->m_session.settingsFingerprint = std::move(settingsFingerprint);
}

void DevTraceStore::setModVersion(std::string modVersion) {
    if (!this->m_open) return;
    this->m_session.modVersion = std::move(modVersion);
}

std::string DevTraceStore::makeSettingsFingerprint() {
    return fmt::format(
        "stage={} mode={} pick={} ticks={}/{} unit={} safe={} orbs={} "
        "simUs={} tickBud={} twBake={} twSkipDyn={} autoTune={} selfBake={}",
        StageConfig::stageLabel(StageConfig::fromSettings()),
        StageConfig::assistModeName(StageConfig::assistMode()),
        StageConfig::assistPickName(),
        StageConfig::ticksAll(),
        StageConfig::ticksWave(),
        StageConfig::windowUnitFrames() ? "Frames" : "Ticks",
        StageConfig::safeMode() ? 1 : 0,
        StageConfig::assistOrbs() ? 1 : 0,
        StageConfig::simBudgetUs(),
        StageConfig::tickBudgetPerPress(),
        StageConfig::triggerBake() ? 1 : 0,
        StageConfig::triggerBakeSkipDynamic() ? 1 : 0,
        StageConfig::autoTune() ? 1 : 0,
        StageConfig::selfBake() ? 1 : 0
    );
}

bool DevTraceStore::beginAttempt(int startTick) {
    if (!this->m_open) return false;
    if (this->hasOpenAttempt()) return false;

    int64_t nextId = 1;
    for (auto const& existing : this->m_session.attempts) {
        if (existing.attemptId >= nextId) nextId = existing.attemptId + 1;
    }

    DevAttempt a;
    a.attemptId = nextId;
    a.startTick = startTick;
    a.sealed = false;
    a.counted = false;
    this->m_session.attempts.push_back(std::move(a));
    this->m_nextEventId = 1;
    return true;
}

int64_t DevTraceStore::allocPressId() {
    if (!this->m_open) return 0;
    return this->m_nextPressId++;
}

int64_t DevTraceStore::nextEventId() {
    return this->m_nextEventId++;
}

DevDecisionEvent* DevTraceStore::appendEvent(DevDecisionEvent event) {
    auto* attempt = this->currentAttempt();
    if (!attempt || attempt->sealed) return nullptr;
    if (event.pressId <= 0) return nullptr;

    if (event.eventId <= 0) {
        event.eventId = this->nextEventId();
    }
    if (event.attemptId <= 0) {
        event.attemptId = attempt->attemptId;
    }
    if (event.cycleId <= 0) {
        event.cycleId = event.pressId;
    }
    attempt->events.push_back(std::move(event));
    return &attempt->events.back();
}

void DevTraceStore::trimRollingWindow() {
    int const target = this->m_session.recordingsTarget;
    if (target < 1) return;

    // Indices of sealed+counted attempts (ascending = oldest first).
    std::vector<size_t> countedIdx;
    for (size_t i = 0; i < this->m_session.attempts.size(); ++i) {
        auto const& a = this->m_session.attempts[i];
        if (a.sealed && a.counted) countedIdx.push_back(i);
    }
    if (static_cast<int>(countedIdx.size()) <= target) {
        this->m_session.recordingsDone = static_cast<int>(countedIdx.size());
        return;
    }

    // Keep from the Nth-last counted attempt onward (drop older attempts).
    size_t const keepFrom = countedIdx[
        countedIdx.size() - static_cast<size_t>(target)
    ];
    if (keepFrom == 0) {
        this->m_session.recordingsDone = target;
        return;
    }
    this->m_session.attempts.erase(
        this->m_session.attempts.begin(),
        this->m_session.attempts.begin()
            + static_cast<std::ptrdiff_t>(keepFrom)
    );
    int kept = 0;
    for (auto const& a : this->m_session.attempts) {
        if (a.sealed && a.counted) ++kept;
    }
    this->m_session.recordingsDone = kept;
}

bool DevTraceStore::sealAttempt(std::string endReason, bool countTowardTarget) {
    auto* attempt = this->currentAttempt();
    if (!attempt || attempt->sealed) return false;
    attempt->endReason = std::move(endReason);
    attempt->sealed = true;
    attempt->counted = countTowardTarget;
    if (countTowardTarget) {
        ++this->m_session.recordingsDone;
        this->trimRollingWindow();
    }
    return this->flush();
}

bool DevTraceStore::writeAtomic(matjson::Value const& root) const {
    auto const dir = this->tracesDir();
    (void)file::createDirectoryAll(dir);
    auto const path = this->sessionPath();
    auto tmpPath = path;
    tmpPath += ".tmp";

    auto w = file::writeToJson(tmpPath, root);
    if (w.isErr()) {
        // Fallback: direct write if temp failed (e.g. path quirks).
        auto d = file::writeToJson(path, root);
        return d.isOk();
    }

    std::error_code ec;
    std::filesystem::rename(tmpPath, path, ec);
    if (!ec) return true;

    // Windows: replace existing.
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(tmpPath, path, ec);
    if (!ec) return true;

    // Last resort: copy then remove tmp.
    std::filesystem::copy_file(
        tmpPath, path,
        std::filesystem::copy_options::overwrite_existing, ec
    );
    std::filesystem::remove(tmpPath, ec);
    return !ec || std::filesystem::exists(path);
}

bool DevTraceStore::flush() {
    if (!this->m_open) return false;
    return this->writeAtomic(sessionToJsonValue(this->m_session));
}

void DevTraceStore::closeSession() {
    if (this->m_open && this->hasOpenAttempt()) {
        // Partial attempt kept with endReason quit-unsealed marker, then seal+flush.
        (void)this->sealAttempt("quit", false);
    }
    else if (this->m_open) {
        (void)this->flush();
    }
    this->m_open = false;
}

DevAttempt* DevTraceStore::currentAttempt() {
    if (!this->m_open || this->m_session.attempts.empty()) return nullptr;
    auto& a = this->m_session.attempts.back();
    if (a.sealed) return nullptr;
    return &a;
}

DevAttempt const* DevTraceStore::currentAttempt() const {
    if (!this->m_open || this->m_session.attempts.empty()) return nullptr;
    auto const& a = this->m_session.attempts.back();
    if (a.sealed) return nullptr;
    return &a;
}

static DevDecisionEvent makeEv(
    DevEventKind kind,
    int64_t eventId,
    int64_t attemptId,
    int64_t pressId,
    int64_t parent,
    int64_t planId
) {
    DevDecisionEvent e;
    e.kind = kind;
    e.eventId = eventId;
    e.attemptId = attemptId;
    e.pressId = pressId;
    e.cycleId = pressId;
    e.parentEventId = parent;
    e.planId = planId;
    e.down = true;
    e.source = DevEventSource::Live;
    return e;
}

bool DevTraceStore::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    expect(std::strcmp(kindId(DevEventKind::PressObserved), "pressObserved") == 0, "kind-press");
    expect(std::strcmp(kindId(DevEventKind::Outcome), "outcome") == 0, "kind-outcome");
    DevEventKind k = DevEventKind::PressObserved;
    expect(parseKind("delayArm", k) && k == DevEventKind::DelayArm, "parse-delayArm");
    expect(!parseKind("nope", k), "parse-kind-fail");

    // Valid Delay chain: press → plan → arm → fire → outcome
    std::vector<DevDecisionEvent> good;
    good.push_back(makeEv(DevEventKind::PressObserved, 1, 1, 10, 0, 0));
    good.push_back(makeEv(DevEventKind::PlanFinished, 2, 1, 10, 1, 42));
    good.push_back(makeEv(DevEventKind::DelayArm, 3, 1, 10, 2, 42));
    good.push_back(makeEv(DevEventKind::Fire, 4, 1, 10, 3, 42));
    good.push_back(makeEv(DevEventKind::Outcome, 5, 1, 10, 4, 42));
    std::string err;
    expect(validatePressChain(good, 10, err), "chain-good");

    // Missing parentEventId on fire
    auto badParent = good;
    badParent[3].parentEventId = 0;
    expect(!validatePressChain(badParent, 10, err), "chain-missing-parent");
    expect(err == "missing-parentEventId", "chain-missing-parent-msg");

    // Parent not in chain
    auto badRef = good;
    badRef[3].parentEventId = 99;
    expect(!validatePressChain(badRef, 10, err), "chain-parent-missing");
    expect(err == "parent-not-in-chain", "chain-parent-missing-msg");

    // Gate cycle: press → gatePass → outcome
    std::vector<DevDecisionEvent> gate;
    gate.push_back(makeEv(DevEventKind::PressObserved, 1, 1, 7, 0, 0));
    gate.push_back(makeEv(DevEventKind::GatePass, 2, 1, 7, 1, 0));
    gate.push_back(makeEv(DevEventKind::Outcome, 3, 1, 7, 2, 0));
    expect(validatePressChain(gate, 7, err), "chain-gate");

    // JSON roundtrip preserves IDs
    DevSessionFile sess;
    sess.traceSchema = kTraceSchema;
    sess.modVersion = "v1.3.0-test";
    sess.sessionId = "selfcheck";
    sess.recordingsTarget = 10;
    sess.recordingsDone = 1;
    DevAttempt att;
    att.attemptId = 1;
    att.startTick = 0;
    att.endReason = "death";
    att.sealed = true;
    att.events = good;
    sess.attempts.push_back(att);

    auto body = sessionToJsonString(sess);
    expect(!body.empty(), "json-nonempty");
    DevSessionFile back;
    std::string jerr;
    expect(sessionFromJsonString(body, back, jerr), "json-parse-ok");
    expect(back.traceSchema == kTraceSchema, "json-schema");
    expect(back.sessionId == "selfcheck", "json-sessionId");
    expect(back.attempts.size() == 1, "json-attempts");
    expect(back.attempts[0].events.size() == 5, "json-events");
    expect(back.attempts[0].events[0].pressId == 10, "json-pressId");
    expect(back.attempts[0].events[1].planId == 42, "json-planId");
    expect(back.attempts[0].events[3].parentEventId == 3, "json-parent");
    expect(validatePressChain(back.attempts[0].events, 10, err), "json-chain");

    // Store API: begin/append/seal/flush (disk) when Mod is available.
    // Constrained devices (Android/iOS) skip disk gold — PlatformLimits::allowLoadDiskSelfCheck.
    if (PlatformLimits::get().allowLoadDiskSelfCheck()) {
        auto& store = DevTraceStore::get();
        bool const wasOpen = store.isOpen();
        DevSessionFile saved = store.session();
        expect(store.beginSession("selfcheck-api", 3), "store-begin");
        store.setModVersion("v1.3.0-test");
        store.setLevelMeta("lvl", "Level", false, true, "fp");
        expect(store.beginAttempt(100), "store-attempt");
        int64_t const pid = store.allocPressId();
        expect(pid > 0, "store-pressId");

        DevDecisionEvent e1 = makeEv(DevEventKind::PressObserved, 0, 0, pid, 0, 0);
        e1.pressTick = 100;
        expect(store.appendEvent(e1) != nullptr, "store-append-press");
        auto* pPlan = store.appendEvent(makeEv(DevEventKind::PlanFinished, 0, 0, pid, 0, 9));
        expect(pPlan != nullptr, "store-append-plan");
        // Fix parent to press eventId after auto-assign.
        auto* cur = store.currentAttempt();
        expect(cur && cur->events.size() >= 2, "store-two-events");
        if (cur && cur->events.size() >= 2) {
            cur->events[1].parentEventId = cur->events[0].eventId;
            cur->events[1].planId = 9;
            auto* arm = store.appendEvent(makeEv(
                DevEventKind::DelayArm, 0, 0, pid, cur->events[1].eventId, 9
            ));
            expect(arm != nullptr, "store-append-arm");
            auto* fire = store.appendEvent(makeEv(
                DevEventKind::Fire, 0, 0, pid, arm->eventId, 9
            ));
            expect(fire != nullptr, "store-append-fire");
            auto* out = store.appendEvent(makeEv(
                DevEventKind::Outcome, 0, 0, pid, fire->eventId, 9
            ));
            expect(out != nullptr, "store-append-outcome");
            expect(validatePressChain(cur->events, pid, err), "store-chain");
        }
        expect(store.sealAttempt("death", true), "store-seal");
        expect(store.session().recordingsDone == 1, "store-count");

        // Reload from disk
        auto path = store.sessionPath();
        expect(std::filesystem::exists(path), "store-file-exists");
        std::ifstream in(path);
        expect(static_cast<bool>(in), "store-file-open");
        std::string diskBody(
            (std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>()
        );
        DevSessionFile diskSess;
        expect(sessionFromJsonString(diskBody, diskSess, jerr), "store-disk-parse");
        expect(diskSess.attempts.size() == 1, "store-disk-attempt");
        if (!diskSess.attempts.empty()) {
            expect(
                validatePressChain(diskSess.attempts[0].events, pid, err),
                "store-disk-chain"
            );
        }

        // Cleanup selfcheck file; restore previous open state lightly.
        store.closeSession();
        std::error_code ec;
        std::filesystem::remove(path, ec);
        if (wasOpen) {
            // Do not resurrect prior session contents; callers must reopen.
            (void)saved;
        }
    }

    if (fails == 0) {
        report = "DevTraceStore selfCheck OK";
    }
    else {
        report = fmt::format("DevTraceStore {} fails: {}", fails, report);
    }
    return fails == 0;
}
