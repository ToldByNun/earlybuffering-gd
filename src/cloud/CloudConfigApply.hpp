#ifndef EARLYBUFFER_CLOUD_CLOUDCONFIGAPPLY_HPP
#define EARLYBUFFER_CLOUD_CLOUDCONFIGAPPLY_HPP

#include "../core/ConfigBundle.hpp"

#include <string>

// Validate pack body (size/sha), filtered ConfigBundle import, write active.json.
// Does not show UI — caller runs PauseActionBridge::afterConfigApplied on success.
class CloudConfigApply {
public:
    struct Result {
        bool ok = false;
        std::string id;
        std::string error;
        ConfigBundle::ImportStats stats;
    };

    // expectedSha256 lowercase hex; expectedSize 0 → skip size check (still sha).
    static Result applyPackBody(
        std::string const& id,
        std::string const& displayName,
        std::string const& expectedSha256,
        size_t expectedSize,
        std::string const& body
    );

    // Cloud apply always skips keybinds + dev-* keys.
    static ConfigBundle::ApplyOptions cloudOptions();

    static bool selfCheck(std::string& report);

private:
    CloudConfigApply() = default;
};

#endif // EARLYBUFFER_CLOUD_CLOUDCONFIGAPPLY_HPP
