#include "ClickBakeStore.hpp"

#include "BakeMap.hpp"
#include "LevelCacheAtomicIo.hpp"
#include "LevelCacheIndex.hpp"
#include "LevelFingerprint.hpp"
#include "../config/StageConfig.hpp"
#include "../debug/AssistLog.hpp"

#include <Geode/utils/file.hpp>
#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <fstream>
#include <system_error>

using namespace geode::prelude;

matjson::Value ClickBakeStore::clicksToJson(BakeMapData const& data) {
    matjson::Value root = matjson::Value::object();
    root["schema"] = kFileSchema;
    root["version"] = 1; // legacy BakeMap field retained for dual readability
    root["key"] = data.levelKey;
    root["source"] = data.sourceReplay;
    root["tps"] = data.tps;
    root["method"] = LevelCacheIndex::normalizeMethod(data.method);

    std::vector<matjson::Value> arr;
    arr.reserve(data.clicks.size());
    for (auto const& c : data.clicks) {
        matjson::Value j = matjson::Value::object();
        j["nominal"] = c.nominalFrame;
        j["earliest"] = c.earliest;
        j["latest"] = c.latest;
        j["down"] = c.down;
        j["player2"] = c.player2;
        j["x"] = c.x;
        j["mode"] = StageConfig::kindName(c.mode);
        if (c.holdAssist) j["holdAssist"] = true;
        if (c.replayTiming && data.method == "replay") j["replayTiming"] = true;
        arr.push_back(std::move(j));
    }
    root["clicks"] = arr;
    return root;
}

geode::Result<> ClickBakeStore::parseClicksJson(
    matjson::Value const& root,
    BakeMapData& out
) {
    if (root.contains("source")) {
        out.sourceReplay = root["source"].asString().unwrapOr("");
    }
    if (root.contains("tps")) {
        out.tps = root["tps"].asDouble().unwrapOr(240.0);
    }
    if (out.tps <= 0.0) out.tps = 240.0;
    if (root.contains("method")) {
        out.method = root["method"].asString().unwrapOr("replay");
    }
    out.method = LevelCacheIndex::normalizeMethod(out.method);

    out.clicks.clear();
    if (!root.contains("clicks") || !root["clicks"].isArray()) return Ok();

    for (auto const& j : root["clicks"]) {
        BakedClick c;
        c.nominalFrame = static_cast<int>(j["nominal"].asInt().unwrapOr(0));
        c.earliest = static_cast<int>(j["earliest"].asInt().unwrapOr(c.nominalFrame));
        c.latest = static_cast<int>(j["latest"].asInt().unwrapOr(c.nominalFrame));
        c.down = j["down"].asBool().unwrapOr(true);
        c.replayTiming = out.method == "replay" && j["replayTiming"].asBool().unwrapOr(false);
        c.player2 = j["player2"].asBool().unwrapOr(false);
        c.x = static_cast<float>(j["x"].asDouble().unwrapOr(0.0));
        c.holdAssist = j.contains("holdAssist")
            ? j["holdAssist"].asBool().unwrapOr(false)
            : false;
        auto modeName = j.contains("mode")
            ? j["mode"].asString().unwrapOr("cube")
            : "cube";
        c.mode = GameModeKind::Cube;
        for (int i = 0; i < StageConfig::MODE_COUNT; ++i) {
            if (modeName == StageConfig::kindName(static_cast<GameModeKind>(i))) {
                c.mode = static_cast<GameModeKind>(i);
                break;
            }
        }
        out.clicks.push_back(c);
    }
    return Ok();
}

geode::Result<> ClickBakeStore::readOneSource(
    std::filesystem::path const& path,
    BakeMapData& out
) {
    auto res = LevelCacheAtomicIo::readJson(path);
    if (res.isErr()) return Err(res.unwrapErr());
    return parseClicksJson(res.unwrap(), out);
}

void ClickBakeStore::appendUniqueClicks(
    BakeMapData& dst,
    BakeMapData const& src
) {
    for (auto const& neu : src.clicks) {
        bool dup = false;
        for (auto const& old : dst.clicks) {
            if (old.down != neu.down || old.player2 != neu.player2) continue;
            if (std::fabs(old.x - neu.x) > BakeMap::MERGE_X_TOL) continue;
            if (std::abs(old.nominalFrame - neu.nominalFrame) > BakeMap::MERGE_FRAME_TOL) {
                continue;
            }
            dup = true;
            break;
        }
        if (dup) continue;
        if (dst.clicks.size() >= BakeMap::kMaxMapClicks) break;
        dst.clicks.push_back(neu);
    }
}

geode::Result<> ClickBakeStore::loadMerged(
    std::string const& fullKey,
    BakeMapData& out,
    int startXq
) {
    out = {};
    out.levelKey = fullKey;
    out.tps = 240.0;
    out.method.clear();

    if (fullKey.empty()) return Err("fullKey empty");

    // Legacy flat bake migrates into startXq=0 only.
    if (startXq == 0) {
        std::string migLog;
        if (!LevelCacheIndex::migrateLegacyIfNeeded(fullKey, migLog)) {
            AssistLog::debug(fmt::format(
                "LEVEL-CACHE migrate fail {}: {}", fullKey, migLog
            ));
        }
        else if (migLog.find("migrated") != std::string::npos
            || migLog.find("copied") != std::string::npos) {
            AssistLog::info(fmt::format("LEVEL-CACHE {}", migLog));
        }
    }

    BakeMapData primary;
    primary.levelKey = fullKey;
    bool any = false;

    for (char const* method : {"replay", "brute", "self"}) {
        BakeMapData part;
        part.levelKey = fullKey;
        auto const path = LevelCacheIndex::clicksPath(fullKey, method, startXq);
        auto r = readOneSource(path, part);
        if (r.isErr()) continue;
        part.method = method;
        if (!any) {
            primary = part;
            any = true;
        }
        else {
            appendUniqueClicks(primary, part);
            if (primary.tps <= 0.0 && part.tps > 0.0) primary.tps = part.tps;
        }
    }

    if (any) {
        out = std::move(primary);
        out.levelKey = fullKey;
        return Ok();
    }

    // Legacy fallback only for normal start (startXq==0).
    if (startXq == 0) {
        auto legacy = LevelCacheAtomicIo::readJson(
            LevelCacheIndex::legacyBakePath(fullKey)
        );
        if (legacy.isOk()) {
            out.levelKey = fullKey;
            auto p = parseClicksJson(legacy.unwrap(), out);
            if (p.isOk()) return Ok();
            return p;
        }
    }

    out.levelKey = fullKey;
    out.tps = 240.0;
    out.method.clear();
    out.clicks.clear();
    out.sourceReplay.clear();
    return Ok();
}

geode::Result<> ClickBakeStore::saveMethod(
    BakeMapData const& data,
    int startXq
) {
    if (data.levelKey.empty()) return Err("levelKey empty");

    std::string err;
    if (!LevelCacheIndex::ensureShardDir(data.levelKey, startXq, err)) {
        return Err(err);
    }

    BakeMapData written = data;
    written.method = LevelCacheIndex::normalizeMethod(
        data.method.empty() ? "self" : data.method
    );
    auto const path = LevelCacheIndex::clicksPath(
        data.levelKey, written.method, startXq
    );
    auto w = LevelCacheAtomicIo::writeJson(path, clicksToJson(written));
    if (w.isErr()) return w;

    LevelCacheIndex::Meta meta;
    auto existing = LevelCacheIndex::readMeta(data.levelKey, startXq);
    if (existing.isOk()) meta = existing.unwrap();
    meta.schema = LevelCacheIndex::kSchema;
    meta.fullKey = data.levelKey;
    meta.startXq = startXq;
    meta.tps = written.tps > 0.0 ? written.tps : meta.tps;
    meta.settingsFp = LevelFingerprint::settingsFp();
    meta.stale = false;
    if (auto* mod = Mod::get()) {
        meta.modVersion = mod->getVersion().toVString();
    }
    meta.gdVersion = "2.2081";
    auto mw = LevelCacheIndex::writeMeta(meta);
    if (mw.isErr()) {
        AssistLog::debug(fmt::format(
            "LEVEL-CACHE meta save: {}", mw.unwrapErr()
        ));
    }
    (void)LevelCacheIndex::evictLruIfOverCap();
    return Ok();
}

bool ClickBakeStore::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    // Isolated temp tree — do not touch the live mod save dir.
    std::filesystem::path tmpRoot;
    try {
        tmpRoot = std::filesystem::temp_directory_path()
            / "earlybuffer-clickbake-selfcheck";
    }
    catch (...) {
        report = "ClickBakeStore selfCheck fails: temp-dir";
        return false;
    }

    std::error_code ec;
    std::filesystem::remove_all(tmpRoot, ec);
    ec.clear();
    std::filesystem::create_directories(tmpRoot / "bakes", ec);
    std::filesystem::create_directories(tmpRoot / "level-cache", ec);
    expect(!ec, "mkdir");

    auto const key = std::string("id_selfcheck_rev_cafebabe");
    auto const legacyPath = tmpRoot / "bakes" / (key + ".json");

    BakeMapData sample;
    sample.levelKey = key;
    sample.sourceReplay = "fixture.gdr";
    sample.tps = 240.0;
    sample.method = "replay";
    BakedClick c;
    c.nominalFrame = 10;
    c.earliest = 8;
    c.latest = 12;
    c.down = true;
    c.x = 100.f;
    c.mode = GameModeKind::Ship;
    sample.clicks.push_back(c);

    // Write a legacy bake file (direct JSON, simulates old install).
    {
        auto w = LevelCacheAtomicIo::writeJson(legacyPath, clicksToJson(sample));
        expect(w.isOk(), "legacy-seed");
    }

    // Roundtrip parse.
    {
        BakeMapData parsed;
        parsed.levelKey = key;
        auto raw = LevelCacheAtomicIo::readJson(legacyPath);
        expect(raw.isOk(), "legacy-read");
        if (raw.isOk()) {
            expect(parseClicksJson(raw.unwrap(), parsed).isOk(), "parse");
            expect(parsed.clicks.size() == 1, "parse-n");
            expect(parsed.clicks[0].mode == GameModeKind::Ship, "parse-mode");
            expect(parsed.method == "replay", "parse-method");
        }
    }

    // Atomic write fail simulation: parent is a file.
    {
        auto const blocked = tmpRoot / "blocked-file";
        {
            std::ofstream f(blocked, std::ios::binary | std::ios::trunc);
            f << "x";
        }
        auto fail = LevelCacheAtomicIo::writeJson(blocked / "x.json", clicksToJson(sample));
        expect(fail.isErr(), "disk-full-sim");
    }

    // Corrupt clicks JSON → parse still safe (caller gets Err from readJson).
    {
        auto const bad = tmpRoot / "bad.json";
        {
            std::ofstream f(bad, std::ios::binary | std::ios::trunc);
            f << "{bad";
        }
        auto corrupt = LevelCacheAtomicIo::readJson(bad);
        expect(corrupt.isErr(), "corrupt");
    }

    // Method file names + normalize.
    expect(
        LevelCacheIndex::normalizeMethod("BruteForce") == "brute",
        "norm-brute"
    );

    std::filesystem::remove_all(tmpRoot, ec);

    // Live migrate path only when Mod is available (saveDir writable).
    if (Mod::get()) {
        auto const liveKey = std::string("id_selfcheck_live_rev_00");
        auto const liveLegacy = LevelCacheIndex::legacyBakePath(liveKey);
        (void)file::createDirectoryAll(LevelCacheIndex::legacyBakesDir());
        auto seed = LevelCacheAtomicIo::writeJson(liveLegacy, clicksToJson(sample));
        expect(seed.isOk(), "live-seed");

        // Remove cache dir so migrate runs.
        std::filesystem::remove_all(LevelCacheIndex::levelDir(liveKey), ec);

        BakeMapData loaded;
        auto lr = loadMerged(liveKey, loaded);
        expect(lr.isOk(), "live-load");
        expect(loaded.clicks.size() == 1, "live-clicks");
        expect(
            LevelCacheAtomicIo::existsRegular(
                LevelCacheIndex::clicksPath(liveKey, "replay")
            ),
            "live-cache-clicks"
        );
        expect(
            LevelCacheAtomicIo::existsRegular(LevelCacheIndex::metaPath(liveKey)),
            "live-meta"
        );
        // Legacy kept.
        expect(LevelCacheAtomicIo::existsRegular(liveLegacy), "legacy-kept");

        // saveMethod writes only that method; second source untouched.
        BakeMapData brute = sample;
        brute.method = "brute";
        brute.clicks[0].x = 200.f;
        expect(saveMethod(brute).isOk(), "save-brute");
        expect(
            LevelCacheAtomicIo::existsRegular(
                LevelCacheIndex::clicksPath(liveKey, "brute")
            ),
            "brute-file"
        );
        expect(
            LevelCacheAtomicIo::existsRegular(
                LevelCacheIndex::clicksPath(liveKey, "replay")
            ),
            "replay-untouched"
        );

        BakeMapData merged;
        expect(loadMerged(liveKey, merged).isOk(), "reload-merge");
        expect(merged.clicks.size() >= 2, "merged-both");

        // Cleanup selfcheck artifacts.
        std::filesystem::remove_all(LevelCacheIndex::levelDir(liveKey), ec);
        std::filesystem::remove(liveLegacy, ec);
    }

    if (fails == 0) {
        report = "ClickBakeStore selfCheck OK";
        return true;
    }
    report = fmt::format("ClickBakeStore selfCheck {} fails: {}", fails, report);
    return false;
}
