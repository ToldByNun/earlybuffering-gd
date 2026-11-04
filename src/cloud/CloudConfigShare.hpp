#ifndef EARLYBUFFER_CLOUD_CLOUDCONFIGSHARE_HPP
#define EARLYBUFFER_CLOUD_CLOUDCONFIGSHARE_HPP

#include <filesystem>
#include <string>

// Export current settings → outbox envelope + clipboard (no HTTP upload).
class CloudConfigShare {
public:
    struct Result {
        bool ok = false;
        bool clipboardOk = false;
        std::filesystem::path outboxPath;
        std::string envelopeJson;
        std::string clipboardText;
        std::string error;
        std::string suggestedId;
    };

    // nameHint empty → "shared-config". Author/description optional.
    static Result shareCurrent(
        std::string const& nameHint = {},
        std::string const& author = {},
        std::string const& description = {}
    );

    // Pack id suggestion from a display name (no disk/network).
    static std::string suggestId(std::string const& nameHint);

    static std::string discordCommunityUrl();

    static bool selfCheck(std::string& report);

private:
    CloudConfigShare() = default;

    static std::string buildEnvelope(
        std::string const& suggestedId,
        std::string const& name,
        std::string const& author,
        std::string const& description,
        std::string const& bundleJson
    );
    static std::string buildClipboardText(
        std::string const& suggestedId,
        std::filesystem::path const& outboxPath,
        std::string const& envelopeJson
    );
};

#endif // EARLYBUFFER_CLOUD_CLOUDCONFIGSHARE_HPP
