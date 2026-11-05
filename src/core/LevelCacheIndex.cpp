#include "LevelCacheIndex.hpp"

#include "BakeMap.hpp"
#include "ClickBakeStore.hpp"
#include "LevelCacheAtomicIo.hpp"
#include "LevelFingerprint.hpp"
#include "../debug/AssistLog.hpp"

#include <Geode/utils/file.hpp>
#include <algorithm>
#include <chrono>
#include <fmt/format.h>
#include <fstream>
#include <system_error>

using namespace geode::prelude;

namespace {

std::filesystem::path saveRoot() {
    if (auto* mod = Mod::get()) return mod->getSaveDir();
    return std::filesystem::path("earlybuffer-save");
}

} // namespace

std::filesystem::path LevelCacheIndex::rootDir() {
    return saveRoot() / "level-cache";
}

std::filesystem::path LevelCacheIndex::levelDir(std::string const& fullKey) {
    return rootDir() / fullKey;
}

std::filesystem::path LevelCacheIndex::shardDir(
    std::string const& fullKey, int startXq
) {
    auto const shard = LevelFingerprint::shardFolderName(startXq);
    if (shard.empty()) return levelDir(fullKey);
    return levelDir(fullKey) / shard;
}

std::filesystem::path LevelCacheIndex::metaPath(
    std::string const& fullKey, int startXq
) {
    return shardDir(fullKey, startXq) / "meta.json";
}

char const* LevelCacheIndex::clicksFileName(std::string const& method) {
    auto const m = normalizeMethod(method);
    if (m == "brute") return "clicks-brute.json";
    if (m == "self") return "clicks-self.json";
    return "clicks-replay.json";
}

std::string LevelCacheIndex::normalizeMethod(std::string const& method) {
    if (method == "brute" || method == "BruteForce" || method == "bruteforce") {
        return "brute";
    }
    if (method == "self") return "self";
    return "replay";
}

std::filesystem::path LevelCacheIndex::clicksPath(
    std::string const& fullKey,
    std::string const& method,
    int startXq
) {
    return shardDir(fullKey, startXq) / clicksFileName(method);
}

std::filesystem::path LevelCacheIndex::routePath(
    std::string const& fullKey, int startXq
) {
    return shardDir(fullKey, startXq) / "route.json";
}

std::filesystem::path LevelCacheIndex::triggersPath(std::string const& fullKey) {
    return levelDir(fullKey) / "triggers.json";
}

std::filesystem::path LevelCacheIndex::profilePath(std::string const& fullKey) {
    return levelDir(fullKey) / "profile.json";
}

std::filesystem::path LevelCacheIndex::legacyBakesDir() {
    return saveRoot() / "bakes";
}

std::filesystem::path LevelCacheIndex::legacyBakePath(std::string const& fullKey) {
    return legacyBakesDir() / (fullKey + ".json");
}

std::filesystem::path LevelCacheIndex::legacyTriggersDir() {
    return saveRoot() / "triggers";
}

std::filesystem::path LevelCacheIndex::legacyTriggerPath(std::string const& fullKey) {
    return legacyTriggersDir() / (fullKey + ".json");
}

std::filesystem::path LevelCacheIndex::legacyLevelsDir() {
    return saveRoot() / "levels";
}

std::filesystem::path LevelCacheIndex::legacyProfilePath(std::string const& fullKey) {
    return legacyLevelsDir() / (fullKey + ".json");
}

bool LevelCacheIndex::ensureRoot(std::string& err) {
    err.clear();
    auto res = file::createDirectoryAll(rootDir());
    if (res.isErr()) {
        err = fmt::format(
            "cannot create level-cache {}: {}",
            rootDir().string(),
            res.unwrapErr()
        );
        return false;
    }
    return true;
}

bool LevelCacheIndex::ensureLevelDir(std::string const& fullKey, std::string& err) {
    err.clear();
    if (fullKey.empty()) {
        err = "fullKey empty";
        return false;
    }
    if (!ensureRoot(err)) return false;
    auto res = file::createDirectoryAll(levelDir(fullKey));
    if (res.isErr()) {
        err = fmt::format(
            "cannot create {}: {}",
            levelDir(fullKey).string(),
            res.unwrapErr()
        );
        return false;
    }
    return true;
}

bool LevelCacheIndex::ensureShardDir(
    std::string const& fullKey, int startXq, std::string& err
) {
    if (!ensureLevelDir(fullKey, err)) return false;
    auto const dir = shardDir(fullKey, startXq);
    if (dir == levelDir(fullKey)) return true;
    auto res = file::createDirectoryAll(dir);
    if (res.isErr()) {
        err = fmt::format(
            "cannot create shard {}: {}", dir.string(), res.unwrapErr()
        );
        return false;
    }
    return true;
}

std::string LevelCacheIndex::nowUtcIso() {
    using clock = std::chrono::system_clock;
    auto const t = clock::to_time_t(clock::now());
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32]{};
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
}

int64_t LevelCacheIndex::dirBytesApprox(std::filesystem::path const& dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec) || ec) return 0;
    int64_t sum = 0;
    for (auto const& entry : std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec) || ec) continue;
        auto const sz = entry.file_size(ec);
        if (!ec) sum += static_cast<int64_t>(sz);
    }
    return sum;
}

geode::Result<> LevelCacheIndex::writeMeta(Meta const& meta) {
    if (meta.fullKey.empty()) return Err("meta fullKey empty");
    std::string err;
    if (!ensureShardDir(meta.fullKey, meta.startXq, err)) return Err(err);

    Meta out = meta;
    if (out.schema <= 0) out.schema = kSchema;
    if (out.updatedAt.empty()) out.updatedAt = nowUtcIso();
    // bytesApprox covers the whole level tree (all shards).
    out.bytesApprox = dirBytesApprox(levelDir(out.fullKey));

    matjson::Value root = matjson::Value::object();
    root["schema"] = out.schema;
    root["fullKey"] = out.fullKey;
    root["startXq"] = out.startXq;
    root["modVersion"] = out.modVersion;
    root["gdVersion"] = out.gdVersion;
    root["tps"] = out.tps;
    root["bakeRadius"] = out.bakeRadius;
    root["settingsFp"] = out.settingsFp;
    root["contentRev"] = out.contentRev;
    root["objectCount"] = out.objectCount;
    root["updatedAt"] = out.updatedAt;
    root["stale"] = out.stale;
    root["bytesApprox"] = out.bytesApprox;
    return LevelCacheAtomicIo::writeJson(metaPath(out.fullKey, out.startXq), root);
}

geode::Result<LevelCacheIndex::Meta> LevelCacheIndex::readMeta(
    std::string const& fullKey, int startXq
) {
    auto res = LevelCacheAtomicIo::readJson(metaPath(fullKey, startXq));
    if (res.isErr()) return Err(res.unwrapErr());
    auto root = res.unwrap();

    Meta m;
    m.schema = static_cast<int>(root["schema"].asInt().unwrapOr(0));
    m.fullKey = root.contains("fullKey")
        ? root["fullKey"].asString().unwrapOr(fullKey)
        : fullKey;
    m.startXq = static_cast<int>(root["startXq"].asInt().unwrapOr(startXq));
    m.modVersion = root.contains("modVersion")
        ? root["modVersion"].asString().unwrapOr("")
        : "";
    m.gdVersion = root.contains("gdVersion")
        ? root["gdVersion"].asString().unwrapOr("")
        : "";
    m.tps = root.contains("tps") ? root["tps"].asDouble().unwrapOr(240.0) : 240.0;
    m.bakeRadius = static_cast<int>(root["bakeRadius"].asInt().unwrapOr(0));
    m.settingsFp = root.contains("settingsFp")
        ? root["settingsFp"].asString().unwrapOr("")
        : "";
    m.contentRev = root.contains("contentRev")
        ? root["contentRev"].asString().unwrapOr("")
        : "";
    m.objectCount = static_cast<int>(root["objectCount"].asInt().unwrapOr(0));
    m.updatedAt = root.contains("updatedAt")
        ? root["updatedAt"].asString().unwrapOr("")
        : "";
    m.stale = root.contains("stale") ? root["stale"].asBool().unwrapOr(false) : false;
    m.bytesApprox = root.contains("bytesApprox")
        ? root["bytesApprox"].asInt().unwrapOr(0)
        : 0;

    if (m.schema > kSchema) m.stale = true;
    return Ok(std::move(m));
}

geode::Result<> LevelCacheIndex::copyFileIfPresent(
    std::filesystem::path const& from,
    std::filesystem::path const& to
) {
    if (!LevelCacheAtomicIo::existsRegular(from)) return Ok();
    if (LevelCacheAtomicIo::existsRegular(to)) return Ok();
    auto parent = to.parent_path();
    if (!parent.empty()) {
        auto mk = file::createDirectoryAll(parent);
        if (mk.isErr()) return Err(mk.unwrapErr());
    }
    auto json = LevelCacheAtomicIo::readJson(from);
    if (json.isOk()) {
        return LevelCacheAtomicIo::writeJson(to, json.unwrap());
    }
    std::ifstream in(from, std::ios::binary);
    if (!in) return Err(fmt::format("open failed {}", from.string()));
    std::string body(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>()
    );
    auto tmp = to;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return Err(fmt::format("open tmp failed {}", tmp.string()));
        out << body;
        if (!out) return Err(fmt::format("write tmp failed {}", tmp.string()));
    }
    std::error_code ec;
    std::filesystem::rename(tmp, to, ec);
    if (!ec) return Ok();
    std::filesystem::remove(to, ec);
    ec.clear();
    std::filesystem::rename(tmp, to, ec);
    if (!ec) return Ok();
    return Err(ec.message());
}

bool LevelCacheIndex::migrateLegacyIfNeeded(
    std::string const& fullKey,
    std::string& log
) {
    log.clear();
    if (fullKey.empty()) {
        log = "empty-key";
        return false;
    }

    std::error_code ec;
    auto const cacheDir = levelDir(fullKey);
    bool const cacheExists = std::filesystem::is_directory(cacheDir, ec) && !ec;

    bool const hasClicks =
        LevelCacheAtomicIo::existsRegular(clicksPath(fullKey, "replay", 0))
        || LevelCacheAtomicIo::existsRegular(clicksPath(fullKey, "brute", 0))
        || LevelCacheAtomicIo::existsRegular(clicksPath(fullKey, "self", 0));
    bool const hasMeta = LevelCacheAtomicIo::existsRegular(metaPath(fullKey, 0));

    auto const legacyBake = legacyBakePath(fullKey);
    bool const hasLegacyBake = LevelCacheAtomicIo::existsRegular(legacyBake);

    if (!cacheExists && !hasLegacyBake
        && !LevelCacheAtomicIo::existsRegular(legacyTriggerPath(fullKey))
        && !LevelCacheAtomicIo::existsRegular(legacyProfilePath(fullKey))) {
        log = "nothing-to-migrate";
        return true;
    }

    std::string err;
    if (!ensureLevelDir(fullKey, err)) {
        log = err;
        return false;
    }

    if (!hasClicks && hasLegacyBake) {
        auto raw = LevelCacheAtomicIo::readJson(legacyBake);
        if (raw.isErr()) {
            log = fmt::format("legacy-bake-unreadable: {}", raw.unwrapErr());
        }
        else {
            BakeMapData data;
            data.levelKey = fullKey;
            auto parsed = ClickBakeStore::parseClicksJson(raw.unwrap(), data);
            if (parsed.isErr()) {
                log = fmt::format("legacy-bake-parse: {}", parsed.unwrapErr());
            }
            else {
                auto const method = normalizeMethod(data.method);
                data.method = method;
                auto w = LevelCacheAtomicIo::writeJson(
                    clicksPath(fullKey, method, 0),
                    ClickBakeStore::clicksToJson(data)
                );
                if (w.isErr()) {
                    log = fmt::format("clicks-write: {}", w.unwrapErr());
                    return false;
                }
                if (!log.empty()) log += "; ";
                log += fmt::format("migrated-bake→clicks-{}", method);
            }
        }
    }

    auto twCopy = copyFileIfPresent(
        legacyTriggerPath(fullKey), triggersPath(fullKey)
    );
    if (twCopy.isErr()) {
        if (!log.empty()) log += "; ";
        log += fmt::format("triggers-copy: {}", twCopy.unwrapErr());
    }
    else if (LevelCacheAtomicIo::existsRegular(legacyTriggerPath(fullKey))
        && LevelCacheAtomicIo::existsRegular(triggersPath(fullKey))) {
        if (!log.empty()) log += "; ";
        log += "copied-triggers";
    }

    auto prCopy = copyFileIfPresent(
        legacyProfilePath(fullKey), profilePath(fullKey)
    );
    if (prCopy.isErr()) {
        if (!log.empty()) log += "; ";
        log += fmt::format("profile-copy: {}", prCopy.unwrapErr());
    }
    else if (LevelCacheAtomicIo::existsRegular(legacyProfilePath(fullKey))
        && LevelCacheAtomicIo::existsRegular(profilePath(fullKey))) {
        if (!log.empty()) log += "; ";
        log += "copied-profile";
    }

    if (!hasMeta) {
        Meta meta;
        meta.schema = kSchema;
        meta.fullKey = fullKey;
        meta.startXq = 0;
        meta.settingsFp = LevelFingerprint::settingsFp();
        if (auto* mod = Mod::get()) {
            meta.modVersion = mod->getVersion().toVString();
        }
        meta.gdVersion = "2.2081";
        for (char const* method : {"replay", "brute", "self"}) {
            auto const cp = clicksPath(fullKey, method, 0);
            auto raw = LevelCacheAtomicIo::readJson(cp);
            if (raw.isErr()) continue;
            BakeMapData probe;
            probe.levelKey = fullKey;
            if (ClickBakeStore::parseClicksJson(raw.unwrap(), probe).isOk()
                && probe.tps > 0.0) {
                meta.tps = probe.tps;
                break;
            }
        }
        auto mw = writeMeta(meta);
        if (mw.isErr()) {
            if (!log.empty()) log += "; ";
            log += fmt::format("meta-write: {}", mw.unwrapErr());
            return false;
        }
        if (!log.empty()) log += "; ";
        log += "wrote-meta";
    }

    if (log.empty()) log = cacheExists ? "cache-present" : "migrated";
    return true;
}

bool LevelCacheIndex::clearLevel(std::string const& fullKey, std::string& err) {
    err.clear();
    if (fullKey.empty()) {
        err = "fullKey empty";
        return false;
    }
    auto const dir = levelDir(fullKey);
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || ec) {
        return true; // already gone
    }
    std::filesystem::remove_all(dir, ec);
    if (ec) {
        err = fmt::format("clear {}: {}", dir.string(), ec.message());
        return false;
    }
    return true;
}

bool LevelCacheIndex::clearAll(std::string& err) {
    err.clear();
    auto const root = rootDir();
    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || ec) {
        return true;
    }
    std::filesystem::remove_all(root, ec);
    if (ec) {
        err = fmt::format("clear-all {}: {}", root.string(), ec.message());
        return false;
    }
    (void)ensureRoot(err);
    return err.empty();
}

int64_t LevelCacheIndex::totalBytesApprox() {
    return dirBytesApprox(rootDir());
}

std::vector<LevelCacheIndex::LevelEntry> LevelCacheIndex::listLevelEntries() {
    std::vector<LevelEntry> out;
    std::error_code ec;
    auto const root = rootDir();
    if (!std::filesystem::is_directory(root, ec) || ec) return out;
    for (auto const& entry : std::filesystem::directory_iterator(root, ec)) {
        if (ec) break;
        if (!entry.is_directory(ec) || ec) continue;
        LevelEntry e;
        e.fullKey = entry.path().filename().string();
        if (e.fullKey.empty() || e.fullKey == "." || e.fullKey == "..") continue;
        e.bytes = dirBytesApprox(entry.path());
        // Prefer root meta updatedAt; else newest shard meta; else epoch.
        e.updatedAt = "1970-01-01T00:00:00Z";
        auto rootMeta = readMeta(e.fullKey, 0);
        if (rootMeta.isOk() && !rootMeta.unwrap().updatedAt.empty()) {
            e.updatedAt = rootMeta.unwrap().updatedAt;
        }
        else {
            std::error_code ec2;
            for (auto const& sub : std::filesystem::directory_iterator(entry.path(), ec2)) {
                if (ec2 || !sub.is_directory(ec2)) continue;
                auto const name = sub.path().filename().string();
                if (name.rfind("sp_", 0) != 0) continue;
                // Parse startXq after "sp_"
                try {
                    int const xq = std::stoi(name.substr(3));
                    auto sm = readMeta(e.fullKey, xq);
                    if (sm.isOk() && sm.unwrap().updatedAt > e.updatedAt) {
                        e.updatedAt = sm.unwrap().updatedAt;
                    }
                }
                catch (...) {
                }
            }
        }
        out.push_back(std::move(e));
    }
    return out;
}

int LevelCacheIndex::evictLruIfOverCap() {
    auto total = totalBytesApprox();
    if (total <= kSoftCapBytes) return 0;

    auto entries = listLevelEntries();
    std::sort(entries.begin(), entries.end(), [](LevelEntry const& a, LevelEntry const& b) {
        if (a.updatedAt != b.updatedAt) return a.updatedAt < b.updatedAt;
        return a.fullKey < b.fullKey;
    });

    int removed = 0;
    for (auto const& e : entries) {
        if (total <= kSoftCapBytes) break;
        std::string err;
        if (!clearLevel(e.fullKey, err)) {
            AssistLog::debug(fmt::format("LEVEL-CACHE LRU skip {}: {}", e.fullKey, err));
            continue;
        }
        total -= e.bytes;
        if (total < 0) total = totalBytesApprox();
        ++removed;
        AssistLog::info(fmt::format(
            "LEVEL-CACHE LRU evict {} ({} bytes)", e.fullKey, e.bytes
        ));
    }
    return removed;
}

std::string LevelCacheIndex::overlayBit() {
    auto const bytes = totalBytesApprox();
    if (bytes <= 0) return {};
    double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (bytes > kSoftCapBytes) {
        return fmt::format("cache={:.0f}MB LRU", mb);
    }
    if (mb < 10.0) {
        return fmt::format("cache={:.1f}MB", mb);
    }
    return fmt::format("cache={:.0f}MB", mb);
}

bool LevelCacheIndex::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    expect(normalizeMethod("brute") == "brute", "norm-brute");
    expect(normalizeMethod("self") == "self", "norm-self");
    expect(normalizeMethod("") == "replay", "norm-default");
    expect(
        std::string(clicksFileName("replay")) == "clicks-replay.json",
        "fname-replay"
    );

    auto const key = std::string("id_selfcheck_rev_deadbeef");
    expect(
        levelDir(key).string().find(key) != std::string::npos,
        "level-dir"
    );
    expect(shardDir(key, 0) == levelDir(key), "shard-root");
    expect(
        shardDir(key, 5).filename() == "sp_5",
        "shard-5"
    );
    expect(
        clicksPath(key, "replay", 5).filename() == "clicks-replay.json",
        "shard-clicks"
    );
    expect(
        clicksPath(key, "replay", 5).parent_path().filename() == "sp_5",
        "shard-parent"
    );
    expect(triggersPath(key).parent_path() == levelDir(key), "tw-root");
    expect(kSoftCapBytes == 256LL * 1024 * 1024, "soft-cap");
    expect(kSchema == 1, "schema");

    // Live cross-contamination + clear when Mod is available.
    if (Mod::get()) {
        auto const liveKey = std::string("id_selfcheck_shard_rev_11");
        std::string err;
        (void)clearLevel(liveKey, err);

        BakeMapData a;
        a.levelKey = liveKey;
        a.method = "replay";
        a.tps = 240.0;
        BakedClick c;
        c.x = 100.f;
        c.nominalFrame = 1;
        c.earliest = 1;
        c.latest = 2;
        a.clicks.push_back(c);
        expect(ClickBakeStore::saveMethod(a, 0).isOk(), "save-xq0");

        BakeMapData b = a;
        b.clicks[0].x = 999.f;
        expect(ClickBakeStore::saveMethod(b, 7).isOk(), "save-xq7");

        expect(
            LevelCacheAtomicIo::existsRegular(clicksPath(liveKey, "replay", 0)),
            "file-xq0"
        );
        expect(
            LevelCacheAtomicIo::existsRegular(clicksPath(liveKey, "replay", 7)),
            "file-xq7"
        );

        BakeMapData load0;
        BakeMapData load7;
        expect(ClickBakeStore::loadMerged(liveKey, load0, 0).isOk(), "load-0");
        expect(ClickBakeStore::loadMerged(liveKey, load7, 7).isOk(), "load-7");
        expect(load0.clicks.size() == 1 && load0.clicks[0].x == 100.f, "iso-0");
        expect(load7.clicks.size() == 1 && load7.clicks[0].x == 999.f, "iso-7");

        Meta m0;
        m0.fullKey = liveKey;
        m0.startXq = 0;
        m0.settingsFp = LevelFingerprint::settingsFp();
        expect(writeMeta(m0).isOk(), "meta-0");
        auto rm0 = readMeta(liveKey, 0);
        expect(rm0.isOk() && !rm0.unwrap().settingsFp.empty(), "meta-fp");

        expect(clearLevel(liveKey, err), "clear-level");
        expect(
            !LevelCacheAtomicIo::existsRegular(clicksPath(liveKey, "replay", 0)),
            "cleared"
        );

        // overlayBit must not crash on empty/partial trees.
        (void)overlayBit();
        (void)evictLruIfOverCap();
    }

    if (fails == 0) {
        report = "LevelCacheIndex selfCheck OK";
        return true;
    }
    report = fmt::format("LevelCacheIndex selfCheck {} fails: {}", fails, report);
    return false;
}
