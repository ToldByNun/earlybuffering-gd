#include "dev_session.hpp"
#include "../core/assist/assist_controller.hpp"
#include "../core/learning/level_profile.hpp"
#include "../debug/assist_log.hpp"
#include "capture/dev_cycle_recorder.hpp"
#include "capture/dev_trace_store.hpp"
#include "dev_config.hpp"
#include <Geode/Geode.hpp>
#include <cstring>
#include <fmt/format.h>

DevSession& DevSession::get() {
    static DevSession inst;
    return inst;
}

void DevSession::refreshLevelMeta() {
    auto& store = DevTraceStore::get();
    if (!store.isOpen())
        return;

    auto* layer = PlayLayer::get();
    std::string key;
    std::string name;
    bool startPos = false;
    bool practice = false;
    if (layer && layer->m_level) {
        key = LevelProfile::makeKey(layer->m_level);
        name = std::string(layer->m_level->m_levelName);
        startPos = layer->m_isTestMode;
        practice = layer->m_isPracticeMode;
    }
    store.setLevelMeta(std::move(key), std::move(name), startPos, practice,
                       DevTraceStore::makeSettingsFingerprint());
}

bool DevSession::start() {
    if (!DevConfig::recordEnable()) {
        AssistLog::info("Dev Lab: record enable off — cannot start session");
        return false;
    }
    if (this->m_state == State::Recording && DevTraceStore::get().isOpen()) {
        AssistLog::info("Dev Lab: session already recording");
        return true;
    }

    auto& store = DevTraceStore::get();
    int const window = DevConfig::recordCount();
    if (!store.beginSession("", window)) {
        this->m_state = State::Idle;
        AssistLog::info("Dev Lab: beginSession failed");
        return false;
    }

    this->refreshLevelMeta();

    int const tick = AssistController::get().attemptTick();
    if (!store.beginAttempt(tick)) {
        store.closeSession();
        this->m_state = State::Idle;
        AssistLog::info("Dev Lab: beginAttempt failed");
        return false;
    }

    DevCycleRecorder::get().clearActive();
    this->m_state = State::Recording;
    AssistLog::info(fmt::format("Dev Lab: recording session {} (rolling last {})",
                                store.session().sessionId, window));
    return true;
}

void DevSession::stop() {
    auto& store = DevTraceStore::get();
    if (this->m_state != State::Recording && !store.isOpen()) {
        this->m_state = State::Idle;
        return;
    }

    if (store.hasOpenAttempt()) {
        (void)DevCycleRecorder::get().sealAttemptWithOutcomes("manual", false);
        if (store.hasOpenAttempt()) {
            (void)store.sealAttempt("manual", false);
        }
    } else {
        (void)store.flush();
    }

    auto const path = store.sessionPath();
    store.closeSession();
    DevCycleRecorder::get().clearActive();
    this->m_state = State::Idle;
    AssistLog::info(fmt::format("Dev Lab: session stopped → {}", path.string()));
}

std::string DevSession::statusLine() const {
    if (this->m_state != State::Recording || !DevTraceStore::get().isOpen()) {
        return "Dev idle";
    }
    auto const& s = DevTraceStore::get().session();
    int const count = s.recordingsTarget > 0 ? s.recordingsTarget : DevConfig::recordCount();
    int const offset = s.recordingsDone;
    if (offset >= count) {
        return fmt::format("REC {}/{} rolling", count, count);
    }
    return fmt::format("REC {}/{}", offset, count);
}

std::filesystem::path DevSession::sessionPath() const {
    if (!(!DevTraceStore::get().isOpen())) {
        return DevTraceStore::get().sessionPath();
    }
    return {};
}

int DevSession::clearTraces() {
    if (this->m_state == State::Recording || DevTraceStore::get().isOpen()) {
        this->stop();
    }

    auto const directory = DevTraceStore::get().tracesDir();
    if (directory.empty() || !std::filesystem::exists(directory))
        return 0;

    int removed = 0;
    std::error_code ec;
    for (auto const& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (ec)
            break;
        if (!entry.is_regular_file(ec))
            continue;
        auto const ext = entry.path().extension().string();
        auto const name = entry.path().filename().string();
        // Only session JSON (and leftover .tmp) under devtraces/.
        if (name.rfind("session-", 0) != 0)
            continue;
        bool const isJson = (ext == ".json") || (name.find(".json.tmp") != std::string::npos);
        if (!isJson)
            continue;
        std::filesystem::remove(entry.path(), ec);
        if (!ec)
            ++removed;
        ec.clear();
    }
    AssistLog::info(
        fmt::format("Dev Lab: cleared {} trace file(s) under {}", removed, directory.string()));
    return removed;
}

bool DevSession::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok)
            return;
        ++fails;
        if (!report.empty())
            report += ", ";
        report += name;
    };

    auto& sess = DevSession::get();
    auto& store = DevTraceStore::get();

    // Ensure idle start.
    if (store.isOpen())
        sess.stop();
    expect(sess.state() == State::Idle, "idle");

    // Rolling trim via TraceStore (independent of recordEnable).
    {
        expect(store.beginSession("session-roll-selfcheck", 2), "roll-begin");
        for (int index = 0; index < 4; ++index) {
            expect(store.beginAttempt(index * 10), "roll-attempt");
            DevDecisionEvent ev;
            ev.pressId = store.allocPressId();
            ev.kind = DevEventKind::PressObserved;
            ev.down = true;
            expect(store.appendEvent(ev) != nullptr, "roll-press");
            expect(store.sealAttempt("death", true), "roll-seal");
        }
        expect(store.session().recordingsDone == 2, "roll-done-2");
        int counted = 0;
        for (auto const& a : store.session().attempts) {
            if (a.sealed && a.counted)
                ++counted;
        }
        expect(counted == 2, "roll-kept-2");
        // Oldest counted attempts dropped; attemptIds of survivors are 3 and 4.
        expect(store.session().attempts.size() >= 2 &&
                   store.session().attempts.front().attemptId >= 3,
               "roll-oldest-gone");
        store.closeSession();
        auto path = store.tracesDir() / "session-session-roll-selfcheck.json";
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    expect(std::strcmp(sess.statusLine().c_str(), "Dev idle") == 0 || sess.state() == State::Idle,
           "status-idle");

    if (fails == 0) {
        report = "DevSession selfCheck OK";
        return true;
    }
    report = std::string("DevSession selfCheck fails: ") + report;
    return false;
}
