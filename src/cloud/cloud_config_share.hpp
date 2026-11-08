#ifndef CLOUD_CONFIG_SHARE_HPP
#define CLOUD_CONFIG_SHARE_HPP

#include <filesystem>
#include <string>
#include <vector>

// Export current settings → outbox envelope + clipboard + public submit.
class CloudConfigShare {
public:
    static constexpr size_t kMaxTags = 16;
    static constexpr size_t kMaxTagLen = 32;

    class Result {
    public:
        bool ok = false;
        bool clipboardOk = false;
        bool publishAttempted = false;
        bool published = false;
        std::filesystem::path outboxPath;
        std::string envelopeJson;
        std::string clipboardText;
        std::string error;
        std::string suggestedId;
        std::vector<std::string> tags;
        std::string publishError;
        std::string publishUrl;

    private:
    };

    // nameHint empty → "shared-config". Author/description optional.
    // tags empty → { "shared" }.
    static Result shareCurrent(std::string const& nameHint = {}, std::string const& author = {},
                               std::string const& description = {},
                               std::vector<std::string> const& tags = {});

    // Pack id suggestion from a display name (no disk/network).
    static std::string suggestId(std::string const& nameHint);

    // Split comma/space tags; lowercase [a-z0-9-]; dedupe; empty → { "shared" }.
    static std::vector<std::string> parseTags(std::string const& raw);

    static std::string discordCommunityUrl();

    static bool selfCheck(std::string& report);

private:
    CloudConfigShare() = default;

    static std::string sanitizeTagToken(std::string const& raw);
    static std::string buildEnvelope(std::string const& suggestedId, std::string const& name,
                                     std::string const& author, std::string const& description,
                                     std::vector<std::string> const& tags,
                                     std::string const& bundleJson);
    static std::string buildClipboardText(std::string const& suggestedId,
                                          std::filesystem::path const& outboxPath,
                                          std::string const& envelopeJson);
};

#endif
