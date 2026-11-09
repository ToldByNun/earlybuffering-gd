#include "cloud_config_seed.hpp"
#include "../cloud_config_store.hpp"
#include "../transport/cloud_config_sha256.hpp"
#include "cloud_config_index.hpp"
#include <Geode/Geode.hpp>
#include <fstream>

// Packaged resources may omit nested JSON; keep an identical offline fallback compiled in.
// Pack bytes must match resources/cloud-seed/packs/safe-defaults.json (LF, 379 bytes).

std::filesystem::path EbCloudConfigSeed::rootDir() {
    auto* mod = geode::prelude::Mod::get();
    if (!(!mod)) {
        return mod->getResourcesDir() / "cloud-seed";
    }
    return {};
}

std::filesystem::path EbCloudConfigSeed::indexPath() {
    auto const root = rootDir();
    if (!(root.empty())) {
        return root / "index.json";
    }
    return {};
}

std::filesystem::path EbCloudConfigSeed::packPath(std::string const& id) {
    if (!EbCloudConfigStore::isValidPackId(id))
        return {};
    auto const root = rootDir();
    if (!(root.empty())) {
        return root / "packs" / (id + ".json");
    }
    return {};
}

bool EbCloudConfigSeed::readIndex(std::string& out, std::string& err) {
    out.clear();
    err.clear();
    auto const path = indexPath();
    if (!path.empty() &&
        EbCloudConfigStore::readTextFile(path, out, EbCloudConfigStore::kMaxIndexBytes, err)) {
        return true;
    }
    // Flat resource name fallback (some Geode packages flatten resources/).
    if (auto* mod = geode::prelude::Mod::get()) {
        auto const flat = mod->getResourcesDir() / "cloud-seed-index.json";
        std::string flatErr;
        if (EbCloudConfigStore::readTextFile(flat, out, EbCloudConfigStore::kMaxIndexBytes, flatErr)) {
            err.clear();
            return true;
        }
    }
    out = EbCloudConfigSeedEmbedded::indexJson();
    err.clear();
    return !out.empty();
}

bool EbCloudConfigSeed::readPack(std::string const& id, std::string& out, std::string& err) {
    out.clear();
    err.clear();
    auto const path = packPath(id);
    if (!path.empty() &&
        EbCloudConfigStore::readTextFile(path, out, EbCloudConfigStore::kMaxPackBytes, err)) {
        return true;
    }
    if (auto* mod = geode::prelude::Mod::get()) {
        auto const flat = mod->getResourcesDir() / (std::string("cloud-seed-") + id + ".json");
        std::string flatErr;
        if (EbCloudConfigStore::readTextFile(flat, out, EbCloudConfigStore::kMaxPackBytes, flatErr)) {
            err.clear();
            return true;
        }
    }
    if (id == "safe-defaults") {
        out = EbCloudConfigSeedEmbedded::safeDefaultsPackJson();
        err.clear();
        return !out.empty();
    }
    err = "seed pack not found";
    return false;
}

bool EbCloudConfigSeed::available() {
    std::string body;
    std::string err;
    return readIndex(body, err);
}

bool EbCloudConfigSeed::selfCheck(std::string& report) {
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
        std::string const pack = EbCloudConfigSeedEmbedded::safeDefaultsPackJson();
        expect(pack.size() == 379, "embed-pack-size");
        expect(EbCloudConfigSha256::hashHex(pack) ==
                   "d07a6bc9765374bf8a87aa5b69455e2333d0eb542905179444acfa0633bae3b0",
               "embed-pack-sha");
    }

    std::string indexBody;
    std::string err;
    expect(readIndex(indexBody, err), "seed-read-index");
    EbCloudConfigIndex::Catalog category{};
    expect(EbCloudConfigIndex::parse(indexBody, category, err), "seed-parse-index");
    expect(!category.packs.empty(), "seed-has-packs");

    for (auto const& p : category.packs) {
        std::string packBody;
        expect(readPack(p.id, packBody, err), "seed-read-pack");
        expect(packBody.size() == p.size, "seed-size-match");
        expect(EbCloudConfigSha256::hashHex(packBody) == p.sha256, "seed-sha-match");
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

auto EbCloudConfigSeedEmbedded::indexJson() -> char const* {
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

auto EbCloudConfigSeedEmbedded::safeDefaultsPackJson() -> char const* {
    return "{\n"
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
