#include "cloud_config_store.hpp"
#include "../core/platform/save_drop_paths.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>
#include <cctype>
#include <chrono>
#include <fstream>
#include <system_error>

CloudConfigStore& CloudConfigStore::get() {
    static CloudConfigStore inst;
    return inst;
}

std::filesystem::path CloudConfigStore::rootDir() const {
    return SaveDropPaths::get().cloudConfigsDir();
}

std::filesystem::path CloudConfigStore::packsDir() const {
    return this->rootDir() / "packs";
}

std::filesystem::path CloudConfigStore::outboxDir() const {
    return this->rootDir() / "outbox";
}

std::filesystem::path CloudConfigStore::indexCachePath() const {
    return this->rootDir() / "index-cache.json";
}

std::filesystem::path CloudConfigStore::metaPath() const {
    return this->rootDir() / "meta.json";
}

std::filesystem::path CloudConfigStore::activePath() const {
    return this->rootDir() / "active.json";
}

std::filesystem::path CloudConfigStore::packPath(std::string const& id) const {
    if (!isValidPackId(id)) {
        return {};
    }
    return this->packsDir() / (id + ".json");
}

bool CloudConfigStore::ensureDirs(std::string& err) const {
    err.clear();
    auto const root = this->rootDir();
    auto const packs = this->packsDir();
    auto const outbox = this->outboxDir();

    auto rootRes = geode::utils::file::createDirectoryAll(root);
    if (rootRes.isErr()) {
        err = fmt::format("cannot create cloud-configs dir {}: {}", root.string(),
                          rootRes.unwrapErr());
        return false;
    }
    auto packsRes = geode::utils::file::createDirectoryAll(packs);
    if (packsRes.isErr()) {
        err = fmt::format("cannot create packs dir {}: {}", packs.string(), packsRes.unwrapErr());
        return false;
    }
    auto outboxRes = geode::utils::file::createDirectoryAll(outbox);
    if (outboxRes.isErr()) {
        err =
            fmt::format("cannot create outbox dir {}: {}", outbox.string(), outboxRes.unwrapErr());
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec) || ec) {
        err = fmt::format("cloud-configs missing or not a directory: {}", root.string());
        return false;
    }
    if (!std::filesystem::is_directory(packs, ec) || ec) {
        err = fmt::format("packs missing or not a directory: {}", packs.string());
        return false;
    }
    if (!std::filesystem::is_directory(outbox, ec) || ec) {
        err = fmt::format("outbox missing or not a directory: {}", outbox.string());
        return false;
    }
    return true;
}

bool CloudConfigStore::isValidPackId(std::string const& id) {
    if (id.empty() || id.size() > kMaxIdLen)
        return false;
    if (id.front() == '-' || id.back() == '-')
        return false;
    for (char const character : id) {
        bool const ok = (character >= 'a' && character <= 'z') ||
                        (character >= '0' && character <= '9') || character == '-';
        if (!ok)
            return false;
    }
    return true;
}

bool CloudConfigStore::readTextFile(std::filesystem::path const& path, std::string& out,
                                    size_t maxBytes, std::string& err) {
    out.clear();
    err.clear();
    if (path.empty()) {
        err = "read path empty";
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        err = fmt::format("file missing: {}", path.string());
        return false;
    }
    auto const sizeValue = std::filesystem::file_size(path, ec);
    if (ec) {
        err = fmt::format("file size failed: {}", path.string());
        return false;
    }
    if (sizeValue == 0) {
        err = fmt::format("file empty: {}", path.string());
        return false;
    }
    if (sizeValue > maxBytes) {
        err =
            fmt::format("file too large ({} > {}): {}", static_cast<unsigned long long>(sizeValue),
                        static_cast<unsigned long long>(maxBytes), path.string());
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = fmt::format("open failed: {}", path.string());
        return false;
    }
    out.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (out.empty()) {
        err = fmt::format("read empty: {}", path.string());
        return false;
    }
    if (out.size() > maxBytes) {
        out.clear();
        err = fmt::format("read oversize: {}", path.string());
        return false;
    }
    return true;
}

bool CloudConfigStore::writeTextFile(std::filesystem::path const& path, std::string const& body,
                                     std::string& err) {
    err.clear();
    if (path.empty()) {
        err = "write path empty";
        return false;
    }
    if (body.empty()) {
        err = "write body empty";
        return false;
    }
    auto const parent = path.parent_path();
    if (!parent.empty()) {
        auto mk = geode::utils::file::createDirectoryAll(parent);
        if (mk.isErr()) {
            err = fmt::format("cannot create parent {}: {}", parent.string(), mk.unwrapErr());
            return false;
        }
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        err = fmt::format("open for write failed: {}", path.string());
        return false;
    }
    output << body;
    if (!output) {
        err = fmt::format("write failed: {}", path.string());
        return false;
    }
    return true;
}

bool CloudConfigStore::readIndexCache(std::string& out, std::string& err) const {
    return readTextFile(this->indexCachePath(), out, kMaxIndexBytes, err);
}

bool CloudConfigStore::clearIndexCache(std::string& err) const {
    err.clear();
    auto const path = this->indexCachePath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return true;
    }
    if (!std::filesystem::remove(path, ec) || ec) {
        err = ec ? ec.message() : "remove index-cache failed";
        return false;
    }
    return true;
}

bool CloudConfigStore::writeIndexCache(std::string const& body, std::string& err) const {
    if (body.size() > kMaxIndexBytes) {
        err = "index body too large";
        return false;
    }
    return writeTextFile(this->indexCachePath(), body, err);
}

bool CloudConfigStore::readPack(std::string const& id, std::string& out, std::string& err) const {
    auto const path = this->packPath(id);
    if (path.empty()) {
        err = "invalid pack id";
        return false;
    }
    return readTextFile(path, out, kMaxPackBytes, err);
}

bool CloudConfigStore::writePack(std::string const& id, std::string const& body,
                                 std::string& err) const {
    if (!isValidPackId(id)) {
        err = "invalid pack id";
        return false;
    }
    if (body.size() > kMaxPackBytes) {
        err = "pack body too large";
        return false;
    }
    std::string dirErr;
    if (!this->ensureDirs(dirErr)) {
        err = dirErr.empty() ? "ensureDirs failed" : dirErr;
        return false;
    }
    return writeTextFile(this->packPath(id), body, err);
}

bool CloudConfigStore::removePack(std::string const& id, std::string& err) const {
    err.clear();
    auto const path = this->packPath(id);
    if (path.empty()) {
        err = "invalid pack id";
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return true; // already gone
    }
    if (!std::filesystem::remove(path, ec) || ec) {
        err = fmt::format("remove failed: {}", path.string());
        return false;
    }
    return true;
}

bool CloudConfigStore::hasPack(std::string const& id) const {
    auto const path = this->packPath(id);
    if (path.empty())
        return false;
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec) && !ec;
}

std::vector<std::string> CloudConfigStore::listCachedPackIds() const {
    std::vector<std::string> out;
    auto const directory = this->packsDir();
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec) || ec) {
        return out;
    }
    for (auto const& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (ec)
            break;
        if (!entry.is_regular_file(ec) || ec)
            continue;
        auto const path = entry.path();
        if (path.extension() != ".json")
            continue;
        auto const stem = path.stem().string();
        if (!isValidPackId(stem))
            continue;
        auto const size = entry.file_size(ec);
        if (ec || size == 0 || size > kMaxPackBytes)
            continue;
        out.push_back(stem);
    }
    return out;
}

bool CloudConfigStore::readActive(ActiveInfo& out, std::string& err) const {
    out = {};
    std::string body;
    if (!readTextFile(this->activePath(), body, 8u * 1024u, err)) {
        return false;
    }
    auto parsed = matjson::parse(body);
    if (parsed.isErr() || !parsed.unwrap().isObject()) {
        err = "active.json parse failed";
        return false;
    }
    auto const root = parsed.unwrap();
    auto takeStr = [&](char const* key, std::string& dest) {
        if (!root.contains(key))
            return;
        auto const value = root[key].asString();
        if (value.isOk())
            dest = std::string(value.unwrap());
    };
    takeStr("id", out.id);
    takeStr("sha256", out.sha256);
    takeStr("name", out.name);
    takeStr("appliedAt", out.appliedAt);
    if (out.id.empty()) {
        err = "active.json missing id";
        out = {};
        return false;
    }
    return true;
}

bool CloudConfigStore::writeActive(ActiveInfo const& info, std::string& err) const {
    if (info.id.empty()) {
        err = "active id empty";
        return false;
    }
    // Local marker "local-file" is allowed; catalog packs must pass isValidPackId.
    if (info.id != "local-file" && !isValidPackId(info.id)) {
        err = "active id invalid";
        return false;
    }
    std::string dirErr;
    if (!this->ensureDirs(dirErr)) {
        err = dirErr.empty() ? "ensureDirs failed" : dirErr;
        return false;
    }
    matjson::Value root = matjson::Value::object();
    root["id"] = info.id;
    root["sha256"] = info.sha256;
    root["name"] = info.name;
    root["appliedAt"] = info.appliedAt;
    return writeTextFile(this->activePath(), root.dump(), err);
}

bool CloudConfigStore::clearActive(std::string& err) const {
    err.clear();
    std::error_code ec;
    auto const path = this->activePath();
    if (!std::filesystem::exists(path, ec) || ec) {
        return true;
    }
    if (!std::filesystem::remove(path, ec) || ec) {
        err = fmt::format("clear active failed: {}", path.string());
        return false;
    }
    return true;
}

bool CloudConfigStore::readMeta(MetaInfo& out, std::string& err) const {
    out = {};
    std::string body;
    if (!readTextFile(this->metaPath(), body, 8u * 1024u, err)) {
        return false;
    }
    auto parsed = matjson::parse(body);
    if (parsed.isErr() || !parsed.unwrap().isObject()) {
        err = "meta.json parse failed";
        return false;
    }
    auto const root = parsed.unwrap();
    if (root.contains("lastRefreshOk")) {
        auto const secondValue = root["lastRefreshOk"].asBool();
        if (secondValue.isOk())
            out.lastRefreshOk = secondValue.unwrap();
    }
    auto takeStr = [&](char const* key, std::string& dest) {
        if (!root.contains(key))
            return;
        auto const value = root[key].asString();
        if (value.isOk())
            dest = std::string(value.unwrap());
    };
    takeStr("lastError", out.lastError);
    takeStr("sourceUrl", out.sourceUrl);
    takeStr("updatedAt", out.updatedAt);
    return true;
}

bool CloudConfigStore::writeMeta(MetaInfo const& info, std::string& err) const {
    std::string dirErr;
    if (!this->ensureDirs(dirErr)) {
        err = dirErr.empty() ? "ensureDirs failed" : dirErr;
        return false;
    }
    matjson::Value root = matjson::Value::object();
    root["lastRefreshOk"] = info.lastRefreshOk;
    root["lastError"] = info.lastError;
    root["sourceUrl"] = info.sourceUrl;
    root["updatedAt"] = info.updatedAt;
    return writeTextFile(this->metaPath(), root.dump(), err);
}

std::string CloudConfigStore::sanitizeStem(std::string const& stem) {
    std::string out;
    out.reserve(stem.size());
    for (char const character : stem) {
        unsigned char const unsignedCharacter = static_cast<unsigned char>(character);
        if (std::isalnum(unsignedCharacter)) {
            out.push_back(static_cast<char>(std::tolower(unsignedCharacter)));
        } else if (character == '-' || character == '_') {
            out.push_back('-');
        }
        if (out.size() >= kMaxIdLen)
            break;
    }
    while (!out.empty() && out.front() == '-')
        out.erase(out.begin());
    while (!out.empty() && out.back() == '-')
        out.pop_back();
    if (out.empty())
        out = "share";
    return out;
}

bool CloudConfigStore::writeOutbox(std::string const& stem, std::string const& body,
                                   std::filesystem::path& outPath, std::string& err) const {
    outPath.clear();
    if (body.empty()) {
        err = "outbox body empty";
        return false;
    }
    if (body.size() > kMaxPackBytes * 2u) {
        err = "outbox body too large";
        return false;
    }
    std::string dirErr;
    if (!this->ensureDirs(dirErr)) {
        err = dirErr.empty() ? "ensureDirs failed" : dirErr;
        return false;
    }
    auto const clean = sanitizeStem(stem);
    auto const stamp = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    auto const name = fmt::format("{}-{}.json", clean, stamp);
    outPath = this->outboxDir() / name;
    return writeTextFile(outPath, body, err);
}

bool CloudConfigStore::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    expect(isValidPackId("safe-defaults"), "id-ok");
    expect(isValidPackId("a"), "id-one");
    expect(isValidPackId("x1-y2"), "id-alnum-dash");
    expect(!isValidPackId(""), "id-empty");
    expect(!isValidPackId("-bad"), "id-lead-dash");
    expect(!isValidPackId("bad-"), "id-trail-dash");
    expect(!isValidPackId("Bad"), "id-upper");
    expect(!isValidPackId("has space"), "id-space");
    expect(!isValidPackId("has/slash"), "id-slash");
    expect(!isValidPackId(std::string(kMaxIdLen + 1, 'a')), "id-too-long");
    expect(isValidPackId(std::string(kMaxIdLen, 'a')), "id-max");

    expect(sanitizeStem("") == "share", "stem-empty");
    expect(sanitizeStem("My Pack!") == "my-pack", "stem-sanitize");
    expect(sanitizeStem("---") == "share", "stem-dashes");

    auto& store = CloudConfigStore::get();
    auto const root = store.rootDir();
    expect(root.filename() == "cloud-configs", "root-name");
    expect(store.packsDir().filename() == "packs", "packs-name");
    expect(store.packsDir().parent_path() == root, "packs-under-root");
    expect(store.outboxDir().filename() == "outbox", "outbox-name");
    expect(store.outboxDir().parent_path() == root, "outbox-under-root");
    expect(store.indexCachePath().filename() == "index-cache.json", "index-name");
    expect(store.metaPath().filename() == "meta.json", "meta-name");
    expect(store.activePath().filename() == "active.json", "active-name");
    expect(store.packPath("safe-defaults").filename() == "safe-defaults.json", "pack-file");
    expect(store.packPath("../x").empty(), "pack-reject-traversal");
    expect(store.packPath("").empty(), "pack-reject-empty");

    expect(SaveDropPaths::get().cloudConfigsDir() == root, "save-drop-cloud-dir");

    if (geode::prelude::Mod::get()) {
        std::string err;
        expect(store.ensureDirs(err), "ensure-dirs");
        if (!err.empty() && fails > 0) {
            report += err + "; ";
        }

        // Round-trip a tiny pack + active + meta, then clean up.
        std::string const packId = "selfcheck-pack";
        std::string const packBody = R"({"version":2,"settings":{"ticks-all":4}})";
        expect(store.writePack(packId, packBody, err), "write-pack");
        expect(store.hasPack(packId), "has-pack");
        std::string readBack;
        expect(store.readPack(packId, readBack, err), "read-pack");
        expect(readBack == packBody, "pack-body");

        ActiveInfo active{};
        active.id = packId;
        active.sha256 = "abc";
        active.name = "Selfcheck";
        active.appliedAt = "test";
        expect(store.writeActive(active, err), "write-active");
        ActiveInfo activeRead{};
        expect(store.readActive(activeRead, err), "read-active");
        expect(activeRead.id == packId, "active-id");
        expect(activeRead.name == "Selfcheck", "active-name");

        ActiveInfo localMark{};
        localMark.id = "local-file";
        localMark.name = "Local";
        expect(store.writeActive(localMark, err), "write-active-local");
        expect(store.readActive(activeRead, err) && activeRead.id == "local-file",
               "read-active-local");

        MetaInfo meta{};
        meta.lastRefreshOk = true;
        meta.sourceUrl = "https://example.invalid/index.json";
        meta.updatedAt = "test";
        expect(store.writeMeta(meta, err), "write-meta");
        MetaInfo metaRead{};
        expect(store.readMeta(metaRead, err), "read-meta");
        expect(metaRead.lastRefreshOk, "meta-ok");
        expect(metaRead.sourceUrl == meta.sourceUrl, "meta-url");

        std::string const indexBody = R"({"schema":1,"packs":[]})";
        expect(store.writeIndexCache(indexBody, err), "write-index");
        std::string indexRead;
        expect(store.readIndexCache(indexRead, err), "read-index");
        expect(indexRead == indexBody, "index-body");

        std::filesystem::path outPath;
        expect(store.writeOutbox("SelfCheck!", R"({"ok":true})", outPath, err), "write-outbox");
        expect(!outPath.empty(), "outbox-path");
        expect(outPath.parent_path() == store.outboxDir(), "outbox-under");

        expect(store.removePack(packId, err), "remove-pack");
        expect(!store.hasPack(packId), "pack-gone");
        expect(store.clearActive(err), "clear-active");

        // Corrupt / missing active must not crash — read fails cleanly.
        ActiveInfo missing{};
        std::string missErr;
        expect(!store.readActive(missing, missErr), "active-missing-fail");
        expect(!missErr.empty(), "active-missing-err");
    }

    if (fails == 0) {
        report = "CloudConfigStore selfCheck OK";
        return true;
    }
    report = std::string("CloudConfigStore selfCheck fails: ") + report;
    return false;
}
