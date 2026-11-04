#include "CloudConfigSeed.hpp"
#include "CloudConfigIndex.hpp"
#include "CloudConfigSha256.hpp"
#include "CloudConfigStore.hpp"

#include <Geode/Geode.hpp>

#include <fstream>

using namespace geode::prelude;

// Packaged resources may omit nested JSON; keep an identical offline fallback compiled in.
// Pack bytes must match resources/cloud-seed/packs/safe-defaults.json (LF, 379 bytes).
class CloudConfigSeedEmbedded {
public:
    static char const* indexJson() {
        return R"({
  "schema": 1,
  "updatedAt": "2026-11-04T00:00:00Z",
  "packs": [
    {
      "id": "safe-defaults",
      "name": "Safe Defaults",
      "author": "Audesign",
      "description": "Safe Mode on, Live assist, short tick windows — a solid first install. Keybinds and Dev Lab settings are left unchanged when applied from cloud.",
      "tags": ["safe", "beginner", "live"],
      "minModVersion": "1.4.0",
      "bundleVersion": 2,
      "sha256": "d07a6bc9765374bf8a87aa5b69455e2333d0eb542905179444acfa0633bae3b0",
      "path": "packs/safe-defaults.json",
      "size": 379
    }
  ]
})";
    }

    static char const* safeDefaultsPackJson() {
        return
            "{\n"
            "  \"version\": 2,\n"
            "  \"settings\": {\n"
            "    \"enabled\": true,\n"
            "    \"safe-mode\": true,\n"
            "    \"assist-mode\": \"Live\",\n"
            "    \"window-unit\": \"Ticks\",\n"
            "    \"ticks-all\": 4,\n"
            "    \"ticks-wave\": 4,\n"
            "    \"look-ahead-cap\": 0,\n"
            "    \"death-slack\": 2,\n"
            "    \"assist-orbs\": true,\n"
            "    \"assist-press\": true,\n"
            "    \"assist-release\": true,\n"
            "    \"auto-tune\": false,\n"
            "    \"self-bake\": false,\n"
            "    \"trigger-bake\": false\n"
            "  }\n"
            "}";
    }

private:
    CloudConfigSeedEmbedded() = default;
};

std::filesystem::path CloudConfigSeed::rootDir() {
    auto* mod = Mod::get();
    if (!mod) return {};
    return mod->getResourcesDir() / "cloud-seed";
}

std::filesystem::path CloudConfigSeed::indexPath() {
    auto const root = rootDir();
    if (root.empty()) return {};
    return root / "index.json";
}

std::filesystem::path CloudConfigSeed::packPath(std::string const& id) {
    if (!CloudConfigStore::isValidPackId(id)) return {};
    auto const root = rootDir();
    if (root.empty()) return {};
    return root / "packs" / (id + ".json");
}

bool CloudConfigSeed::readIndex(std::string& out, std::string& err) {
    out.clear();
    err.clear();
    auto const path = indexPath();
    if (!path.empty()
        && CloudConfigStore::readTextFile(
            path, out, CloudConfigStore::kMaxIndexBytes, err
        )) {
        return true;
    }
    // Flat resource name fallback (some Geode packages flatten resources/).
    if (auto* mod = Mod::get()) {
        auto const flat = mod->getResourcesDir() / "cloud-seed-index.json";
        std::string flatErr;
        if (CloudConfigStore::readTextFile(
                flat, out, CloudConfigStore::kMaxIndexBytes, flatErr
            )) {
            err.clear();
            return true;
        }
    }
    out = CloudConfigSeedEmbedded::indexJson();
    err.clear();
    return !out.empty();
}

bool CloudConfigSeed::readPack(std::string const& id, std::string& out, std::string& err) {
    out.clear();
    err.clear();
    auto const path = packPath(id);
    if (!path.empty()
        && CloudConfigStore::readTextFile(
            path, out, CloudConfigStore::kMaxPackBytes, err
        )) {
        return true;
    }
    if (auto* mod = Mod::get()) {
        auto const flat = mod->getResourcesDir()
            / (std::string("cloud-seed-") + id + ".json");
        std::string flatErr;
        if (CloudConfigStore::readTextFile(
                flat, out, CloudConfigStore::kMaxPackBytes, flatErr
            )) {
            err.clear();
            return true;
        }
    }
    if (id == "safe-defaults") {
        out = CloudConfigSeedEmbedded::safeDefaultsPackJson();
        err.clear();
        return !out.empty();
    }
    err = "seed pack not found";
    return false;
}

bool CloudConfigSeed::available() {
    std::string body;
    std::string err;
    return readIndex(body, err);
}

bool CloudConfigSeed::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    expect(packPath("../x").empty(), "seed-pack-reject");
    expect(packPath("").empty(), "seed-pack-empty");

    // Embedded pack must match the hashed size/sha recorded in the index.
    {
        std::string const pack = CloudConfigSeedEmbedded::safeDefaultsPackJson();
        expect(pack.size() == 379, "embed-pack-size");
        expect(
            CloudConfigSha256::hashHex(pack)
                == "d07a6bc9765374bf8a87aa5b69455e2333d0eb542905179444acfa0633bae3b0",
            "embed-pack-sha"
        );
    }

    std::string indexBody;
    std::string err;
    expect(readIndex(indexBody, err), "seed-read-index");
    CloudConfigIndex::Catalog cat{};
    expect(CloudConfigIndex::parse(indexBody, cat, err), "seed-parse-index");
    expect(!cat.packs.empty(), "seed-has-packs");

    for (auto const& p : cat.packs) {
        std::string packBody;
        expect(readPack(p.id, packBody, err), "seed-read-pack");
        expect(packBody.size() == p.size, "seed-size-match");
        expect(CloudConfigSha256::hashHex(packBody) == p.sha256, "seed-sha-match");
        auto parsed = matjson::parse(packBody);
        expect(parsed.isOk() && parsed.unwrap().isObject(), "seed-pack-json");
        if (parsed.isOk() && parsed.unwrap().isObject()) {
            auto const root = parsed.unwrap();
            expect(root.contains("version") || root.contains("settings"), "seed-pack-shape");
        }
    }

    expect(available(), "seed-available");

    if (fails == 0) {
        report = "CloudConfigSeed selfCheck OK";
        return true;
    }
    report = std::string("CloudConfigSeed selfCheck fails: ") + report;
    return false;
}
