#include "gold_reproduce_gate.hpp"
#include "../../debug/assist_log.hpp"
#include "../platform/save_drop_paths.hpp"
#include "../replay/replay_import.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>
#include <asp/fs/std.hpp>
#include <cstdlib>
#include <fstream>
#include <system_error>

std::string EbGoldReproduceGate::envOrEmpty(char const* name) {
    if (!name)
        return {};
#if defined(_MSC_VER)
    char* buffer = nullptr;
    size_t length = 0;
    if (_dupenv_s(&buffer, &length, name) != 0 || !buffer)
        return {};
    std::string value(buffer);
    free(buffer);
    return value;
#else
    char const* value = std::getenv(name);
    return value ? std::string(value) : std::string{};
#endif
}

bool EbGoldReproduceGate::envTruthy(char const* name) {
    auto value = envOrEmpty(name);
    if (value.empty())
        return false;
    for (char& character : value) {
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
    }
    return value != "0" && value != "false" && value != "off" && value != "no";
}

std::filesystem::path EbGoldReproduceGate::saveCorpusRoot() {
    return EbSaveDropPaths::get().rootDir() / "real_corpus";
}

std::filesystem::path EbGoldReproduceGate::corpusRoot() {
    auto const fromEnv = envOrEmpty(kEnvCorpusDir);
    if (!fromEnv.empty()) {
        std::filesystem::path path(fromEnv);
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec) && !ec)
            return path;
    }
    auto const saveRoot = saveCorpusRoot();
    std::error_code ec;
    if (std::filesystem::is_directory(saveRoot, ec) && !ec)
        return saveRoot;
    return saveRoot;
}

std::vector<std::string> EbGoldReproduceGate::ids() {
    auto const root = corpusRoot();
    auto const manifestPath = root / "manifest.json";
    auto parsed = geode::utils::file::readJson(asp::fs::Path::fromStdPath(manifestPath));
    if (parsed.isOk()) {
        auto rootJson = parsed.unwrap();
        if (rootJson.contains("ids") && rootJson["ids"].isArray()) {
            std::vector<std::string> out;
            for (auto const& entry : rootJson["ids"]) {
                auto id = entry.asString().unwrapOr("");
                if (!id.empty())
                    out.push_back(id);
            }
            if (!out.empty())
                return out;
        }
    }
    return {"cataclysm", "quantum", "slaughterhouse", "hoveropolis"};
}

std::filesystem::path EbGoldReproduceGate::idDir(std::string const& corpusId) {
    return corpusRoot() / corpusId;
}

std::filesystem::path EbGoldReproduceGate::clearPath(std::string const& corpusId) {
    return idDir(corpusId) / "clear.gdr2";
}

std::filesystem::path EbGoldReproduceGate::reproducePath(std::string const& corpusId) {
    return idDir(corpusId) / "reproduce.json";
}

bool EbGoldReproduceGate::writeLastRunSummary(std::string const& text, std::string& err) {
    err.clear();
    auto const dir = EbSaveDropPaths::get().rootDir() / "real_assist";
    auto created = geode::utils::file::createDirectoryAll(asp::fs::Path::fromStdPath(dir));
    if (created.isErr()) {
        err = created.unwrapErr();
        return false;
    }
    auto const path = dir / "last_run.txt";
    auto written = geode::utils::file::writeString(asp::fs::Path::fromStdPath(path), text);
    if (written.isErr()) {
        err = written.unwrapErr();
        return false;
    }
    return true;
}

bool EbGoldReproduceGate::importAndStamp(std::string const& corpusId, std::string& report) {
    report.clear();
    if (corpusId.empty()) {
        report = "empty corpus id";
        return false;
    }
    auto const clear = clearPath(corpusId);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(clear, ec) || ec) {
        report = fmt::format("missing clear {}", clear.string());
        return false;
    }

    auto loaded = EbReplayImport::load(clear);
    if (loaded.isErr()) {
        report = fmt::format("import failed: {}", loaded.unwrapErr());
        return false;
    }
    auto const replay = loaded.unwrap();
    if (replay.events.empty()) {
        report = "import produced 0 events";
        return false;
    }

    int p2 = 0;
    int lastFrame = 0;
    for (auto const& event : replay.events) {
        if (event.player2)
            ++p2;
        if (event.frame > lastFrame)
            lastFrame = event.frame;
    }

    matjson::Value previous = matjson::Value::object();
    auto existing =
        geode::utils::file::readJson(asp::fs::Path::fromStdPath(reproducePath(corpusId)));
    if (existing.isOk())
        previous = existing.unwrap();

    std::string status =
        previous.contains("status") ? previous["status"].asString().unwrapOr("pending") : "pending";
    if (status != "pass" && status != "fail" && status != "pending")
        status = "pending";

    matjson::Value stamp = matjson::Value::object();
    stamp["schema"] = 1;
    stamp["corpusId"] = corpusId;
    stamp["status"] = status;
    if (previous.contains("clearSha256"))
        stamp["clearSha256"] = previous["clearSha256"];
    else
        stamp["clearSha256"] = "";

    matjson::Value clock = matjson::Value::object();
    clock["tps"] = replay.tps;
    clock["practice"] = false;
    clock["startPos"] = false;
    clock["assist"] = false;
    clock["percentStart"] = 0;
    stamp["clock"] = clock;

    matjson::Value importBlock = matjson::Value::object();
    importBlock["tps"] = replay.tps;
    importBlock["eventCount"] = static_cast<int>(replay.events.size());
    importBlock["lastFrame"] = lastFrame;
    importBlock["dual"] = p2 > 0;
    importBlock["format"] = replay.format;
    stamp["import"] = importBlock;

    if (previous.contains("attempt"))
        stamp["attempt"] = previous["attempt"];

    stamp["notes"] =
        "Import OK. status=pass requires unmodified PlayLayer gold clear (assist off).";
    stamp["playLayer"] = "pending";

    auto const outPath = reproducePath(corpusId);
    auto parent = outPath.parent_path();
    auto created = geode::utils::file::createDirectoryAll(asp::fs::Path::fromStdPath(parent));
    if (created.isErr()) {
        report = created.unwrapErr();
        return false;
    }
    auto written =
        geode::utils::file::writeString(asp::fs::Path::fromStdPath(outPath), stamp.dump(2) + "\n");
    if (written.isErr()) {
        report = written.unwrapErr();
        return false;
    }

    // Stage clear into replay-drop for manual PlayLayer / bake follow-up.
    auto const drop =
        EbSaveDropPaths::get().replayDropDir() / fmt::format("real_corpus_{}.gdr2", corpusId);
    {
        std::error_code copyEc;
        std::filesystem::create_directories(EbSaveDropPaths::get().replayDropDir(), copyEc);
        std::filesystem::copy_file(clear, drop, std::filesystem::copy_options::overwrite_existing,
                                   copyEc);
        if (copyEc) {
            EbAssistLog::info(fmt::format("WARN reproduce staging copy: {}", copyEc.message()));
        }
    }

    report = fmt::format("{} import OK events={} tps={:.0f} dual={} status={}", corpusId,
                         replay.events.size(), replay.tps, p2 > 0, status);
    return true;
}

bool EbGoldReproduceGate::runImportGate(std::string const& caseFilter, std::string& report) {
    report.clear();
    auto const root = corpusRoot();
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec) || ec) {
        report = fmt::format("corpus root missing: {}\n"
                             "Run: python tools/sync_real_corpus.py --install-save\n"
                             "Or set EB_REAL_CORPUS_DIR",
                             root.string());
        return false;
    }

    auto const all = ids();
    std::vector<std::string> selected;
    if (caseFilter.empty() || caseFilter == "all") {
        selected = all;
    } else {
        std::string id = caseFilter;
        auto slash = id.find('/');
        if (slash != std::string::npos)
            id = id.substr(0, slash);
        selected.push_back(id);
    }

    int fails = 0;
    std::string lines;
    for (auto const& id : selected) {
        std::string one;
        if (!importAndStamp(id, one)) {
            ++fails;
            lines += fmt::format("FAIL {}\n", one);
        } else {
            lines += fmt::format("PASS {}\n", one);
        }
    }

    lines += fmt::format("\n=== REAL ASSIST REPRODUCE (import gate) ===\n"
                         "corpus={}\n"
                         "ids={}\n"
                         "importFails={}\n"
                         "playLayer=pending (open matching level; unmodified clear must finish)\n"
                         "oracleBlockedUntil=reproduce=pass\n",
                         root.string(), selected.size(), fails);

    std::string writeErr;
    if (!writeLastRunSummary(lines, writeErr) && !writeErr.empty()) {
        lines += fmt::format("WARN last_run.txt: {}\n", writeErr);
    }

    report = lines;
    return fails == 0;
}

bool EbGoldReproduceGate::runDevAction(std::string& report) {
    bool const ok = runImportGate(envOrEmpty(kEnvCase), report);
    EbAssistLog::info(fmt::format("Dev Lab: Reproduce Gold\n{}", report));
    if (ok)
        geode::log::info("EarlyBuffer reproduce import gate:\n{}", report);
    else
        geode::log::warn("EarlyBuffer reproduce import gate:\n{}", report);
    return ok;
}

void EbGoldReproduceGate::maybeQueueAutoFromEnv() {
    if (!envTruthy(kEnvAuto))
        return;
    auto* loader = geode::Loader::get();
    if (!loader)
        return;
    loader->queueInMainThread([] {
        std::string report;
        bool const ok = runImportGate(envOrEmpty(kEnvCase), report);
        EbAssistLog::info(fmt::format("EB_REAL_ASSIST_AUTO reproduce import:\n{}", report));
        if (ok)
            geode::log::info("{}", report);
        else
            geode::log::warn("{}", report);
    });
}

bool EbGoldReproduceGate::statusIsPass(std::string const& corpusId) {
    auto parsed = geode::utils::file::readJson(asp::fs::Path::fromStdPath(reproducePath(corpusId)));
    if (parsed.isErr())
        return false;
    auto root = parsed.unwrap();
    return root.contains("status") && root["status"].asString().unwrapOr("") == "pass";
}

bool EbGoldReproduceGate::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string(name) + "; ";
        }
    };
    expect(std::string(kSchema) == "1", "schema");
    expect(std::string(kEnvAuto) == "EB_REAL_ASSIST_AUTO", "env-auto");
    expect(std::string(kEnvCase) == "EB_REAL_ASSIST_CASE", "env-case");
    expect(std::string(kEnvCorpusDir) == "EB_REAL_CORPUS_DIR", "env-dir");
    auto const known = ids();
    expect(known.size() == 4, "id-count");
    expect(!clearPath("cataclysm").empty(), "clear-path");
    expect(!reproducePath("quantum").empty(), "repro-path");
    if (fails == 0)
        report = "GoldReproduceGate OK";
    else
        report = fmt::format("GoldReproduceGate {} fails: {}", fails, report);
    return fails == 0;
}
