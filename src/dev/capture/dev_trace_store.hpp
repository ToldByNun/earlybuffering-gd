#ifndef DEV_TRACE_STORE_HPP
#define DEV_TRACE_STORE_HPP

#include "../diagnostics/dev_resource_stats.hpp"
#include "../evaluation/dev_attempt_eval.hpp"
#include "../evaluation/dev_reject_ledger.hpp"
#include "../evaluation/dev_state_diff.hpp"
#include "dev_clock_contract.hpp"
#include "dev_live_witness.hpp"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Dev Lab event kinds for one press cycle (Vertrag D).
enum class DevEventKind {
    PressObserved = 0,
    GatePass,
    PlanFinished,
    DelayArm,
    PassThrough,
    Recheck,
    Hus,
    Fire,
    Checkpoint,
    Outcome
};

enum class DevEventSource { Live = 0, Bake };

// One traced decision step. Commit 3 covers identity + chain fields;
// later commits fill clock/witness/eval payloads on the same struct.
class EbDevDecisionEvent {
public:
    int64_t eventId = 0;
    int64_t attemptId = 0;
    int64_t pressId = 0;
    int64_t cycleId = 0;
    int64_t planId = 0;
    int64_t parentEventId = 0;
    int playerIndex = 0;
    bool down = true;
    DevEventSource source = DevEventSource::Live;
    DevEventKind kind = DevEventKind::PressObserved;

    int effectTick = 0;
    int pressTick = 0;
    int plannedDelay = 0;
    int plannedFireTick = -1;
    int actualFireTick = -1;
    std::string assistReason;
    std::string endReason; // outcome only: death|complete|quit|manual|empty

    // Assist diagnostics (DelayArm / Recheck / Fire / Outcome) — optional JSON.
    bool hasAssistDiag = false;
    bool armVerifyOk = true;
    bool budgetHitAtArm = false;
    int recheckDelta = 0;
    int holdExtendMax = -1;
    bool deathAfterFire = false;
    int ticksToDeath = -1;
    bool liveVsEvalMismatch = false;

    // Commit 6: clock stamp on planFinished (eval binds the same stamp).
    bool hasClockStamp = false;
    EbDevClockContract::Stamp clockStamp{};

    // Commit 7: live witness at plan time (no restore yet).
    bool hasWitness = false;
    EbDevLiveWitness witness{};

    // Commit 8: fidelity / claims (witness-gate at plan; full classify post-restore).
    bool hasFidelity = false;
    DevFidelity fidelity = DevFidelity::NotReproducible;
    bool claimsAllowed = false;
    bool approx = false;
    std::string evalSuppressedReason;
    std::vector<EbDevDiffEntry> diffs;

    // Commit 12: reject ledger + attribution on planFinished (gate-pass: empty notes).
    std::vector<EbDevRejectLedger::CandNote> candNotes;
    bool candNotesTruncated = false;
    std::vector<EbDevRejectLedger::AttributionNote> attribution;

    // 1.5.1 AP0: hitch / Adaptive shell diagnostics on planFinished (additive JSON).
    bool hasPlanCost = false;
    int planUs = 0;
    bool budgetHit = false;
    int candidatesTried = 0;
    bool adaptiveShell = false;
    bool adaptiveFellThrough = false;

    // Dev Lab AP4: planner-branch + resolved mode params on planFinished.
    bool hasPlannerMeta = false;
    std::string plannerPath;
    std::string gameModeKind;
    bool continuous = false;
    bool inputWindow = false;
    int modeTicks = 0;
    int modeCap = 0;
    int modeSlack = 0;
    float modeLatencyWeight = 0.f;
    bool modeAdaptivePlan = false;
    int modeAdaptiveScreenTicks = 0;
    int modeAdaptiveVerifyQuantum = 0;
    int modeAdaptiveReservePct = 0;
    int adaptiveScreenedCount = 0;
    int adaptiveSurvivors = 0;
    int adaptiveVerifyGrants = 0;
    std::vector<int> viableDelays;
    bool requireHold = false;
    int releaseAfter = -1;
    int releaseEarliest = -1;
    int releaseLatest = -1;
    bool idleSafer = false;
    bool ascentUseful = false;
    bool usedOrbWindow = false;
    bool foresightOrbPad = false;
    bool longForesightNeedSearch = false;
    bool threatZoneValid = false;
    float threatZoneStartX = 0.f;
    float threatZoneEndX = 0.f;

private:
};

class EbDevAttempt {
public:
    int64_t attemptId = 0;
    int startTick = 0;
    std::string endReason; // death|complete|quit|manual|empty
    bool sealed = false;
    // True when seal counted toward the rolling window (pressObserved / empty-count).
    bool counted = false;
    std::vector<EbDevDecisionEvent> events;
    // Offline counterfactual eval (AP3) — optional; written by Eval Last/Session/On-Death.
    EbDevAttemptEval eval{};

private:
};

class EbDevSessionFile {
public:
    int traceSchema = 1;
    std::string modVersion;
    std::string sessionId;
    std::string levelKey;
    std::string levelName;
    bool startPos = false;
    bool practice = false;
    std::string settingsFingerprint;
    int recordingsTarget = 10;
    int recordingsDone = 0;
    // Process RAM/CPU min-max over the session (Dev Lab diagnostics only).
    EbDevResourceStats::Snapshot resourceStats{};
    std::vector<EbDevAttempt> attempts;

private:
};

// In-memory + disk JSON for Dev Lab sessions. Atomic flush = temp + rename.
class EbDevTraceStore {
public:
    static constexpr int kTraceSchema = 1;

    static EbDevTraceStore& get();

    static char const* kindId(DevEventKind kind);
    static bool parseKind(std::string const& s, DevEventKind& out);
    static char const* sourceId(DevEventSource source);
    static bool parseSource(std::string const& s, DevEventSource& out);

    // Events for one pressId must form a parent chain (press parent=0).
    // Returns false and fills err on break / missing parent / id mismatch.
    static bool validatePressChain(std::vector<EbDevDecisionEvent> const& events, int64_t pressId,
                                   std::string& err);

    // Serialize / parse without touching disk (selfCheck + later export).
    static std::string sessionToJsonString(EbDevSessionFile const& session);
    static bool sessionFromJsonString(std::string const& body, EbDevSessionFile& out,
                                      std::string& err);

    static bool selfCheck(std::string& report);

    bool isOpen() const;
    bool hasOpenAttempt() const;

    std::filesystem::path tracesDir() const;
    std::filesystem::path sessionPath() const;

    // New session (closes previous without counting). sessionId empty → generated.
    bool beginSession(std::string sessionId, int recordingsTarget);
    void setLevelMeta(std::string levelKey, std::string levelName, bool startPos, bool practice,
                      std::string settingsFingerprint);
    void setModVersion(std::string modVersion);

    // Compact portable settings summary for session JSON (never empty when called).
    static std::string makeSettingsFingerprint();

    // New attempt under the open session. Fails if none open or previous unsealed.
    bool beginAttempt(int startTick);

    int64_t allocPressId();
    // Appends event; assigns eventId/attemptId if zero; cycleId defaults to pressId.
    EbDevDecisionEvent* appendEvent(EbDevDecisionEvent event);

    // Seal current attempt, bump recordingsDone when it counts, rolling-trim, flush.
    // Never auto-stops the session — keeps only the last recordingsTarget counted attempts.
    bool sealAttempt(std::string endReason, bool countTowardTarget);
    // Attach offline eval grid to an attempt and flush (AP3).
    bool sealOfflineEval(int64_t attemptId, EbDevAttemptEval eval);
    bool flush();
    void closeSession();

    // Drop oldest counted sealed attempts until recordingsDone <= recordingsTarget.
    // Also drops sealed attempts that fall before the retained window.
    void trimRollingWindow();

    EbDevSessionFile const& session() const {
        return this->m_session;
    }
    EbDevAttempt* currentAttempt();
    EbDevAttempt const* currentAttempt() const;

private:
    friend class EbServiceStore;

    friend class EbDevTraceValidation;

    friend class EbDevSessionCodec;

    EbDevTraceStore() = default;

    int64_t nextEventId();
    bool writeAtomic(matjson::Value const& root) const;

    void sampleResources();

    EbDevSessionFile m_session{};
    bool m_open = false;
    int64_t m_nextPressId = 1;
    int64_t m_nextEventId = 1;
    EbDevResourceStats m_resourceStats{};
};

#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fmt/format.h>
#include <fstream>
#include <unordered_map>
#include <utility>

class EbDevTraceStoreImplementation {
public:
    static matjson::Value clockStampToJson(EbDevClockContract::Stamp const& st);
    static bool clockStampFromJson(matjson::Value const& j, EbDevClockContract::Stamp& st,
                                   std::string& err);
    static matjson::Value playerBlobToJson(EbDevPlayerBlob const& b);
    static void playerBlobFromJson(matjson::Value const& j, EbDevPlayerBlob& b);
    static matjson::Value witnessToJson(EbDevLiveWitness const& w);
    static bool witnessFromJson(matjson::Value const& j, EbDevLiveWitness& w, std::string& err);
    static matjson::Value eventToJson(EbDevDecisionEvent const& e);
    static bool eventFromJson(matjson::Value const& j, EbDevDecisionEvent& e, std::string& err);
    static matjson::Value sessionToJsonValue(EbDevSessionFile const& session);
    static EbDevDecisionEvent makeEv(DevEventKind kind, int64_t eventId, int64_t attemptId,
                                   int64_t pressId, int64_t parent, int64_t planId);

private:
    friend class EbDevTraceValidation;

    friend class EbDevSessionCodec;

    friend class EbDevEventCodec;

    friend class EbDevSnapshotCodec;

    EbDevTraceStoreImplementation() = delete;
};

#endif
