#include "CloudConfigShare.hpp"
#include "CloudConfigPublish.hpp"
#include "CloudConfigStore.hpp"

#include "../core/ConfigBundle.hpp"

#include <Geode/Geode.hpp>

#include <cctype>

using namespace geode::prelude;

std::string CloudConfigShare::discordCommunityUrl() {
    static char const* kFallback = "https://discord.gg/QJuTDkmPcf";
    auto* mod = Mod::get();
    if (!mod) return kFallback;
    if (auto url = mod->getMetadata().getLinks().getCommunityURL()) {
        if (!url->empty()) return *url;
    }
    return kFallback;
}

std::string CloudConfigShare::suggestId(std::string const& nameHint) {
    std::string out;
    out.reserve(32);
    for (char const c : nameHint) {
        unsigned char const uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc)) {
            out.push_back(static_cast<char>(std::tolower(uc)));
        }
        else if (c == ' ' || c == '-' || c == '_') {
            if (!out.empty() && out.back() != '-') out.push_back('-');
        }
        if (out.size() >= CloudConfigStore::kMaxIdLen) break;
    }
    while (!out.empty() && out.front() == '-') out.erase(out.begin());
    while (!out.empty() && out.back() == '-') out.pop_back();
    if (out.empty()) out = "shared-config";
    if (!CloudConfigStore::isValidPackId(out)) out = "shared-config";
    return out;
}

std::string CloudConfigShare::buildEnvelope(
    std::string const& suggestedId,
    std::string const& name,
    std::string const& author,
    std::string const& description,
    std::string const& bundleJson
) {
    auto bundleParsed = matjson::parse(bundleJson);
    matjson::Value bundle = bundleParsed.isOk()
        ? bundleParsed.unwrap()
        : matjson::Value(bundleJson);

    matjson::Value root = matjson::Value::object();
    root["schema"] = 1;
    root["suggestedId"] = suggestedId;
    root["name"] = name;
    root["author"] = author;
    root["description"] = description;
    root["bundle"] = bundle;
    return root.dump();
}

std::string CloudConfigShare::buildClipboardText(
    std::string const& suggestedId,
    std::filesystem::path const& outboxPath,
    std::string const& envelopeJson
) {
    return fmt::format(
        "EarlyBuffer cloud config share\n"
        "suggestedId: {}\n"
        "outbox: {}\n"
        "Discord: {}\n"
        "\n"
        "{}\n",
        suggestedId,
        outboxPath.string(),
        discordCommunityUrl(),
        envelopeJson
    );
}

CloudConfigShare::Result CloudConfigShare::shareCurrent(
    std::string const& nameHint,
    std::string const& author,
    std::string const& description
) {
    Result r{};

    std::string bundleJson;
    auto const exported = ConfigBundle::exportToString(bundleJson);
    if (exported.isErr()) {
        r.error = exported.unwrapErr().empty()
            ? "export failed"
            : exported.unwrapErr();
        return r;
    }

    std::string const name = nameHint.empty() ? "Shared Config" : nameHint;
    r.suggestedId = suggestId(nameHint.empty() ? name : nameHint);
    r.envelopeJson = buildEnvelope(
        r.suggestedId,
        name,
        author,
        description,
        bundleJson
    );

    std::string writeErr;
    if (!CloudConfigStore::get().ensureDirs(writeErr)) {
        r.error = writeErr.empty() ? "ensureDirs failed" : writeErr;
        return r;
    }
    if (!CloudConfigStore::get().writeOutbox(
            r.suggestedId, r.envelopeJson, r.outboxPath, writeErr
        )) {
        r.error = writeErr.empty() ? "outbox write failed" : writeErr;
        return r;
    }

    r.clipboardText = buildClipboardText(
        r.suggestedId, r.outboxPath, r.envelopeJson
    );
    r.clipboardOk = clipboard::write(r.clipboardText);
    r.ok = true;

    // Maintainer auto-publish (optional). Failure does not undo outbox share.
    if (CloudConfigPublish::isConfigured()) {
        CloudConfigPublish::PackMeta meta{};
        meta.id = r.suggestedId;
        meta.name = name;
        meta.author = author;
        meta.description = description;
        meta.tags = { "shared" };
        meta.minModVersion = "1.4.1";
        meta.bundleVersion = 2;
        auto const pub = CloudConfigPublish::publishPack(meta, bundleJson);
        r.publishAttempted = pub.attempted;
        r.published = pub.ok;
        r.publishUrl = pub.commitUrl;
        r.publishError = pub.error;
    }

    return r;
}

bool CloudConfigShare::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    expect(suggestId("") == "shared-config", "id-empty");
    expect(suggestId("My Cool Pack!") == "my-cool-pack", "id-sanitize");
    expect(CloudConfigStore::isValidPackId(suggestId("Hello")), "id-valid");

    std::string env = buildEnvelope(
        "demo-pack",
        "Demo",
        "Tester",
        "desc",
        R"({"version":2,"settings":{"safe-mode":true}})"
    );
    expect(env.find("\"schema\":") != std::string::npos, "env-schema");
    expect(env.find("demo-pack") != std::string::npos, "env-id");
    expect(env.find("safe-mode") != std::string::npos, "env-bundle");

    expect(!discordCommunityUrl().empty(), "discord-url");

    if (Mod::get()) {
        auto const shared = shareCurrent("PlanGold Share", "EarlyBuffer", "selfCheck");
        expect(shared.ok, "share-ok");
        expect(!shared.outboxPath.empty(), "share-path");
        expect(!shared.envelopeJson.empty(), "share-json");
        std::error_code ec;
        expect(
            std::filesystem::is_regular_file(shared.outboxPath, ec) && !ec,
            "share-file"
        );
    }

    if (fails == 0) {
        report = "CloudConfigShare selfCheck OK";
        return true;
    }
    report = std::string("CloudConfigShare selfCheck fails: ") + report;
    return false;
}
