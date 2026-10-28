#include "PerfTrace.hpp"
#include "AssistLog.hpp"
#include "../config/StageConfig.hpp"

#include <Geode/Geode.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <fstream>

using namespace geode::prelude;

PerfTrace& PerfTrace::get() {
    static PerfTrace inst;
    return inst;
}

PerfTrace::Scope::Scope(char const* name) {
    this->m_name = name;
    if (!PerfTrace::get().enabled()) return;
    this->m_active = true;
    PerfTrace::get().begin(name);
}

PerfTrace::Scope::~Scope() {
    if (!this->m_active) return;
    PerfTrace::get().end(this->m_name);
}

bool PerfTrace::enabled() const {
    return this->m_enabled;
}

void PerfTrace::setEnabled(bool on) {
    if (this->m_enabled == on) return;
    this->m_enabled = on;
    if (on) {
        this->reset();
        this->appendLine(fmt::format(
            "=== PerfTrace ON spike>{}us hotSection>{}us ===",
            static_cast<long long>(kSpikeUs),
            static_cast<long long>(kHotSectionUs)
        ));
        this->flush("enable");
        AssistLog::info(fmt::format("PerfTrace ON → {}", logPath()));
        return;
    }
    this->flush("disable");
    AssistLog::info("PerfTrace OFF");
}

std::string PerfTrace::logPath() {
    auto* mod = Mod::get();
    if (!mod) return {};
    return (mod->getSaveDir() / "earlybuffer-perf.log").string();
}

void PerfTrace::reset() {
    this->m_tickOpen = false;
    this->m_ticksSinceSummary = 0;
    this->m_spikeCount = 0;
    this->m_hasLastBegin = false;
    this->m_stackDepth = 0;
    this->m_ringWrite = 0;
    this->m_ringCount = 0;
    this->m_pending.clear();
    this->m_pendingLines = 0;
    this->m_current = {};
}

void PerfTrace::writeToFile(std::string const& text) {
    if (text.empty()) return;
    auto const path = logPath();
    if (path.empty()) return;
    std::ofstream out(path, std::ios::app);
    if (!out) return;
    out << text;
    if (!text.empty() && text.back() != '\n') out << '\n';
}

void PerfTrace::appendLine(std::string const& line) {
    this->m_pending += line;
    this->m_pending.push_back('\n');
    ++this->m_pendingLines;
    // Bound memory if someone leaves verbose mode running for hours.
    if (this->m_pendingLines >= 400) {
        this->writeToFile(this->m_pending);
        this->m_pending.clear();
        this->m_pendingLines = 0;
    }
}

void PerfTrace::flush(char const* reason) {
    if (!this->m_pending.empty()) {
        this->writeToFile(this->m_pending);
        this->m_pending.clear();
        this->m_pendingLines = 0;
    }
    if (reason && reason[0]) {
        this->writeToFile(fmt::format("--- flush ({}) spikes={} ---", reason, this->m_spikeCount));
    }
}

void PerfTrace::note(char const* event, std::string const& detail) {
    if (!this->m_enabled || !event) return;
    if (detail.empty()) {
        this->appendLine(fmt::format("NOTE {}", event));
        return;
    }
    this->appendLine(fmt::format("NOTE {} | {}", event, detail));
}

void PerfTrace::beginTick(int attemptTick, float dt) {
    if (!this->m_enabled) return;

    auto const now = std::chrono::steady_clock::now();
    int64_t gapUs = 0;
    if (this->m_hasLastBegin) {
        gapUs = std::chrono::duration_cast<std::chrono::microseconds>(
            now - this->m_lastBegin
        ).count();
    }
    this->m_lastBegin = now;
    this->m_tickStart = now;
    this->m_hasLastBegin = true;

    this->m_current = {};
    this->m_current.attemptTick = attemptTick;
    this->m_current.dt = dt;
    this->m_current.gapUs = gapUs;
    this->m_stackDepth = 0;
    this->m_tickOpen = true;

    // Gap much larger than physics dt → render/hitch outside our scopes.
    if (gapUs > 12000) {
        this->appendLine(fmt::format(
            "GAP tick={} gap={:.2f}ms dt={:.4f} (hitch before our work)",
            attemptTick,
            gapUs / 1000.0,
            dt
        ));
    }
}

void PerfTrace::begin(char const* section) {
    if (!this->m_enabled || !section) return;

    if (!this->m_tickOpen) {
        // Input-path plan / one-shots outside physicsTick — log on end as ORPHAN.
        this->m_orphan.name = section;
        this->m_orphan.start = std::chrono::steady_clock::now();
        this->m_orphanOpen = true;
        return;
    }

    if (this->m_stackDepth >= static_cast<int>(std::size(this->m_stack))) return;
    this->m_stack[this->m_stackDepth].name = section;
    this->m_stack[this->m_stackDepth].start = std::chrono::steady_clock::now();
    ++this->m_stackDepth;
}

void PerfTrace::end(char const* section) {
    if (!this->m_enabled || !section) return;

    if (!this->m_tickOpen) {
        if (!this->m_orphanOpen) return;
        this->m_orphanOpen = false;
        int64_t const us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - this->m_orphan.start
        ).count();
        char const* name = this->m_orphan.name ? this->m_orphan.name : section;
        auto const line = fmt::format("ORPHAN {}={:.2f}ms", name, us / 1000.0);
        this->appendLine(line);
        // Do NOT sync-flush to disk on every hot orphan — evacuateGhosts / plan
        // regularly exceed 1ms and flush() was causing multi-ms hitches on restart.
        if (us >= kHotSectionUs) {
            AssistLog::info(line);
        }
        return;
    }

    if (this->m_stackDepth <= 0) return;

    --this->m_stackDepth;
    auto const& open = this->m_stack[this->m_stackDepth];
    char const* name = section;
    if (open.name && open.name[0]) name = open.name;

    int64_t const us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - open.start
    ).count();

    if (this->m_current.sectionCount < kMaxSectionsPerTick) {
        auto& slot = this->m_current.sections[this->m_current.sectionCount++];
        slot.name = name;
        slot.us = us;
    }
}

void PerfTrace::dumpSpike(TickSample const& tick) {
    ++this->m_spikeCount;

    std::string parts;
    for (int i = 0; i < tick.sectionCount; ++i) {
        auto const& s = tick.sections[i];
        if (!s.name) continue;
        parts += fmt::format(" {}={:.2f}ms", s.name, s.us / 1000.0);
    }

    auto const line = fmt::format(
        "SPIKE #{} tick={} total={:.2f}ms gap={:.2f}ms dt={:.4f} |{}",
        this->m_spikeCount,
        tick.attemptTick,
        tick.totalUs / 1000.0,
        tick.gapUs / 1000.0,
        tick.dt,
        parts.empty() ? " (no sections)" : parts
    );
    this->appendLine(line);
    AssistLog::info(line);
    // Batch to disk — sync flush on every spike hitch (saveSound ~4ms + flush).
    if ((this->m_spikeCount % 8) == 0 || this->m_pendingLines >= 80) {
        this->flush("spike");
    }
}

void PerfTrace::dumpPeriodicSummary() {
    if (this->m_ringCount <= 0) return;

    int64_t sumTotal = 0;
    int64_t maxTotal = 0;
    int64_t sumGap = 0;
    int64_t maxGap = 0;
    int n = this->m_ringCount;

    struct Agg {
        char const* name = nullptr;
        int64_t sum = 0;
        int64_t max = 0;
        int hits = 0;
    };
    Agg aggs[kMaxSectionsPerTick]{};
    int aggCount = 0;

    for (int i = 0; i < n; ++i) {
        auto const& t = this->m_ring[i];
        sumTotal += t.totalUs;
        maxTotal = std::max(maxTotal, t.totalUs);
        sumGap += t.gapUs;
        maxGap = std::max(maxGap, t.gapUs);

        for (int s = 0; s < t.sectionCount; ++s) {
            auto const& sec = t.sections[s];
            if (!sec.name) continue;
            int found = -1;
            for (int a = 0; a < aggCount; ++a) {
                if (aggs[a].name == sec.name) { found = a; break; }
            }
            if (found < 0) {
                if (aggCount >= kMaxSectionsPerTick) continue;
                found = aggCount++;
                aggs[found].name = sec.name;
            }
            aggs[found].sum += sec.us;
            aggs[found].max = std::max(aggs[found].max, sec.us);
            ++aggs[found].hits;
        }
    }

    std::string parts;
    // Sort-ish: emit hottest max first (simple selection of top entries).
    for (int pass = 0; pass < aggCount && pass < 8; ++pass) {
        int best = -1;
        for (int a = 0; a < aggCount; ++a) {
            if (!aggs[a].name) continue;
            if (best < 0 || aggs[a].max > aggs[best].max) best = a;
        }
        if (best < 0) break;
        parts += fmt::format(
            " {} avg={:.2f}ms max={:.2f}ms n={}",
            aggs[best].name,
            (aggs[best].sum / std::max(1, aggs[best].hits)) / 1000.0,
            aggs[best].max / 1000.0,
            aggs[best].hits
        );
        aggs[best].name = nullptr;
    }

    auto const line = fmt::format(
        "SUMMARY last {} ticks: totalAvg={:.2f}ms totalMax={:.2f}ms gapAvg={:.2f}ms gapMax={:.2f}ms spikes={} |{}",
        n,
        (sumTotal / std::max(1, n)) / 1000.0,
        maxTotal / 1000.0,
        (sumGap / std::max(1, n)) / 1000.0,
        maxGap / 1000.0,
        this->m_spikeCount,
        parts.empty() ? " (none)" : parts
    );
    this->appendLine(line);
    AssistLog::info(line);
    // Summary every ~120 ticks; flush only when buffer is chunky.
    if (this->m_pendingLines >= 40) {
        this->flush("summary");
    }
}

void PerfTrace::endTick() {
    if (!this->m_enabled || !this->m_tickOpen) return;
    this->m_tickOpen = false;

    // Close any leaked scopes.
    while (this->m_stackDepth > 0) {
        this->end(this->m_stack[this->m_stackDepth - 1].name
            ? this->m_stack[this->m_stackDepth - 1].name
            : "?");
    }

    // Wall time of our whole hook (avoids double-counting nested section sums).
    int64_t const wallUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - this->m_tickStart
    ).count();
    this->m_current.totalUs = wallUs;

    this->m_ring[this->m_ringWrite] = this->m_current;
    this->m_ringWrite = (this->m_ringWrite + 1) % kRingTicks;
    if (this->m_ringCount < kRingTicks) ++this->m_ringCount;

    bool const spike = this->m_current.totalUs >= kSpikeUs;
    bool hotSection = false;
    if (!spike) {
        for (int i = 0; i < this->m_current.sectionCount; ++i) {
            if (this->m_current.sections[i].us >= kHotSectionUs) {
                hotSection = true;
                break;
            }
        }
    }
    if (spike || hotSection) {
        this->dumpSpike(this->m_current);
    }

    ++this->m_ticksSinceSummary;
    if (this->m_ticksSinceSummary >= kRingTicks) {
        this->m_ticksSinceSummary = 0;
        this->dumpPeriodicSummary();
    }
}
