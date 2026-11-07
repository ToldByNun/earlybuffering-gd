#ifndef DEV_TRACE_STORE_HPP
#define DEV_TRACE_STORE_HPP

#include "DevClockContract.hpp"
#include "DevLiveWitness.hpp"
#include "DevRejectLedger.hpp"
#include "DevStateDiff.hpp"

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

enum class DevEventSource {
    Live = 0,
    Bake
};

// One traced decision step. Commit 3 covers identity + chain fields;
// later commits fill clock/witness/eval payloads on the same struct.
struct DevDecisionEvent {
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
    DevClockContract::Stamp clockStamp{};

    // Commit 7: live witness at plan time (no restore yet).
    bool hasWitness = false;
    DevLiveWitness witness{};

    // Commit 8: fidelity / claims (witness-gate at plan; full classify post-restore).
    bool hasFidelity = false;
    DevFidelity fidelity = DevFidelity::NotReproducible;
    bool claimsAllowed = false;
    bool approx = false;
    std::string evalSuppressedReason;
    std::vector<DevDiffEntry> diffs;

    // Commit 12: reject ledger + attribution on planFinished (gate-pass: empty notes).
    std::vector<DevRejectLedger::CandNote> candNotes;
    bool candNotesTruncated = false;
    std::vector<DevRejectLedger::AttributionNote> attribution;
};

struct DevAttempt {
    int64_t attemptId = 0;
    int startTick = 0;
    std::string endReason; // death|complete|quit|manual|empty
    bool sealed = false;
    // True when seal counted toward the rolling window (pressObserved / empty-count).
    bool counted = false;
    std::vector<DevDecisionEvent> events;
};

struct DevSessionFile {
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
    std::vector<DevAttempt> attempts;
};

// In-memory + disk JSON for Dev Lab sessions. Atomic flush = temp + rename.
class DevTraceStore {
public:
    static constexpr int kTraceSchema = 1;

    static DevTraceStore& get();

    static char const* kindId(DevEventKind kind);
    static bool parseKind(std::string const& s, DevEventKind& out);
    static char const* sourceId(DevEventSource source);
    static bool parseSource(std::string const& s, DevEventSource& out);

    // Events for one pressId must form a parent chain (press parent=0).
    // Returns false and fills err on break / missing parent / id mismatch.
    static bool validatePressChain(
        std::vector<DevDecisionEvent> const& events,
        int64_t pressId,
        std::string& err
    );

    // Serialize / parse without touching disk (selfCheck + later export).
    static std::string sessionToJsonString(DevSessionFile const& session);
    static bool sessionFromJsonString(
        std::string const& body, DevSessionFile& out, std::string& err
    );

    static bool selfCheck(std::string& report);

    bool isOpen() const;
    bool hasOpenAttempt() const;

    std::filesystem::path tracesDir() const;
    std::filesystem::path sessionPath() const;

    // New session (closes previous without counting). sessionId empty → generated.
    bool beginSession(std::string sessionId, int recordingsTarget);
    void setLevelMeta(
        std::string levelKey,
        std::string levelName,
        bool startPos,
        bool practice,
        std::string settingsFingerprint
    );
    void setModVersion(std::string modVersion);

    // Compact portable settings summary for session JSON (never empty when called).
    static std::string makeSettingsFingerprint();

    // New attempt under the open session. Fails if none open or previous unsealed.
    bool beginAttempt(int startTick);

    int64_t allocPressId();
    // Appends event; assigns eventId/attemptId if zero; cycleId defaults to pressId.
    DevDecisionEvent* appendEvent(DevDecisionEvent event);

    // Seal current attempt, bump recordingsDone when it counts, rolling-trim, flush.
    // Never auto-stops the session — keeps only the last recordingsTarget counted attempts.
    bool sealAttempt(std::string endReason, bool countTowardTarget);
    bool flush();
    void closeSession();

    // Drop oldest counted sealed attempts until recordingsDone <= recordingsTarget.
    // Also drops sealed attempts that fall before the retained window.
    void trimRollingWindow();

    DevSessionFile const& session() const { return this->m_session; }
    DevAttempt* currentAttempt();
    DevAttempt const* currentAttempt() const;

private:
    DevTraceStore() = default;

    int64_t nextEventId();
    bool writeAtomic(matjson::Value const& root) const;

    DevSessionFile m_session{};
    bool m_open = false;
    int64_t m_nextPressId = 1;
    int64_t m_nextEventId = 1;
};

#endif // DEV_TRACE_STORE_HPP
