#include "cloud_config_apply.hpp"
#include "cloud_config_store.hpp"
#include "transport/cloud_config_sha256.hpp"
#include <Geode/Geode.hpp>
#include <chrono>

ConfigBundle::ApplyOptions CloudConfigApply::cloudOptions() {
    ConfigBundle::ApplyOptions opt{};
    opt.skipKeybinds = true;
    opt.skipDevKeys = true;
    return opt;
}

CloudConfigApply::Result CloudConfigApply::applyPackBody(std::string const& id,
                                                         std::string const& displayName,
                                                         std::string const& expectedSha256,
                                                         size_t expectedSize,
                                                         std::string const& body) {
    Result result{};
    result.id = id;

    if (!CloudConfigStore::isValidPackId(id)) {
        result.error = "invalid pack id";
        return result;
    }
    if (body.empty()) {
        result.error = "pack body empty";
        return result;
    }
    if (body.size() > CloudConfigStore::kMaxPackBytes) {
        result.error = "pack body too large";
        return result;
    }
    if (expectedSize > 0 && body.size() != expectedSize) {
        result.error =
            fmt::format("size mismatch ({} != {})", static_cast<unsigned long long>(body.size()),
                        static_cast<unsigned long long>(expectedSize));
        return result;
    }
    if (expectedSha256.size() != 64) {
        result.error = "expected sha256 invalid";
        return result;
    }
    auto const hex = CloudConfigSha256::hashHex(body);
    if (hex != expectedSha256) {
        result.error = "sha256 mismatch";
        std::string remErr;
        (void)CloudConfigStore::get().removePack(id, remErr);
        return result;
    }

    auto const imported = ConfigBundle::importFromString(body, cloudOptions());
    if (imported.isErr()) {
        result.error = imported.unwrapErr().empty() ? "import failed" : imported.unwrapErr();
        return result;
    }

    result.stats = ConfigBundle::lastImportStats();

    CloudConfigStore::ActiveInfo active{};
    active.id = id;
    active.sha256 = expectedSha256;
    active.name = displayName.empty() ? id : displayName;
    auto const secs = std::chrono::duration_cast<std::chrono::seconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
    active.appliedAt = fmt::format("{}", secs);

    std::string writeErr;
    (void)CloudConfigStore::get().ensureDirs(writeErr);
    if (!CloudConfigStore::get().writeActive(active, writeErr)) {
        // Settings already applied — report soft failure detail but still ok.
        result.error = writeErr.empty() ? "active write failed" : writeErr;
    }

    // Cache pack bytes for offline reinstall.
    std::string packErr;
    (void)CloudConfigStore::get().writePack(id, body, packErr);

    result.ok = true;
    return result;
}

bool CloudConfigApply::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    auto const opt = cloudOptions();
    expect(opt.skipKeybinds, "skip-kb");
    expect(opt.skipDevKeys, "skip-dev");

    Result badId = applyPackBody("Bad", "x", std::string(64, 'a'), 1, "{}");
    expect(!badId.ok, "reject-bad-id");

    Result empty = applyPackBody("safe-defaults", "x", std::string(64, 'a'), 0, "");
    expect(!empty.ok, "reject-empty");

    std::string body = R"({"version":2,"settings":{"safe-mode":true}})";
    auto const realSha = CloudConfigSha256::hashHex(body);
    Result badSha = applyPackBody("safe-defaults", "Safe", std::string(64, 'b'), body.size(), body);
    expect(!badSha.ok, "reject-bad-sha");
    expect(badSha.error.find("sha256") != std::string::npos, "bad-sha-msg");

    // Do not mutate live settings in PlanGold — reject paths + hash helper only.
    expect(realSha.size() == 64, "sha-len");
    (void)body;

    if (fails == 0) {
        report = "CloudConfigApply selfCheck OK";
        return true;
    }
    report = std::string("CloudConfigApply selfCheck fails: ") + report;
    return false;
}
