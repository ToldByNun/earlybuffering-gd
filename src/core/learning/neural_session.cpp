#include "neural_session.hpp"
#include "../../app/class_manager.hpp"
#include "../../debug/assist_log.hpp"
#include "ai_config.hpp"
#include <Geode/Geode.hpp>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <system_error>

EbNeuralSession& EbNeuralSession::get() {
    return EbClassManager::get().service<EbNeuralSession>();
}

std::filesystem::path EbNeuralSession::sessionsDir() {
    auto* mod = geode::Mod::get();
    if (!mod)
        return {};
    return mod->getSaveDir() / "ai" / "sessions";
}

bool EbNeuralSession::saveRange(std::filesystem::path const& path, int startIndex,
                                int endIndex) const {
    if (path.empty() || m_count <= 0)
        return false;
    int const end = std::clamp(endIndex, 0, m_count);
    int const start = std::clamp(startIndex, 0, end);
    if (start >= end)
        return false;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    std::uint32_t magic = kFileMagic;
    std::uint32_t version = kFileVersion;
    std::uint32_t featureDim = static_cast<std::uint32_t>(EbNeuralModel::kFeatureDim);
    std::uint32_t count = static_cast<std::uint32_t>(end - start);
    out.write(reinterpret_cast<char const*>(&magic), sizeof(magic));
    out.write(reinterpret_cast<char const*>(&version), sizeof(version));
    out.write(reinterpret_cast<char const*>(&featureDim), sizeof(featureDim));
    out.write(reinterpret_cast<char const*>(&count), sizeof(count));
    for (int index = start; index < end; ++index) {
        Sample const* sample = logicalAt(index);
        if (!sample)
            return false;
        out.write(reinterpret_cast<char const*>(sample->state), sizeof(sample->state));
        out.write(reinterpret_cast<char const*>(sample->next), sizeof(sample->next));
        std::int32_t action = sample->action;
        std::int32_t limit = sample->limit;
        float reward = sample->reward;
        std::uint8_t done = sample->done ? 1u : 0u;
        out.write(reinterpret_cast<char const*>(&action), sizeof(action));
        out.write(reinterpret_cast<char const*>(&limit), sizeof(limit));
        out.write(reinterpret_cast<char const*>(&reward), sizeof(reward));
        out.write(reinterpret_cast<char const*>(&done), sizeof(done));
    }
    return static_cast<bool>(out);
}

int EbNeuralSession::loadFile(std::filesystem::path const& path) {
    if (path.empty() || !std::filesystem::is_regular_file(path))
        return 0;
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return 0;
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint32_t featureDim = 0;
    std::uint32_t count = 0;
    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    in.read(reinterpret_cast<char*>(&featureDim), sizeof(featureDim));
    in.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (!in || magic != kFileMagic || version != kFileVersion ||
        featureDim != static_cast<std::uint32_t>(EbNeuralModel::kFeatureDim) || count == 0 ||
        count > 100000u)
        return 0;
    int added = 0;
    for (std::uint32_t index = 0; index < count; ++index) {
        Sample sample{};
        in.read(reinterpret_cast<char*>(sample.state), sizeof(sample.state));
        in.read(reinterpret_cast<char*>(sample.next), sizeof(sample.next));
        std::int32_t action = 0;
        std::int32_t limit = 0;
        float reward = 0.f;
        std::uint8_t done = 0;
        in.read(reinterpret_cast<char*>(&action), sizeof(action));
        in.read(reinterpret_cast<char*>(&limit), sizeof(limit));
        in.read(reinterpret_cast<char*>(&reward), sizeof(reward));
        in.read(reinterpret_cast<char*>(&done), sizeof(done));
        if (!in)
            break;
        if (push(sample.state, action, limit, reward, sample.next, done != 0))
            ++added;
    }
    return added;
}

int EbNeuralSession::loadDirectory(std::filesystem::path const& dir) {
    if (dir.empty() || !std::filesystem::is_directory(dir))
        return 0;
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec)
            break;
        if (!entry.is_regular_file())
            continue;
        if (entry.path().extension() == ".ebns")
            files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end(), [](auto const& a, auto const& b) {
        std::error_code errorA;
        std::error_code errorB;
        auto ta = std::filesystem::last_write_time(a, errorA);
        auto tb = std::filesystem::last_write_time(b, errorB);
        return ta > tb;
    });
    int added = 0;
    for (auto const& file : files) {
        if (m_count >= kCapacity)
            break;
        added += loadFile(file);
    }
    return added;
}

bool EbNeuralSession::maybePersistAttempt() {
    auto const& cfg = EbAiConfig::get();
    if (!cfg.persistSessions)
        return false;
    if (m_count <= m_persistedThrough)
        return false;
    auto dir = sessionsDir();
    if (dir.empty())
        return false;
    auto const stamp = std::chrono::system_clock::now().time_since_epoch().count();
    auto path = dir / fmt::format("sess_{}_a{}_{}.ebns", stamp, m_attempts, m_count);
    if (!saveRange(path, m_persistedThrough, m_count)) {
        EbAssistLog::info(fmt::format("AI session persist failed: {}", path.string()));
        return false;
    }
    m_persistedThrough = m_count;
    m_lastPersistPath = path.string();
    ++m_filesSaved;
    EbAssistLog::info(fmt::format("AI session saved {} samples → {}", m_count, path.string()));
    return true;
}

bool EbNeuralSession::selfCheck(std::string& report) {
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };
    EbNeuralSession session;
    session.clear();
    expect(session.size() == 0, "empty");
    float state[EbNeuralModel::kFeatureDim]{};
    float next[EbNeuralModel::kFeatureDim]{};
    state[0] = 0.25f;
    next[0] = 0.5f;
    expect(session.push(state, 3, 12, 1.0f, next, false), "push");
    expect(session.size() == 1, "size-1");
    session.beginAttempt();
    session.endAttempt();
    expect(session.attemptCount() == 1, "attempt");
    EbNeuralModel model;
    model.reset();
    expect(session.train(model, 0.01f, 1) > 0, "train");

    auto tmp = std::filesystem::temp_directory_path() / "eb_session_selfcheck.ebns";
    expect(session.saveRange(tmp, 0, session.size()), "save");
    EbNeuralSession loaded;
    loaded.clear();
    expect(loaded.loadFile(tmp) == 1, "load");
    expect(loaded.size() == 1, "load-size");
    std::error_code ec;
    std::filesystem::remove(tmp, ec);

    if (fails == 0) {
        report = "NeuralSession OK";
        return true;
    }
    report = std::string("NeuralSession fails: ") + report;
    return false;
}
