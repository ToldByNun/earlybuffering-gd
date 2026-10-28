#ifndef EARLYBUFFER_DEBUG_LAGPROBE_HPP
#define EARLYBUFFER_DEBUG_LAGPROBE_HPP

#include <chrono>
#include <cstdint>
#include <string>

// Episode logger for death → reset / checkpoint hitches.
//
// Unlike PerfTrace (per-physics-tick noise), LagProbe only records discrete
// episodes we care about when hunting "I died here and FPS hitch'd":
//   destroyPlayer → onRealDeath → (later) resetLevel / loadFromCheckpoint
//
// Log file (mod save dir):
//   earlybuffer-lagprobe.log
// Full path example:
//   %LOCALAPPDATA%/GeometryDash/geode/mods/audesign.earlybuffer/earlybuffer-lagprobe.log
//
// Enable: EarlyBuffer setting "Lag Probe" (lag-probe). Default ON.
// Each enable writes a legend of every event name. Reproduce a spike, quit or
// wait for auto-flush, then paste the latest EPISODE block.
//
// Line formats:
//   EPISODE BEGIN <name> | <detail>     wall clock starts
//   +12.34ms  |indent| <event> = 1.23ms | detail
//   +12.34ms  |indent| MARK <event> | detail     (zero-width bookmark)
//   EPISODE END <name> total=45.67ms
//
// How to read a spike:
//   1. Find EPISODE BEGIN death / resetLevel near the hitch.
//   2. The line with the largest "= X.XXms" inside that episode is OUR cost.
//   3. If resetLevel's "vanilla.PlayLayer::resetLevel" dominates, the hitch is
//      GD / other mods — not EarlyBuffer ghosts.
//   4. GAP lines in earlybuffer-perf.log (if Perf Trace on) measure wall time
//      BETWEEN physics ticks (includes death anim + other mods).
class LagProbe {
public:
    class Scope {
    public:
        explicit Scope(char const* name, std::string detail = {});
        ~Scope();
        Scope(Scope const&) = delete;
        Scope& operator=(Scope const&) = delete;

    private:
        char const* m_name = nullptr;
        bool m_active = false;
    };

    static LagProbe& get();

    void setEnabled(bool on);
    bool enabled() const;
    bool episodeOpen() const { return this->m_episodeOpen; }

    static std::string logPath();

    // Open a top-level episode (auto-closes a previous open episode).
    void beginEpisode(char const* name, std::string detail = {});
    void endEpisode();

    // Nested timing (also via Scope). Safe outside episodes → no-op.
    void begin(char const* name, std::string detail = {});
    void end(char const* name);

    // Instant bookmark at current wall offset (no duration).
    void mark(char const* name, std::string detail = {});

    // Force disk write (also on episode end).
    void flush();

    void reset();

private:
    LagProbe() = default;

    void appendLine(std::string const& line);
    void writeToFile(std::string const& text);
    void writeLegend();
    double msSinceEpisode() const;
    std::string indent() const;

    bool m_enabled = false;
    bool m_episodeOpen = false;
    char const* m_episodeName = nullptr;
    std::chrono::steady_clock::time_point m_episodeStart{};

    static constexpr int kMaxDepth = 12;
    struct Frame {
        char const* name = nullptr;
        std::chrono::steady_clock::time_point start{};
    };
    Frame m_stack[kMaxDepth]{};
    int m_depth = 0;

    std::string m_pending;
    int m_pendingLines = 0;
};

#endif // EARLYBUFFER_DEBUG_LAGPROBE_HPP
