#include "SaveDropPaths.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>

#include <algorithm>
#include <cctype>
#include <system_error>

using namespace geode::prelude;

SaveDropPaths& SaveDropPaths::get() {
    static SaveDropPaths inst;
    return inst;
}

std::filesystem::path SaveDropPaths::rootDir() const {
    auto* mod = Mod::get();
    if (!mod) return std::filesystem::path("earlybuffer-save");
    return mod->getSaveDir();
}

std::filesystem::path SaveDropPaths::replayDropDir() const {
    return this->rootDir() / "replay-drop";
}

std::filesystem::path SaveDropPaths::configPath() const {
    return this->rootDir() / "earlybuffer-config.json";
}

std::filesystem::path SaveDropPaths::reportPath() const {
    return this->rootDir() / "earlybuffer-dev-report.txt";
}

bool SaveDropPaths::ensureDirs(std::string& err) const {
    err.clear();
    auto const root = this->rootDir();
    auto const replay = this->replayDropDir();

    auto rootRes = file::createDirectoryAll(root);
    if (rootRes.isErr()) {
        err = fmt::format("cannot create save dir {}: {}", root.string(), rootRes.unwrapErr());
        return false;
    }
    auto replayRes = file::createDirectoryAll(replay);
    if (replayRes.isErr()) {
        err = fmt::format(
            "cannot create replay drop dir {}: {}",
            replay.string(),
            replayRes.unwrapErr()
        );
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec) || ec) {
        err = fmt::format("save dir missing or not a directory: {}", root.string());
        return false;
    }
    if (!std::filesystem::is_directory(replay, ec) || ec) {
        err = fmt::format("replay drop dir missing or not a directory: {}", replay.string());
        return false;
    }
    return true;
}

bool SaveDropPaths::isReplayExtension(std::filesystem::path const& path) {
    auto ext = path.extension().string();
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext == ".gdr"
        || ext == ".gdr2"
        || ext == ".mhr"
        || ext == ".json"
        || ext == ".slc";
}

std::vector<std::filesystem::path> SaveDropPaths::listReplayCandidates() const {
    std::vector<std::filesystem::path> out;
    auto const dir = this->replayDropDir();
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec) || ec) {
        return out;
    }

    for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec) || ec) continue;
        auto const path = entry.path();
        if (!SaveDropPaths::isReplayExtension(path)) continue;
        // Skip zero-byte placeholders — Commit 4 will still surface clear errors.
        auto const sz = entry.file_size(ec);
        if (ec || sz == 0) continue;
        out.push_back(path);
    }

    std::sort(out.begin(), out.end(), [](auto const& a, auto const& b) {
        std::error_code eca;
        std::error_code ecb;
        auto const ta = std::filesystem::last_write_time(a, eca);
        auto const tb = std::filesystem::last_write_time(b, ecb);
        if (eca || ecb) return a.filename().string() > b.filename().string();
        if (ta != tb) return ta > tb; // newest first
        return a.filename().string() > b.filename().string();
    });
    return out;
}

bool SaveDropPaths::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    auto& paths = SaveDropPaths::get();
    auto const root = paths.rootDir();
    auto const replay = paths.replayDropDir();
    auto const config = paths.configPath();
    auto const reportFile = paths.reportPath();

    expect(!root.empty(), "root-nonempty");
    expect(replay.filename() == "replay-drop", "replay-dir-name");
    expect(replay.parent_path() == root, "replay-under-root");
    expect(config.filename() == "earlybuffer-config.json", "config-name");
    expect(config.parent_path() == root, "config-under-root");
    expect(reportFile.filename() == "earlybuffer-dev-report.txt", "report-name");
    expect(reportFile.parent_path() == root, "report-under-root");
    expect(SaveDropPaths::isReplayExtension(std::filesystem::path("x.gdr")), "ext-gdr");
    expect(SaveDropPaths::isReplayExtension(std::filesystem::path("x.GDR2")), "ext-gdr2-case");
    expect(SaveDropPaths::isReplayExtension(std::filesystem::path("x.mhr")), "ext-mhr");
    expect(SaveDropPaths::isReplayExtension(std::filesystem::path("x.json")), "ext-json");
    expect(SaveDropPaths::isReplayExtension(std::filesystem::path("x.slc")), "ext-slc");
    expect(!SaveDropPaths::isReplayExtension(std::filesystem::path("x.txt")), "ext-reject-txt");

    // When Mod is live, ensureDirs must succeed (Geode save dir is writable).
    if (Mod::get()) {
        std::string err;
        expect(paths.ensureDirs(err), "ensure-dirs");
        if (!err.empty() && fails > 0) {
            report += err + "; ";
        }
    }

    if (fails == 0) {
        report = "SaveDropPaths selfCheck OK";
        return true;
    }
    report = std::string("SaveDropPaths selfCheck fails: ") + report;
    return false;
}
