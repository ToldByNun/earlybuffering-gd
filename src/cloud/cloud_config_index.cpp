#include "cloud_config_index.hpp"
#include "cloud_config_store.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/VersionInfo.hpp>
#include <cctype>

bool CloudConfigIndex::isLowerHexSha256(std::string const& hex) {
    if (hex.size() != 64)
        return false;
    for (char const character : hex) {
        bool const ok =
            (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
        if (!ok)
            return false;
    }
    return true;
}

bool CloudConfigIndex::isSafeRelativePackPath(std::string const& path) {
    if (path.empty() || path.front() == '/' || path.find("://") != std::string::npos) {
        return false;
    }
    if (path.find("..") != std::string::npos)
        return false;
    if (path.rfind("packs/", 0) != 0)
        return false;
    if (path.size() < 12)
        return false; // packs/x.json min
    if (path.substr(path.size() - 5) != ".json")
        return false;
    for (char const character : path) {
        unsigned char const unsignedCharacter = static_cast<unsigned char>(character);
        bool const ok = std::isalnum(unsignedCharacter) || character == '-' || character == '_' ||
                        character == '.' || character == '/';
        if (!ok)
            return false;
    }
    return true;
}

bool CloudConfigIndex::validatePack(PackEntry const& p, std::string& err) {
    if (!CloudConfigStore::isValidPackId(p.id)) {
        err = "pack id invalid";
        return false;
    }
    if (p.name.empty() || p.name.size() > 128) {
        err = "pack name invalid";
        return false;
    }
    if (p.author.size() > 64) {
        err = "pack author too long";
        return false;
    }
    if (p.description.size() > 1024) {
        err = "pack description too long";
        return false;
    }
    if (p.tags.size() > 16) {
        err = "pack too many tags";
        return false;
    }
    for (auto const& t : p.tags) {
        if (t.empty() || t.size() > 32) {
            err = "pack tag invalid";
            return false;
        }
    }
    if (p.bundleVersion != 1 && p.bundleVersion != 2) {
        err = "pack bundleVersion must be 1 or 2";
        return false;
    }
    if (!isLowerHexSha256(p.sha256)) {
        err = "pack sha256 invalid";
        return false;
    }
    if (!isSafeRelativePackPath(p.path)) {
        err = "pack path invalid";
        return false;
    }
    // path stem should match id: packs/<id>.json
    auto const expectPath = std::string("packs/") + p.id + ".json";
    if (p.path != expectPath) {
        err = "pack path must be packs/<id>.json";
        return false;
    }
    if (p.size == 0 || p.size > CloudConfigStore::kMaxPackBytes) {
        err = "pack size out of range";
        return false;
    }
    if (!p.minModVersion.empty()) {
        auto const parsed = geode::prelude::VersionInfo::parse(p.minModVersion);
        if (parsed.isErr()) {
            err = "pack minModVersion invalid";
            return false;
        }
    }
    return true;
}

bool CloudConfigIndex::parse(std::string const& json, Catalog& out, std::string& err) {
    out = {};
    err.clear();
    if (json.empty()) {
        err = "index empty";
        return false;
    }
    if (json.size() > CloudConfigStore::kMaxIndexBytes) {
        err = "index too large";
        return false;
    }
    auto parsed = matjson::parse(json);
    if (parsed.isErr() || !parsed.unwrap().isObject()) {
        err = "index json";
        return false;
    }
    auto const root = parsed.unwrap();
    if (!root.contains("schema")) {
        err = "index missing schema";
        return false;
    }
    auto const schema = root["schema"].asInt();
    if (schema.isErr() || static_cast<int>(schema.unwrap()) != kSchema) {
        err = "index schema unsupported";
        return false;
    }
    out.schema = kSchema;
    if (root.contains("updatedAt")) {
        auto const u = root["updatedAt"].asString();
        if (u.isOk())
            out.updatedAt = std::string(u.unwrap());
    }
    if (!root.contains("packs") || !root["packs"].isArray()) {
        err = "index missing packs array";
        out = {};
        return false;
    }
    auto const packs = root["packs"].asArray().unwrap();
    if (packs.size() > 256) {
        err = "index too many packs";
        out = {};
        return false;
    }

    for (auto const& item : packs) {
        if (!item.isObject()) {
            err = "pack entry not object";
            out = {};
            return false;
        }
        PackEntry p;
        auto takeStr = [&](char const* key, std::string& dest, bool required) -> bool {
            if (!item.contains(key)) {
                if (required) {
                    err = std::string("pack missing ") + key;
                    return false;
                }
                return true;
            }
            auto const value = item[key].asString();
            if (value.isErr()) {
                err = std::string("pack bad ") + key;
                return false;
            }
            dest = std::string(value.unwrap());
            return true;
        };
        if (!takeStr("id", p.id, true)) {
            out = {};
            return false;
        }
        if (!takeStr("name", p.name, true)) {
            out = {};
            return false;
        }
        if (!takeStr("author", p.author, false)) {
            out = {};
            return false;
        }
        if (!takeStr("description", p.description, false)) {
            out = {};
            return false;
        }
        if (!takeStr("minModVersion", p.minModVersion, false)) {
            out = {};
            return false;
        }
        if (!takeStr("sha256", p.sha256, true)) {
            out = {};
            return false;
        }
        if (!takeStr("path", p.path, true)) {
            out = {};
            return false;
        }

        if (item.contains("bundleVersion")) {
            auto const bv = item["bundleVersion"].asInt();
            if (bv.isErr()) {
                err = "pack bad bundleVersion";
                out = {};
                return false;
            }
            p.bundleVersion = static_cast<int>(bv.unwrap());
        }
        if (item.contains("size")) {
            auto const sizeValue = item["size"].asInt();
            if (sizeValue.isErr() || sizeValue.unwrap() < 0) {
                err = "pack bad size";
                out = {};
                return false;
            }
            p.size = static_cast<size_t>(sizeValue.unwrap());
        }
        if (item.contains("tags") && item["tags"].isArray()) {
            auto const tags = item["tags"].asArray().unwrap();
            for (auto const& t : tags) {
                auto const ts = t.asString();
                if (ts.isErr()) {
                    err = "pack bad tag";
                    out = {};
                    return false;
                }
                p.tags.emplace_back(ts.unwrap());
            }
        }

        // Normalize sha256 to lowercase for compare.
        for (char& c : p.sha256) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        if (!validatePack(p, err)) {
            out = {};
            return false;
        }
        // Duplicate id?
        if (findById(out, p.id)) {
            err = "duplicate pack id";
            out = {};
            return false;
        }
        out.packs.push_back(std::move(p));
    }
    return true;
}

bool CloudConfigIndex::meetsMinModVersion(PackEntry const& entry,
                                          std::string const& liveModVersion) {
    if (entry.minModVersion.empty())
        return true;
    auto const need = geode::prelude::VersionInfo::parse(entry.minModVersion);
    if (need.isErr())
        return false;
    auto const live = geode::prelude::VersionInfo::parse(liveModVersion);
    if (!(live.isErr())) {
        return live.unwrap() >= need.unwrap();
    }
    return false;
}

CloudConfigIndex::PackEntry const* CloudConfigIndex::findById(Catalog const& catalog,
                                                              std::string const& id) {
    for (auto const& p : catalog.packs) {
        if (p.id == id)
            return &p;
    }
    return nullptr;
}

bool CloudConfigIndex::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    expect(isLowerHexSha256(std::string(64, 'a')), "sha-ok");
    expect(!isLowerHexSha256(std::string(64, 'A')), "sha-upper-reject-raw");
    expect(!isLowerHexSha256("abc"), "sha-short");
    expect(isSafeRelativePackPath("packs/safe-defaults.json"), "path-ok");
    expect(!isSafeRelativePackPath("packs/../x.json"), "path-dotdot");
    expect(!isSafeRelativePackPath("other/x.json"), "path-other");

    Catalog category{};
    std::string err;
    std::string const good = R"({
  "schema": 1,
  "updatedAt": "2026-11-04T00:00:00Z",
  "packs": [
    {
      "id": "safe-defaults",
      "name": "Safe Defaults",
      "author": "Audesign",
      "description": "Safe Mode on, Live, short windows.",
      "tags": ["safe", "beginner"],
      "minModVersion": "1.4.0",
      "bundleVersion": 2,
      "sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      "path": "packs/safe-defaults.json",
      "size": 128
    }
  ]
})";
    expect(parse(good, category, err), "parse-good");
    expect(category.schema == 1, "schema");
    expect(category.packs.size() == 1, "pack-count");
    expect(findById(category, "safe-defaults") != nullptr, "find");
    expect(meetsMinModVersion(category.packs[0], "v1.4.0"), "min-eq");
    expect(meetsMinModVersion(category.packs[0], "v1.4.1"), "min-newer");
    expect(!meetsMinModVersion(category.packs[0], "v1.3.9"), "min-older");

    Catalog bad{};
    expect(!parse(R"({"schema":2,"packs":[]})", bad, err), "reject-schema");
    expect(!parse(R"({"schema":1,"packs":[{"id":"Bad"}]})", bad, err), "reject-bad-id");
    expect(
        !parse(
            R"({"schema":1,"packs":[{"id":"x","name":"X","sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","path":"packs/y.json","size":1}]})",
            bad, err),
        "reject-path-id-mismatch");

    if (fails == 0) {
        report = "CloudConfigIndex selfCheck OK";
        return true;
    }
    report = std::string("CloudConfigIndex selfCheck fails: ") + report;
    return false;
}
