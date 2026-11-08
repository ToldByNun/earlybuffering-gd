#include "dev_trace_validation.hpp"
#include "../../../core/platform/platform_limits.hpp"
#include "../dev_trace_store.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>
#include <cstdio>
#include <cstring>
#include <fmt/format.h>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

bool DevTraceValidation::validatePressChain(std::vector<DevDecisionEvent> const& events,
                                            int64_t pressId, std::string& err) {
    err.clear();
    if (pressId <= 0) {
        err = "pressId<=0";
        return false;
    }

    std::vector<DevDecisionEvent const*> chain;
    chain.reserve(events.size());
    for (auto const& e : events) {
        if (e.pressId == pressId) {
            chain.push_back(&e);
        }
    }
    if (chain.empty()) {
        err = "no-events-for-pressId";
        return false;
    }

    // eventId must be strictly increasing along the chain order we validate.
    for (size_t index = 1; index < chain.size(); ++index) {
        if (chain[index]->eventId <= chain[index - 1]->eventId) {
            err = "eventId-not-monotonic";
            return false;
        }
    }

    std::unordered_map<int64_t, DevDecisionEvent const*> byId;
    byId.reserve(chain.size());

    DevDecisionEvent const* press = nullptr;
    for (auto const* entry : chain) {
        if (entry->eventId <= 0) {
            err = "eventId<=0";
            return false;
        }
        if (entry->cycleId != entry->pressId) {
            err = "cycleId!=pressId";
            return false;
        }
        if (byId.count(entry->eventId)) {
            err = "duplicate-eventId";
            return false;
        }
        byId[entry->eventId] = entry;

        if (entry->kind == DevEventKind::PressObserved) {
            if (press) {
                err = "duplicate-pressObserved";
                return false;
            }
            if (entry->parentEventId != 0) {
                err = "pressObserved-parent-not-0";
                return false;
            }
            press = entry;
        } else {
            if (entry->parentEventId <= 0) {
                err = "missing-parentEventId";
                return false;
            }
            auto it = byId.find(entry->parentEventId);
            if (it == byId.end()) {
                // Parent must already appear earlier in the same press chain.
                err = "parent-not-in-chain";
                return false;
            }
            if (it->second->pressId != pressId) {
                err = "parent-pressId-mismatch";
                return false;
            }
        }
    }

    if (!press) {
        err = "missing-pressObserved";
        return false;
    }

    // Required terminal shape for a Delay arm cycle: … → fire → outcome
    // Gate-only cycles: press → gatePass → outcome (parent = gatePass)
    bool hasFire = false;
    bool hasOutcome = false;
    bool hasGate = false;
    bool hasPlan = false;
    bool hasArm = false;
    bool hasPassThrough = false;
    for (auto const* entry : chain) {
        switch (entry->kind) {
        case DevEventKind::GatePass:
            hasGate = true;
            break;
        case DevEventKind::PlanFinished:
            hasPlan = true;
            break;
        case DevEventKind::DelayArm:
            hasArm = true;
            break;
        case DevEventKind::PassThrough:
            hasPassThrough = true;
            break;
        case DevEventKind::Fire:
            hasFire = true;
            break;
        case DevEventKind::Outcome:
            hasOutcome = true;
            break;
        default:
            break;
        }
        if (entry->kind != DevEventKind::PressObserved && entry->attemptId != press->attemptId) {
            err = "attemptId-mismatch";
            return false;
        }
    }

    if (!hasOutcome) {
        err = "missing-outcome";
        return false;
    }

    if (hasArm) {
        if (!hasPlan) {
            err = "delayArm-without-planFinished";
            return false;
        }
        if (!hasFire) {
            err = "delayArm-without-fire";
            return false;
        }
    }

    if (hasGate && hasPlan) {
        err = "gatePass-and-planFinished";
        return false;
    }

    if (hasPassThrough && hasArm) {
        err = "passThrough-and-delayArm";
        return false;
    }

    // Outcome parent must be fire, passThrough, or gatePass.
    for (auto const* entry : chain) {
        if (entry->kind != DevEventKind::Outcome)
            continue;
        auto it = byId.find(entry->parentEventId);
        if (it == byId.end()) {
            err = "outcome-parent-missing";
            return false;
        }
        auto const pk = it->second->kind;
        if (pk != DevEventKind::Fire && pk != DevEventKind::PassThrough &&
            pk != DevEventKind::GatePass) {
            err = "outcome-parent-kind";
            return false;
        }
    }

    // Fire parent must be delayArm, recheck, or hus.
    for (auto const* entry : chain) {
        if (entry->kind != DevEventKind::Fire)
            continue;
        auto it = byId.find(entry->parentEventId);
        if (it == byId.end()) {
            err = "fire-parent-missing";
            return false;
        }
        auto const pk = it->second->kind;
        if (pk != DevEventKind::DelayArm && pk != DevEventKind::Recheck &&
            pk != DevEventKind::Hus) {
            err = "fire-parent-kind";
            return false;
        }
    }

    return true;
}

DevDecisionEvent DevTraceValidation::makeEv(DevEventKind kind, int64_t eventId, int64_t attemptId,
                                            int64_t pressId, int64_t parent, int64_t planId) {
    DevDecisionEvent e;
    e.kind = kind;
    e.eventId = eventId;
    e.attemptId = attemptId;
    e.pressId = pressId;
    e.cycleId = pressId;
    e.parentEventId = parent;
    e.planId = planId;
    e.down = true;
    e.source = DevEventSource::Live;
    return e;
}

bool DevTraceValidation::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    expect(std::strcmp(DevTraceStore::kindId(DevEventKind::PressObserved), "pressObserved") == 0,
           "kind-press");
    expect(std::strcmp(DevTraceStore::kindId(DevEventKind::Outcome), "outcome") == 0,
           "kind-outcome");
    DevEventKind offset = DevEventKind::PressObserved;
    expect(DevTraceStore::parseKind("delayArm", offset) && offset == DevEventKind::DelayArm,
           "parse-delayArm");
    expect(!DevTraceStore::parseKind("nope", offset), "parse-kind-fail");

    // Valid Delay chain: press → plan → arm → fire → outcome
    std::vector<DevDecisionEvent> good;
    good.push_back(
        DevTraceStoreImplementation::makeEv(DevEventKind::PressObserved, 1, 1, 10, 0, 0));
    good.push_back(
        DevTraceStoreImplementation::makeEv(DevEventKind::PlanFinished, 2, 1, 10, 1, 42));
    good.push_back(DevTraceStoreImplementation::makeEv(DevEventKind::DelayArm, 3, 1, 10, 2, 42));
    good.push_back(DevTraceStoreImplementation::makeEv(DevEventKind::Fire, 4, 1, 10, 3, 42));
    good.push_back(DevTraceStoreImplementation::makeEv(DevEventKind::Outcome, 5, 1, 10, 4, 42));
    std::string err;
    expect(DevTraceStore::validatePressChain(good, 10, err), "chain-good");

    // Missing parentEventId on fire
    auto badParent = good;
    badParent[3].parentEventId = 0;
    expect(!DevTraceStore::validatePressChain(badParent, 10, err), "chain-missing-parent");
    expect(err == "missing-parentEventId", "chain-missing-parent-msg");

    // Parent not in chain
    auto badRef = good;
    badRef[3].parentEventId = 99;
    expect(!DevTraceStore::validatePressChain(badRef, 10, err), "chain-parent-missing");
    expect(err == "parent-not-in-chain", "chain-parent-missing-msg");

    // Gate cycle: press → gatePass → outcome
    std::vector<DevDecisionEvent> gate;
    gate.push_back(DevTraceStoreImplementation::makeEv(DevEventKind::PressObserved, 1, 1, 7, 0, 0));
    gate.push_back(DevTraceStoreImplementation::makeEv(DevEventKind::GatePass, 2, 1, 7, 1, 0));
    gate.push_back(DevTraceStoreImplementation::makeEv(DevEventKind::Outcome, 3, 1, 7, 2, 0));
    expect(DevTraceStore::validatePressChain(gate, 7, err), "chain-gate");

    // JSON roundtrip preserves IDs
    DevSessionFile sess;
    sess.traceSchema = DevTraceStore::kTraceSchema;
    sess.modVersion = "v1.3.2-test";
    sess.sessionId = "selfcheck";
    sess.recordingsTarget = 10;
    sess.recordingsDone = 1;
    DevAttempt att;
    att.attemptId = 1;
    att.startTick = 0;
    att.endReason = "death";
    att.sealed = true;
    att.events = good;
    sess.attempts.push_back(att);

    auto body = DevTraceStore::sessionToJsonString(sess);
    expect(!body.empty(), "json-nonempty");
    DevSessionFile back;
    std::string jerr;
    expect(DevTraceStore::sessionFromJsonString(body, back, jerr), "json-parse-ok");
    expect(back.traceSchema == DevTraceStore::kTraceSchema, "json-schema");
    expect(back.sessionId == "selfcheck", "json-sessionId");
    expect(back.attempts.size() == 1, "json-attempts");
    expect(back.attempts[0].events.size() == 5, "json-events");
    expect(back.attempts[0].events[0].pressId == 10, "json-pressId");
    expect(back.attempts[0].events[1].planId == 42, "json-planId");
    expect(back.attempts[0].events[3].parentEventId == 3, "json-parent");
    expect(DevTraceStore::validatePressChain(back.attempts[0].events, 10, err), "json-chain");

    // Store API: begin/append/seal/flush (disk) when Mod is available.
    // Constrained devices (Android/iOS) skip disk gold — PlatformLimits::allowLoadDiskSelfCheck.
    if (PlatformLimits::get().allowLoadDiskSelfCheck()) {
        auto& store = DevTraceStore::get();
        bool const wasOpen = store.isOpen();
        DevSessionFile saved = store.session();
        expect(store.beginSession("selfcheck-api", 3), "store-begin");
        store.setModVersion("v1.3.2-test");
        store.setLevelMeta("lvl", "Level", false, true, "fp");
        expect(store.beginAttempt(100), "store-attempt");
        int64_t const pid = store.allocPressId();
        expect(pid > 0, "store-pressId");

        DevDecisionEvent e1 =
            DevTraceStoreImplementation::makeEv(DevEventKind::PressObserved, 0, 0, pid, 0, 0);
        e1.pressTick = 100;
        expect(store.appendEvent(e1) != nullptr, "store-append-press");
        auto* pPlan = store.appendEvent(
            DevTraceStoreImplementation::makeEv(DevEventKind::PlanFinished, 0, 0, pid, 0, 9));
        expect(pPlan != nullptr, "store-append-plan");
        // Fix parent to press eventId after auto-assign.
        auto* cur = store.currentAttempt();
        expect(cur && cur->events.size() >= 2, "store-two-events");
        if (cur && cur->events.size() >= 2) {
            cur->events[1].parentEventId = cur->events[0].eventId;
            cur->events[1].planId = 9;
            auto* arm = store.appendEvent(DevTraceStoreImplementation::makeEv(
                DevEventKind::DelayArm, 0, 0, pid, cur->events[1].eventId, 9));
            expect(arm != nullptr, "store-append-arm");
            auto* fire = store.appendEvent(DevTraceStoreImplementation::makeEv(
                DevEventKind::Fire, 0, 0, pid, arm->eventId, 9));
            expect(fire != nullptr, "store-append-fire");
            auto* output = store.appendEvent(DevTraceStoreImplementation::makeEv(
                DevEventKind::Outcome, 0, 0, pid, fire->eventId, 9));
            expect(output != nullptr, "store-append-outcome");
            expect(DevTraceStore::validatePressChain(cur->events, pid, err), "store-chain");
        }
        expect(store.sealAttempt("death", true), "store-seal");
        expect(store.session().recordingsDone == 1, "store-count");

        // Reload from disk
        auto path = store.sessionPath();
        expect(std::filesystem::exists(path), "store-file-exists");
        std::ifstream in(path);
        expect(static_cast<bool>(in), "store-file-open");
        std::string diskBody((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
        DevSessionFile diskSess;
        expect(DevTraceStore::sessionFromJsonString(diskBody, diskSess, jerr), "store-disk-parse");
        expect(diskSess.attempts.size() == 1, "store-disk-attempt");
        if (!diskSess.attempts.empty()) {
            expect(DevTraceStore::validatePressChain(diskSess.attempts[0].events, pid, err),
                   "store-disk-chain");
        }

        // Cleanup selfcheck file; restore previous open state lightly.
        store.closeSession();
        std::error_code ec;
        std::filesystem::remove(path, ec);
        if (wasOpen) {
            // Do not resurrect prior session contents; callers must reopen.
            (void)saved;
        }
    }

    if (fails == 0) {
        report = "DevTraceStore selfCheck OK";
    } else {
        report = fmt::format("DevTraceStore {} fails: {}", fails, report);
    }
    return fails == 0;
}
