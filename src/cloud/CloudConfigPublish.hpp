#ifndef EARLYBUFFER_CLOUD_CLOUDCONFIGPUBLISH_HPP
#define EARLYBUFFER_CLOUD_CLOUDCONFIGPUBLISH_HPP

#include <string>
#include <vector>

// Optional maintainer publish: GitHub Contents API → earlybuffer-configs.
// Requires local PAT setting (never bundled / never exported). No anonymous upload.
class CloudConfigPublish {
public:
    struct Result {
        bool attempted = false;
        bool ok = false;
        bool created = false; // true = new file, false = updated existing
        std::string repo;
        std::string packPath;
        std::string commitUrl;
        std::string error;
    };

    struct PackMeta {
        std::string id;
        std::string name;
        std::string author;
        std::string description;
        std::vector<std::string> tags;
        std::string minModVersion = "1.4.1";
        int bundleVersion = 2;
    };

    // True when enable + non-empty token + valid repo slug.
    static bool isConfigured();

    static std::string readTokenSetting();
    static std::string readRepoSetting();
    static bool readEnableSetting();

    // PUT packs/<id>.json then upsert index.json on branch main.
    // bundleJson = bare ConfigBundle document (not the share envelope).
    static Result publishPack(PackMeta const& meta, std::string const& bundleJson);

    static bool selfCheck(std::string& report);

private:
    CloudConfigPublish() = default;

    static bool isValidRepoSlug(std::string const& slug);
    static std::string contentsApiUrl(std::string const& repo, std::string const& path);
    static std::string isoNowUtc();
};

#endif // EARLYBUFFER_CLOUD_CLOUDCONFIGPUBLISH_HPP
