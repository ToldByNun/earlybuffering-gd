#include "dev_research_pack.hpp"
#include "../../core/bundle/config_bundle.hpp"
#include "../../core/bundle/config_bundle_codec.hpp"
#include "../../core/bundle/config_bundle_keys.hpp"
#include "../../core/platform/save_drop_paths.hpp"
#include "../capture/dev_trace_store.hpp"
#include "../dev_session.hpp"
#include "dev_report.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>
#include <chrono>
#include <fstream>
#include <system_error>

class EbDevResearchPackImplementation {
public:
    static std::string stampNow() {
        using clock = std::chrono::system_clock;
        auto const timestamp = clock::to_time_t(clock::now());
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &timestamp);
#else
        localtime_r(&timestamp, &tm);
#endif
        char buffer[32]{};
        std::strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &tm);
        return std::string(buffer);
    }

    static bool writeText(std::filesystem::path const& path, std::string const& body,
                          std::string& errMsg) {
        auto const written = geode::utils::file::writeString(path, body);
        if (written.isOk())
            return true;
        errMsg = written.unwrapErr();
        if (errMsg.empty())
            errMsg = "write failed";
        return false;
    }

    static bool copyFile(std::filesystem::path const& from, std::filesystem::path const& to,
                         std::string& errMsg) {
        std::error_code ec;
        std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            errMsg = ec.message();
            return false;
        }
        return true;
    }

    static std::filesystem::path newestSessionPath(std::filesystem::path const& tracesDir) {
        std::error_code ec;
        if (!std::filesystem::is_directory(tracesDir, ec) || ec)
            return {};
        std::filesystem::path best;
        std::filesystem::file_time_type bestTime{};
        bool have = false;
        for (auto const& entry : std::filesystem::directory_iterator(tracesDir, ec)) {
            if (ec)
                break;
            if (!entry.is_regular_file(ec) || ec)
                continue;
            auto const name = entry.path().filename().string();
            if (name.rfind("session-", 0) != 0 || entry.path().extension() != ".json")
                continue;
            auto const time = entry.last_write_time(ec);
            if (ec)
                continue;
            if (!have || time > bestTime) {
                best = entry.path();
                bestTime = time;
                have = true;
            }
        }
        return best;
    }

    static bool writeConfigNoKeybinds(std::filesystem::path const& path, std::string& errMsg) {
        auto* mod = geode::prelude::Mod::get();
        if (!mod) {
            errMsg = "mod unavailable";
            return false;
        }
        matjson::Value settings = matjson::Value::object();
        int written = 0;
        for (auto const* it = EbConfigBundleKeys::begin(); it != EbConfigBundleKeys::end(); ++it) {
            auto const& entry = *it;
            if (entry.kind == EbConfigBundleKeys::Kind::Keybind)
                continue;
            try {
                switch (entry.kind) {
                case EbConfigBundleKeys::Kind::Bool:
                    settings[entry.key] = mod->getSettingValue<bool>(entry.key);
                    ++written;
                    break;
                case EbConfigBundleKeys::Kind::Int:
                    settings[entry.key] = mod->getSettingValue<int64_t>(entry.key);
                    ++written;
                    break;
                case EbConfigBundleKeys::Kind::Float:
                    settings[entry.key] = mod->getSettingValue<double>(entry.key);
                    ++written;
                    break;
                case EbConfigBundleKeys::Kind::String:
                    settings[entry.key] = mod->getSettingValue<std::string>(entry.key);
                    ++written;
                    break;
                case EbConfigBundleKeys::Kind::Color: {
                    auto const color = mod->getSettingValue<cocos2d::ccColor3B>(entry.key);
                    settings[entry.key] = EbConfigBundleCodec::colorToHex(color);
                    ++written;
                    break;
                }
                default:
                    break;
                }
            } catch (...) {
                // Skip one bad key.
            }
        }
        matjson::Value root = matjson::Value::object();
        root["version"] = EbConfigBundle::kFormatVersion;
        root["settings"] = settings;
        root["keybindsOmitted"] = true;
        root["written"] = written;
        return writeText(path, root.dump(2), errMsg);
    }

private:
    EbDevResearchPackImplementation() = delete;
};

std::filesystem::path EbDevResearchPack::researchRoot() {
    return EbSaveDropPaths::get().rootDir() / "research";
}

bool EbDevResearchPack::exportPack(Result& out, std::string& errMsg) {
    out = {};
    errMsg.clear();

    std::string dirErr;
    if (!EbSaveDropPaths::get().ensureDirs(dirErr)) {
        errMsg = dirErr.empty() ? "save dir unavailable" : dirErr;
        return false;
    }

    auto const root = researchRoot();
    auto mkdir = geode::utils::file::createDirectoryAll(root);
    if (mkdir.isErr()) {
        errMsg = fmt::format("cannot create research dir: {}", mkdir.unwrapErr());
        return false;
    }

    auto const packDir = root / fmt::format("research_{}", EbDevResearchPackImplementation::stampNow());
    mkdir = geode::utils::file::createDirectoryAll(packDir);
    if (mkdir.isErr()) {
        errMsg = fmt::format("cannot create pack dir: {}", mkdir.unwrapErr());
        return false;
    }
    out.dir = packDir;

    auto noteFile = [&](char const* name) { out.files.emplace_back(name); };

    // Flush open session so disk matches memory.
    auto& store = EbDevTraceStore::get();
    if (store.isOpen())
        (void)store.flush();

    std::filesystem::path sessionSrc;
    if (store.isOpen()) {
        sessionSrc = store.sessionPath();
    } else {
        sessionSrc = EbDevResearchPackImplementation::newestSessionPath(store.tracesDir());
    }

    std::string sessionId;
    std::string levelKey;
    std::string levelName;
    std::string fingerprint;
    std::string modVersion;
    if (!sessionSrc.empty()) {
        auto const dest = packDir / "session.json";
        if (!EbDevResearchPackImplementation::copyFile(sessionSrc, dest, errMsg)) {
            errMsg = fmt::format("copy session failed: {}", errMsg);
            return false;
        }
        noteFile("session.json");
        out.hasSession = true;

        // Prefer live session meta when open; otherwise leave blank (MANIFEST still ok).
        if (store.isOpen()) {
            auto const& s = store.session();
            sessionId = s.sessionId;
            levelKey = s.levelKey;
            levelName = s.levelName;
            fingerprint = s.settingsFingerprint;
            modVersion = s.modVersion;
        }
    }

    if (fingerprint.empty())
        fingerprint = EbDevTraceStore::makeSettingsFingerprint();
    if (modVersion.empty()) {
        if (auto* mod = geode::prelude::Mod::get())
            modVersion = mod->getVersion().toVString();
    }

    {
        auto const fpPath = packDir / "settings_fingerprint.txt";
        if (!EbDevResearchPackImplementation::writeText(fpPath, fingerprint + "\n", errMsg)) {
            errMsg = fmt::format("fingerprint write failed: {}", errMsg);
            return false;
        }
        noteFile("settings_fingerprint.txt");
    }

    {
        auto const cfgPath = packDir / "config.json";
        if (!EbDevResearchPackImplementation::writeConfigNoKeybinds(cfgPath, errMsg)) {
            errMsg = fmt::format("config write failed: {}", errMsg);
            return false;
        }
        noteFile("config.json");
        out.hasConfig = true;
    }

    if (EbDevReport::hasLast()) {
        auto const& report = EbDevReport::last();
        std::string const overlay =
            report.overlayLine.empty() ? EbDevReport::formatOverlayLine(report) : report.overlayLine;
        if (!EbDevResearchPackImplementation::writeText(packDir / "report.txt", overlay + "\n",
                                                        errMsg)) {
            errMsg = fmt::format("report.txt write failed: {}", errMsg);
            return false;
        }
        noteFile("report.txt");

        std::string jsonBody = report.jsonLine;
        if (jsonBody.empty()) {
            matjson::Value fallback = matjson::Value::object();
            fallback["overlay"] = overlay;
            fallback["note"] = "jsonLine empty — eval may predate report.json support";
            jsonBody = fallback.dump(2);
        }
        if (!EbDevResearchPackImplementation::writeText(packDir / "report.json", jsonBody + "\n",
                                                        errMsg)) {
            errMsg = fmt::format("report.json write failed: {}", errMsg);
            return false;
        }
        noteFile("report.json");
        out.hasReport = true;
    }

    {
        std::string readme =
            "EarlyBuffer Dev Lab research pack\n"
            "\n"
            "session.json              — Dev Lab decision traces (if available)\n"
            "report.txt / report.json  — last eval overlay + structured report\n"
            "settings_fingerprint.txt  — compact assist/dev settings summary\n"
            "config.json               — portable settings (keybinds omitted)\n"
            "MANIFEST.json             — pack index\n"
            "\n"
            "For bake bugs also run Dump Diag (separate diag/full_* folder).\n";
        if (!EbDevResearchPackImplementation::writeText(packDir / "README.txt", readme, errMsg)) {
            errMsg = fmt::format("README write failed: {}", errMsg);
            return false;
        }
        noteFile("README.txt");
    }

    matjson::Value manifest = matjson::Value::object();
    manifest["manifestSchema"] = kManifestSchema;
    manifest["kind"] = "earlybuffer-research-pack";
    manifest["modVersion"] = modVersion;
    manifest["stamp"] = packDir.filename().string();
    manifest["sessionId"] = sessionId;
    manifest["levelKey"] = levelKey;
    manifest["levelName"] = levelName;
    manifest["hasSession"] = out.hasSession;
    manifest["hasReport"] = out.hasReport;
    manifest["hasConfig"] = out.hasConfig;
    manifest["recording"] = EbDevSession::get().isRecording();
    matjson::Value files = matjson::Value::array();
    for (auto const& name : out.files)
        files.push(name);
    manifest["files"] = files;
    if (!EbDevResearchPackImplementation::writeText(packDir / "MANIFEST.json",
                                                    manifest.dump(2) + "\n", errMsg)) {
        errMsg = fmt::format("MANIFEST write failed: {}", errMsg);
        return false;
    }
    noteFile("MANIFEST.json");

    errMsg.clear();
    return true;
}

bool EbDevResearchPack::selfCheck(std::string& report) {
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += report.empty() ? name : std::string(";") + name;
        }
    };

    expect(researchRoot().filename() == "research", "root-name");
    expect(kManifestSchema == 1, "schema");

    // Export path composition only when Mod save dir is available (may be empty
    // on bare hosts) — still exercise stamp + config builder lightly.
    auto const stamp = EbDevResearchPackImplementation::stampNow();
    expect(stamp.size() >= 15, "stamp-len");

    if (fails == 0) {
        report = "DevResearchPack selfCheck OK";
        return true;
    }
    report = fmt::format("DevResearchPack selfCheck {} fails: {}", fails, report);
    return false;
}
