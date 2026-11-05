#include "LevelCacheIndex.hpp"

#include "BakeMap.hpp"
#include "ClickBakeStore.hpp"
#include "LevelCacheAtomicIo.hpp"

#include <Geode/utils/file.hpp>
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

std::filesystem::path LevelCacheIndex::metaPath(std::string const& fullKey) {
    return levelDir(fullKey) / "meta.json";
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
    // Empty / unknown → replay slot (legacy default).
    return "replay";
}

std::filesystem::path LevelCacheIndex::clicksPath(
    std::string const& fullKey,
    std::string const& method
) {
    return levelDir(fullKey) / clicksFileName(method);
}

std::filesystem::path LevelCacheIndex::routePath(std::string const& fullKey) {
    return levelDir(fullKey) / "route.json";
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
    if (!ensureLevelDir(meta.fullKey, err)) return Err(err);

    Meta out = meta;
    if (out.schema <= 0) out.schema = kSchema;
    if (out.updatedAt.empty()) out.updatedAt = nowUtcIso();
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
    return LevelCacheAtomicIo::writeJson(metaPath(out.fullKey), root);
}

geode::Result<LevelCacheIndex::Meta> LevelCacheIndex::readMeta(
    std::string const& fullKey
) {
    auto res = LevelCacheAtomicIo::readJson(metaPath(fullKey));
    if (res.isErr()) return Err(res.unwrapErr());
    auto root = res.unwrap();

    Meta m;
    m.schema = static_cast<int>(root["schema"].asInt().unwrapOr(0));
    m.fullKey = root.contains("fullKey")
        ? root["fullKey"].asString().unwrapOr(fullKey)
        : fullKey;
    m.startXq = static_cast<int>(root["startXq"].asInt().unwrapOr(0));
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

    // Future schema: keep folder, mark stale for Route (not yet).
    if (m.schema > kSchema) m.stale = true;
    return Ok(std::move(m));
}

geode::Result<> LevelCacheIndex::copyFileIfPresent(
    std::filesystem::path const& from,
    std::filesystem::path const& to
) {
    if (!LevelCacheAtomicIo::existsRegular(from)) return Ok();
    if (LevelCacheAtomicIo::existsRegular(to)) return Ok(); // already in cache
    auto parent = to.parent_path();
    if (!parent.empty()) {
        auto mk = file::createDirectoryAll(parent);
        if (mk.isErr()) return Err(mk.unwrapErr());
    }
    // Prefer JSON atomic path (our stores are JSON).
    auto json = LevelCacheAtomicIo::readJson(from);
    if (json.isOk()) {
        return LevelCacheAtomicIo::writeJson(to, json.unwrap());
    }
    // Fallback: binary copy via tmp+rename.
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

    // Already have any clicks-* or meta → treat as migrated (still copy missing TW/profile).
    bool const hasClicks =
        LevelCacheAtomicIo::existsRegular(clicksPath(fullKey, "replay"))
        || LevelCacheAtomicIo::existsRegular(clicksPath(fullKey, "brute"))
        || LevelCacheAtomicIo::existsRegular(clicksPath(fullKey, "self"));
    bool const hasMeta = LevelCacheAtomicIo::existsRegular(metaPath(fullKey));

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
            // Keep going — TW/profile may still copy.
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
                    clicksPath(fullKey, method),
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
        if (auto* mod = Mod::get()) {
            meta.modVersion = mod->getVersion().toVString();
        }
        meta.gdVersion = "2.2081";
        // Prefer tps from a clicks-* file if present (no loadMerged — avoids recursion).
        for (char const* method : {"replay", "brute", "self"}) {
            auto const cp = clicksPath(fullKey, method);
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
    expect(
        std::string(clicksFileName("brute")) == "clicks-brute.json",
        "fname-brute"
    );

    auto const key = std::string("id_selfcheck_rev_deadbeef");
    expect(
        levelDir(key).filename() == key
            || levelDir(key).string().find(key) != std::string::npos,
        "level-dir"
    );
    expect(
        legacyBakePath(key).filename() == key + ".json",
        "legacy-bake-name"
    );

    // Path-only when Mod unavailable is fine; disk migrate tested in ClickBakeStore.
    expect(kSchema == 1, "schema");

    if (fails == 0) {
        report = "LevelCacheIndex selfCheck OK";
        return true;
    }
    report = fmt::format("LevelCacheIndex selfCheck {} fails: {}", fails, report);
    return false;
}
