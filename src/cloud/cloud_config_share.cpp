#include "cloud_config_share.hpp"
#include "../core/config_bundle.hpp"
#include "cloud_config_publish.hpp"
#include "cloud_config_store.hpp"
#include "cloud_config_submit.hpp"
#include <Geode/Geode.hpp>
#include <cctype>
#include <unordered_set>

std::string CloudConfigShare::discordCommunityUrl() {
    static char const* kFallback = "https://discord.gg/QJuTDkmPcf";
    auto* mod = geode::prelude::Mod::get();
    if (!mod)
        return kFallback;
    if (auto url = mod->getMetadata().getLinks().getCommunityURL()) {
        if (!url->empty())
            return *url;
    }
    return kFallback;
}

std::string CloudConfigShare::suggestId(std::string const& nameHint) {
    std::string out;
    out.reserve(32);
    for (char const character : nameHint) {
        unsigned char const unsignedCharacter = static_cast<unsigned char>(character);
        if (std::isalnum(unsignedCharacter)) {
            out.push_back(static_cast<char>(std::tolower(unsignedCharacter)));
        } else if (character == ' ' || character == '-' || character == '_') {
            if (!out.empty() && out.back() != '-')
                out.push_back('-');
        }
        if (out.size() >= CloudConfigStore::kMaxIdLen)
            break;
    }
    while (!out.empty() && out.front() == '-')
        out.erase(out.begin());
    while (!out.empty() && out.back() == '-')
        out.pop_back();
    if (out.empty())
        out = "shared-config";
    if (!CloudConfigStore::isValidPackId(out))
        out = "shared-config";
    return out;
}

std::string CloudConfigShare::sanitizeTagToken(std::string const& raw) {
    std::string out;
    out.reserve(raw.size());
    for (char const character : raw) {
        unsigned char const unsignedCharacter = static_cast<unsigned char>(character);
        if (std::isalnum(unsignedCharacter)) {
            out.push_back(static_cast<char>(std::tolower(unsignedCharacter)));
        } else if (character == '-' || character == '_') {
            if (!out.empty() && out.back() != '-')
                out.push_back('-');
        }
        if (out.size() >= kMaxTagLen)
            break;
    }
    while (!out.empty() && out.front() == '-')
        out.erase(out.begin());
    while (!out.empty() && out.back() == '-')
        out.pop_back();
    if (out.size() > kMaxTagLen)
        out.resize(kMaxTagLen);
    return out;
}

std::vector<std::string> CloudConfigShare::parseTags(std::string const& raw) {
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    std::string token;

    auto flush = [&]() {
        auto const tick = sanitizeTagToken(token);
        token.clear();
        if (tick.empty())
            return;
        if (seen.count(tick))
            return;
        if (out.size() >= kMaxTags)
            return;
        seen.insert(tick);
        out.push_back(tick);
    };

    for (char const character : raw) {
        if (character == ',' || character == ';' ||
            std::isspace(static_cast<unsigned char>(character))) {
            flush();
        } else {
            token.push_back(character);
        }
    }
    flush();

    if (out.empty())
        out.push_back("shared");
    return out;
}

std::string CloudConfigShare::buildEnvelope(std::string const& suggestedId, std::string const& name,
                                            std::string const& author,
                                            std::string const& description,
                                            std::vector<std::string> const& tags,
                                            std::string const& bundleJson) {
    auto bundleParsed = matjson::parse(bundleJson);
    matjson::Value bundle =
        bundleParsed.isOk() ? bundleParsed.unwrap() : matjson::Value(bundleJson);

    matjson::Value tagsArr = matjson::Value::array();
    for (auto const& t : tags)
        tagsArr.push(t);
    if (tags.empty())
        tagsArr.push("shared");

    matjson::Value root = matjson::Value::object();
    root["schema"] = 1;
    root["suggestedId"] = suggestedId;
    root["name"] = name;
    root["author"] = author;
    root["description"] = description;
    root["tags"] = tagsArr;
    root["bundle"] = bundle;
    return root.dump();
}

std::string CloudConfigShare::buildClipboardText(std::string const& suggestedId,
                                                 std::filesystem::path const& outboxPath,
                                                 std::string const& envelopeJson) {
    return fmt::format("EarlyBuffer cloud config share\n"
                       "suggestedId: {}\n"
                       "outbox: {}\n"
                       "Discord: {}\n"
                       "\n"
                       "{}\n",
                       suggestedId, outboxPath.string(), discordCommunityUrl(), envelopeJson);
}

CloudConfigShare::Result CloudConfigShare::shareCurrent(std::string const& nameHint,
                                                        std::string const& author,
                                                        std::string const& description,
                                                        std::vector<std::string> const& tagsIn) {
    Result result{};

    std::string bundleJson;
    auto const exported = ConfigBundle::exportToString(bundleJson);
    if (exported.isErr()) {
        result.error = exported.unwrapErr().empty() ? "export failed" : exported.unwrapErr();
        return result;
    }

    std::string const name = nameHint.empty() ? "Shared Config" : nameHint;
    result.suggestedId = suggestId(nameHint.empty() ? name : nameHint);
    // Re-sanitize even if caller already parsed (cap / format / empty → shared).
    {
        std::string joined;
        for (size_t index = 0; index < tagsIn.size(); ++index) {
            if (index)
                joined.push_back(',');
            joined += tagsIn[index];
        }
        result.tags = parseTags(joined);
    }

    result.envelopeJson =
        buildEnvelope(result.suggestedId, name, author, description, result.tags, bundleJson);

    std::string writeErr;
    if (!CloudConfigStore::get().ensureDirs(writeErr)) {
        result.error = writeErr.empty() ? "ensureDirs failed" : writeErr;
        return result;
    }
    if (!CloudConfigStore::get().writeOutbox(result.suggestedId, result.envelopeJson,
                                             result.outboxPath, writeErr)) {
        result.error = writeErr.empty() ? "outbox write failed" : writeErr;
        return result;
    }

    result.clipboardText =
        buildClipboardText(result.suggestedId, result.outboxPath, result.envelopeJson);
    result.clipboardOk = geode::utils::clipboard::write(result.clipboardText);
    result.ok = true;

    CloudConfigPublish::PackMeta meta{};
    meta.id = result.suggestedId;
    meta.name = name;
    meta.author = author;
    meta.description = description;
    meta.tags = result.tags;
    meta.minModVersion = "1.4.1";
    meta.bundleVersion = 2;

    auto const sub = CloudConfigSubmit::submit(meta, bundleJson);
    result.publishAttempted = sub.attempted;
    result.published = sub.ok;
    result.publishUrl = sub.prOrCommitUrl;
    result.publishError = sub.error;

    return result;
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

    auto const emptyTags = parseTags("");
    expect(emptyTags.size() == 1 && emptyTags[0] == "shared", "tags-empty");
    auto const multi = parseTags("Safe, LIVE beginner;safe");
    expect(multi.size() == 3, "tags-dedupe-count");
    expect(multi[0] == "safe" && multi[1] == "live" && multi[2] == "beginner", "tags-order");
    expect(parseTags("!!!").size() == 1 && parseTags("!!!")[0] == "shared", "tags-junk");

    std::string env = buildEnvelope("demo-pack", "Demo", "Tester", "desc", {"safe", "live"},
                                    R"({"version":2,"settings":{"safe-mode":true}})");
    expect(env.find("\"schema\":") != std::string::npos, "env-schema");
    expect(env.find("demo-pack") != std::string::npos, "env-id");
    expect(env.find("safe-mode") != std::string::npos, "env-bundle");
    expect(env.find("\"tags\"") != std::string::npos, "env-tags");

    expect(!discordCommunityUrl().empty(), "discord-url");

    if (geode::prelude::Mod::get()) {
        auto const shared =
            shareCurrent("PlanGold Share", "EarlyBuffer", "selfCheck", {"selfcheck"});
        expect(shared.ok, "share-ok");
        expect(!shared.outboxPath.empty(), "share-path");
        expect(!shared.envelopeJson.empty(), "share-json");
        expect(shared.tags.size() == 1 && shared.tags[0] == "selfcheck", "share-tags");
        std::error_code ec;
        expect(std::filesystem::is_regular_file(shared.outboxPath, ec) && !ec, "share-file");
    }

    if (fails == 0) {
        report = "CloudConfigShare selfCheck OK";
        return true;
    }
    report = std::string("CloudConfigShare selfCheck fails: ") + report;
    return false;
}
