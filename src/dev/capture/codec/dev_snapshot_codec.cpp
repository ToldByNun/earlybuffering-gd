#include "dev_snapshot_codec.hpp"
#include "../dev_clock_contract.hpp"
#include "../dev_live_witness.hpp"
#include "../dev_trace_store.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>
#include <cstdio>
#include <cstdlib>
#include <fmt/format.h>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

matjson::Value EbDevSnapshotCodec::clockStampToJson(EbDevClockContract::Stamp const& st) {
    matjson::Value candidate = matjson::Value::object();
    candidate["clockSchema"] = st.clockSchema;
    candidate["timeBasis"] = st.timeBasis;
    candidate["attemptTick"] = st.attemptTick;
    candidate["effectTick"] = st.effectTick;
    candidate["simDt"] = st.simDt;
    candidate["firstDt"] = st.firstDt;
    candidate["midStep"] = st.midStep;
    candidate["continuous"] = st.continuous;
    candidate["timeWarp"] = st.timeWarp;
    candidate["simTps"] = st.simTps;
    candidate["wallDt"] = st.wallDt;
    candidate["speedScaleSuspect"] = st.speedScaleSuspect;
    candidate["liveSimPathId"] = EbDevClockContract::simPathId(st.liveSimPathId);
    return candidate;
}

bool EbDevSnapshotCodec::clockStampFromJson(matjson::Value const& j, EbDevClockContract::Stamp& st,
                                          std::string& err) {
    st = {};
    st.clockSchema =
        j.contains("clockSchema") ? static_cast<int>(j["clockSchema"].asInt().unwrapOr(0)) : 0;
    st.timeBasis =
        j.contains("timeBasis") ? std::string(j["timeBasis"].asString().unwrapOr("")) : "";
    st.attemptTick =
        j.contains("attemptTick") ? static_cast<int>(j["attemptTick"].asInt().unwrapOr(0)) : 0;
    st.effectTick =
        j.contains("effectTick") ? static_cast<int>(j["effectTick"].asInt().unwrapOr(0)) : 0;
    st.simDt = j.contains("simDt") ? static_cast<float>(j["simDt"].asDouble().unwrapOr(0.0)) : 0.f;
    st.firstDt =
        j.contains("firstDt") ? static_cast<float>(j["firstDt"].asDouble().unwrapOr(0.0)) : 0.f;
    st.midStep = j.contains("midStep") ? j["midStep"].asBool().unwrapOr(false) : false;
    st.continuous = j.contains("continuous") ? j["continuous"].asBool().unwrapOr(false) : false;
    st.timeWarp =
        j.contains("timeWarp") ? static_cast<float>(j["timeWarp"].asDouble().unwrapOr(1.0)) : 1.f;
    st.simTps = j.contains("simTps") ? j["simTps"].asDouble().unwrapOr(0.0) : 0.0;
    st.wallDt =
        j.contains("wallDt") ? static_cast<float>(j["wallDt"].asDouble().unwrapOr(0.0)) : 0.f;
    st.speedScaleSuspect =
        j.contains("speedScaleSuspect") ? j["speedScaleSuspect"].asBool().unwrapOr(false) : false;
    auto path = j.contains("liveSimPathId")
                    ? std::string(j["liveSimPathId"].asString().unwrapOr("none"))
                    : "none";
    if (!EbDevClockContract::parseSimPathId(path, st.liveSimPathId)) {
        err = "bad-liveSimPathId";
        return false;
    }
    st.valid = true;
    return true;
}

matjson::Value EbDevSnapshotCodec::playerBlobToJson(EbDevPlayerBlob const& b) {
    matjson::Value otherIndex = matjson::Value::object();
    otherIndex["present"] = b.present;
    if (!b.present)
        return otherIndex;
    otherIndex["x"] = b.x;
    otherIndex["y"] = b.y;
    otherIndex["rotation"] = b.rotation;
    otherIndex["scaleX"] = b.scaleX;
    otherIndex["scaleY"] = b.scaleY;
    otherIndex["yVelocity"] = b.yVelocity;
    otherIndex["fallSpeed"] = b.fallSpeed;
    otherIndex["gravity"] = b.gravity;
    otherIndex["gravityMod"] = b.gravityMod;
    otherIndex["speedMultiplier"] = b.speedMultiplier;
    otherIndex["playerSpeed"] = b.playerSpeed;
    otherIndex["vehicleSize"] = b.vehicleSize;
    otherIndex["platformerXVelocity"] = b.platformerXVelocity;
    otherIndex["isUpsideDown"] = b.isUpsideDown;
    otherIndex["isSideways"] = b.isSideways;
    otherIndex["isGoingLeft"] = b.isGoingLeft;
    otherIndex["isShip"] = b.isShip;
    otherIndex["isBall"] = b.isBall;
    otherIndex["isBird"] = b.isBird;
    otherIndex["isSwing"] = b.isSwing;
    otherIndex["isDart"] = b.isDart;
    otherIndex["isRobot"] = b.isRobot;
    otherIndex["isSpider"] = b.isSpider;
    otherIndex["isSecondPlayer"] = b.isSecondPlayer;
    otherIndex["isPlatformer"] = b.isPlatformer;
    otherIndex["isDashing"] = b.isDashing;
    otherIndex["isOnGround"] = b.isOnGround;
    otherIndex["isOnGround2"] = b.isOnGround2;
    otherIndex["isOnSlope"] = b.isOnSlope;
    otherIndex["wasOnSlope"] = b.wasOnSlope;
    otherIndex["jumpBuffered"] = b.jumpBuffered;
    otherIndex["wasJumpBuffered"] = b.wasJumpBuffered;
    otherIndex["stateRingJump"] = b.stateRingJump;
    otherIndex["hasEverJumped"] = b.hasEverJumped;
    otherIndex["hasEverHitRing"] = b.hasEverHitRing;
    otherIndex["isLocked"] = b.isLocked;
    otherIndex["controlsDisabled"] = b.controlsDisabled;
    otherIndex["maybeIsFalling"] = b.maybeIsFalling;
    otherIndex["maybeIsBoosted"] = b.maybeIsBoosted;
    return otherIndex;
}

void EbDevSnapshotCodec::playerBlobFromJson(matjson::Value const& j, EbDevPlayerBlob& b) {
    b = {};
    b.present = j.contains("present") ? j["present"].asBool().unwrapOr(false) : false;
    if (!b.present)
        return;
    auto fraction = [&](char const* offset, float& out) {
        if (j.contains(offset))
            out = static_cast<float>(j[offset].asDouble().unwrapOr(0.0));
    };
    auto bo = [&](char const* offset, bool& out) {
        if (j.contains(offset))
            out = j[offset].asBool().unwrapOr(false);
    };
    fraction("x", b.x);
    fraction("y", b.y);
    fraction("rotation", b.rotation);
    fraction("scaleX", b.scaleX);
    fraction("scaleY", b.scaleY);
    fraction("yVelocity", b.yVelocity);
    fraction("fallSpeed", b.fallSpeed);
    fraction("gravity", b.gravity);
    fraction("gravityMod", b.gravityMod);
    fraction("speedMultiplier", b.speedMultiplier);
    fraction("playerSpeed", b.playerSpeed);
    fraction("vehicleSize", b.vehicleSize);
    fraction("platformerXVelocity", b.platformerXVelocity);
    bo("isUpsideDown", b.isUpsideDown);
    bo("isSideways", b.isSideways);
    bo("isGoingLeft", b.isGoingLeft);
    bo("isShip", b.isShip);
    bo("isBall", b.isBall);
    bo("isBird", b.isBird);
    bo("isSwing", b.isSwing);
    bo("isDart", b.isDart);
    bo("isRobot", b.isRobot);
    bo("isSpider", b.isSpider);
    bo("isSecondPlayer", b.isSecondPlayer);
    bo("isPlatformer", b.isPlatformer);
    bo("isDashing", b.isDashing);
    bo("isOnGround", b.isOnGround);
    bo("isOnGround2", b.isOnGround2);
    bo("isOnSlope", b.isOnSlope);
    bo("wasOnSlope", b.wasOnSlope);
    bo("jumpBuffered", b.jumpBuffered);
    bo("wasJumpBuffered", b.wasJumpBuffered);
    bo("stateRingJump", b.stateRingJump);
    bo("hasEverJumped", b.hasEverJumped);
    bo("hasEverHitRing", b.hasEverHitRing);
    bo("isLocked", b.isLocked);
    bo("controlsDisabled", b.controlsDisabled);
    bo("maybeIsFalling", b.maybeIsFalling);
    bo("maybeIsBoosted", b.maybeIsBoosted);
}

matjson::Value EbDevSnapshotCodec::witnessToJson(EbDevLiveWitness const& w) {
    matjson::Value otherIndex = matjson::Value::object();
    otherIndex["playerBlobSchema"] = fmt::format("{:016x}", w.playerBlobSchema);
    otherIndex["freezeWorld"] = w.freezeWorld;
    otherIndex["player"] = EbDevTraceStoreImplementation::playerBlobToJson(w.player);
    if (w.dual.present)
        otherIndex["dual"] = EbDevTraceStoreImplementation::playerBlobToJson(w.dual);
    if (w.clock.valid)
        otherIndex["clock"] = EbDevTraceStoreImplementation::clockStampToJson(w.clock);

    std::vector<matjson::Value> cats;
    cats.reserve(w.categories.size());
    for (auto const& c : w.categories) {
        matjson::Value cj = matjson::Value::object();
        cj["category"] = EbDevLiveWitness::categoryId(c.category);
        cj["status"] = EbDevLiveWitness::statusId(c.status);
        if (c.digest != 0) {
            cj["digest"] = fmt::format("{:016x}", c.digest);
        }
        if (c.digestMismatch)
            cj["digestMismatch"] = true;
        cats.push_back(cj);
    }
    otherIndex["categories"] = cats;

    std::vector<matjson::Value> deps;
    for (int delay : w.worldDeps) {
        deps.push_back(EbDevLiveWitness::categoryId(static_cast<DevWitnessCategory>(delay)));
    }
    otherIndex["worldDeps"] = deps;

    if (!w.orbIds.empty()) {
        std::vector<matjson::Value> orbs;
        orbs.reserve(w.orbIds.size());
        for (int id : w.orbIds)
            orbs.push_back(id);
        otherIndex["orbIds"] = orbs;
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
        otherIndex["moverPoses"] = poses;
    }
    if (!w.triggerStoreKey.empty())
        otherIndex["triggerStoreKey"] = w.triggerStoreKey;
    otherIndex["triggerStoreVersion"] = w.triggerStoreVersion;
    otherIndex["triggerApplyTick"] = w.triggerApplyTick;
    otherIndex["triggerCanSpool"] = w.triggerCanSpool;
    otherIndex["triggerReadyTracks"] = w.triggerReadyTracks;
    otherIndex["triggerTrackCount"] = w.triggerTrackCount;
    otherIndex["triggerBakeEffects"] = w.triggerBakeEffects;
    otherIndex["triggerBakeChildren"] = w.triggerBakeChildren;
    otherIndex["triggerCollectedEffects"] = w.triggerCollectedEffects;
    otherIndex["triggerSkipFollow"] = w.triggerSkipFollow;
    otherIndex["triggerSkipDynamic"] = w.triggerSkipDynamic;
    otherIndex["triggerSkipNonSpawn"] = w.triggerSkipNonSpawn;
    if (w.hasHazardDigest) {
        otherIndex["hazardDigest"] = fmt::format("{:016x}", w.hazardDigest);
    }
    return otherIndex;
}

bool EbDevSnapshotCodec::witnessFromJson(matjson::Value const& j, EbDevLiveWitness& w,
                                       std::string& err) {
    w = {};
    w.valid = true;
    if (j.contains("playerBlobSchema")) {
        auto text = std::string(j["playerBlobSchema"].asString().unwrapOr("0"));
        w.playerBlobSchema = std::strtoull(text.c_str(), nullptr, 16);
    }
    w.freezeWorld = j.contains("freezeWorld") ? j["freezeWorld"].asBool().unwrapOr(false) : false;
    if (j.contains("player"))
        EbDevTraceStoreImplementation::playerBlobFromJson(j["player"], w.player);
    if (j.contains("dual"))
        EbDevTraceStoreImplementation::playerBlobFromJson(j["dual"], w.dual);
    if (j.contains("clock")) {
        std::string clockErr;
        if (!EbDevTraceStoreImplementation::clockStampFromJson(j["clock"], w.clock, clockErr)) {
            err = clockErr.empty() ? "bad-witness-clock" : clockErr;
            return false;
        }
    }
    if (j.contains("categories") && j["categories"].isArray()) {
        for (auto const& cj : j["categories"].asArray().unwrap()) {
            EbDevWitnessCategoryEntry e;
            auto categoryValue = std::string(cj["category"].asString().unwrapOr(""));
            auto state = std::string(cj["status"].asString().unwrapOr("absent"));
            if (!EbDevLiveWitness::parseCategory(categoryValue, e.category)) {
                err = "bad-witness-category";
                return false;
            }
            if (!EbDevLiveWitness::parseStatus(state, e.status)) {
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
            if (!EbDevLiveWitness::parseCategory(name, c)) {
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
            EbDevObjectPose p;
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
    w.triggerCanSpool =
        j.contains("triggerCanSpool") ? j["triggerCanSpool"].asBool().unwrapOr(false) : false;
    auto ti = [&](char const* offset, int& out) {
        if (j.contains(offset))
            out = static_cast<int>(j[offset].asInt().unwrapOr(0));
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
