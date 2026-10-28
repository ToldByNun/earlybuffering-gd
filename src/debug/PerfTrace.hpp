#ifndef EARLYBUFFER_DEBUG_PERFTRACE_HPP
#define EARLYBUFFER_DEBUG_PERFTRACE_HPP

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

// Lightweight section profiler for FPS / physics-tick spikes.
// When enabled: times named scopes, keeps a ring of recent ticks, dumps spikes
// to Geode log + save-dir file `earlybuffer-perf.log`.
class PerfTrace {
public:
    static constexpr int kMaxSectionsPerTick = 32;
    static constexpr int kRingTicks = 120;
    // Our work in one physics tick above this → SPIKE dump (microseconds).
    static constexpr int64_t kSpikeUs = 4000; // 4 ms
    // Single section above this → always noted in spike dump.
    static constexpr int64_t kHotSectionUs = 1000; // 1 ms

    class Scope {
    public:
        explicit Scope(char const* name);
        ~Scope();
        Scope(Scope const&) = delete;
        Scope& operator=(Scope const&) = delete;

    private:
        char const* m_name = nullptr;
        bool m_active = false;
    };

    static PerfTrace& get();

    void setEnabled(bool on);
    bool enabled() const;

    // Call once at the start/end of AssistController::onPhysicsTick.
    void beginTick(int attemptTick, float dt);
    void endTick();

    void begin(char const* section);
    void end(char const* section);

    // Free-form event line (always timestamped into the file buffer when enabled).
    void note(char const* event, std::string const& detail = {});

    // Force-write buffered lines + a summary (e.g. on quit / setting toggle).
    void flush(char const* reason);

    void reset();

    // Path of the log file (empty if mod unavailable).
    static std::string logPath();

private:
    struct SectionSample {
        char const* name = nullptr;
        int64_t us = 0;
    };

    struct TickSample {
        int attemptTick = 0;
        float dt = 0.f;
        int64_t gapUs = 0;     // wall time since previous beginTick
        int64_t totalUs = 0;   // sum of sections this tick
        int sectionCount = 0;
        SectionSample sections[kMaxSectionsPerTick]{};
    };

    struct OpenSection {
        char const* name = nullptr;
        std::chrono::steady_clock::time_point start{};
    };

    PerfTrace() = default;

    void appendLine(std::string const& line);
    void dumpSpike(TickSample const& tick);
    void dumpPeriodicSummary();
    void writeToFile(std::string const& text);

    bool m_enabled = false;
    bool m_tickOpen = false;
    int m_ticksSinceSummary = 0;
    int m_spikeCount = 0;

    std::chrono::steady_clock::time_point m_lastBegin{};
    std::chrono::steady_clock::time_point m_tickStart{};
    bool m_hasLastBegin = false;

    TickSample m_current{};
    OpenSection m_stack[8]{};
    int m_stackDepth = 0;
    // Sections started outside beginTick/endTick (e.g. input-thread plan).
    OpenSection m_orphan{};
    bool m_orphanOpen = false;

    TickSample m_ring[kRingTicks]{};
    int m_ringWrite = 0;
    int m_ringCount = 0;

    std::string m_pending; // coalesced file writes
    int m_pendingLines = 0;
};

#endif // EARLYBUFFER_DEBUG_PERFTRACE_HPP
