#ifndef EARLYBUFFER_CORE_ASSISTSPIKEWATCH_HPP
#define EARLYBUFFER_CORE_ASSISTSPIKEWATCH_HPP

#include <cstdint>
#include <string>

// Last Live-search section breakdown for overlay / logs.
// Always-on chrono (cheap); also opens PerfTrace scopes when PerfTrace is on.
// Does not change assist budgets or search quality.
class AssistSpikeWatch {
public:
    static constexpr int kMaxSections = 12;
    static constexpr int kOverlayTop = 3;

    static AssistSpikeWatch& get();

    // Surround one Live search (press / release / HUS replan).
    void beginSearch(char const* kind);
    void endSearch(int totalUs, bool budgetHit);

    bool recording() const { return this->m_recording; }

    // Accumulate named section time (nested Scopes call this on close).
    void addSectionUs(char const* name, int64_t us);

    // Top sections from the most recent finished search.
    std::string overlayBit() const;

    char const* lastKind() const { return this->m_lastKind; }
    int lastTotalUs() const { return this->m_lastTotalUs; }
    bool lastBudgetHit() const { return this->m_lastBudgetHit; }

    class Scope {
    public:
        explicit Scope(char const* name);
        ~Scope();
        Scope(Scope const&) = delete;
        Scope& operator=(Scope const&) = delete;

    private:
        char const* m_name = nullptr;
        int64_t m_startUs = 0;
        bool m_active = false;
        bool m_perf = false;
    };

    static bool selfCheck(std::string& report);

private:
    AssistSpikeWatch() = default;

    struct Section {
        char const* name = nullptr;
        int64_t us = 0;
    };

    static int64_t nowUs();

    bool m_recording = false;
    char const* m_kind = nullptr;
    Section m_cur[kMaxSections]{};
    int m_curCount = 0;

    char const* m_lastKind = nullptr;
    int m_lastTotalUs = 0;
    bool m_lastBudgetHit = false;
    Section m_last[kMaxSections]{};
    int m_lastCount = 0;
};

#endif // EARLYBUFFER_CORE_ASSISTSPIKEWATCH_HPP
