#include "AssistSpikeWatch.hpp"
#include "../debug/PerfTrace.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fmt/format.h>

AssistSpikeWatch& AssistSpikeWatch::get() {
    static AssistSpikeWatch inst;
    return inst;
}

int64_t AssistSpikeWatch::nowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

void AssistSpikeWatch::beginSearch(char const* kind) {
    this->m_recording = true;
    this->m_kind = kind && kind[0] ? kind : "search";
    this->m_curCount = 0;
    for (auto& s : this->m_cur) s = {};
}

void AssistSpikeWatch::endSearch(int totalUs, bool budgetHit) {
    this->m_lastKind = this->m_kind ? this->m_kind : "search";
    this->m_lastTotalUs = std::max(0, totalUs);
    this->m_lastBudgetHit = budgetHit;
    this->m_lastCount = this->m_curCount;
    for (int i = 0; i < this->m_curCount; ++i) {
        this->m_last[i] = this->m_cur[i];
    }
    for (int i = this->m_curCount; i < kMaxSections; ++i) {
        this->m_last[i] = {};
    }
    this->m_recording = false;
    this->m_kind = nullptr;
    this->m_curCount = 0;
}

void AssistSpikeWatch::addSectionUs(char const* name, int64_t us) {
    if (!name || name[0] == '\0' || us <= 0) return;
    if (!this->m_recording) return;

    for (int i = 0; i < this->m_curCount; ++i) {
        if (this->m_cur[i].name && std::strcmp(this->m_cur[i].name, name) == 0) {
            this->m_cur[i].us += us;
            return;
        }
    }
    if (this->m_curCount >= kMaxSections) return;
    this->m_cur[this->m_curCount].name = name;
    this->m_cur[this->m_curCount].us = us;
    ++this->m_curCount;
}

std::string AssistSpikeWatch::overlayBit() const {
    if (this->m_lastTotalUs <= 0 && this->m_lastCount <= 0) return {};

    // Pick top-K by us without mutating last[].
    int order[kMaxSections]{};
    int n = this->m_lastCount;
    for (int i = 0; i < n; ++i) order[i] = i;
    for (int a = 0; a < n; ++a) {
        for (int b = a + 1; b < n; ++b) {
            if (this->m_last[order[b]].us > this->m_last[order[a]].us) {
                std::swap(order[a], order[b]);
            }
        }
    }

    std::string parts;
    int shown = 0;
    for (int i = 0; i < n && shown < kOverlayTop; ++i) {
        auto const& s = this->m_last[order[i]];
        if (!s.name || s.us <= 0) continue;
        if (!parts.empty()) parts += ' ';
        parts += fmt::format("{}={:.1f}", s.name, s.us / 1000.0);
        ++shown;
    }

    char const* kind = this->m_lastKind ? this->m_lastKind : "?";
    if (parts.empty()) {
        return fmt::format(
            "last {} {:.1f}ms{}",
            kind,
            this->m_lastTotalUs / 1000.0,
            this->m_lastBudgetHit ? " budg" : ""
        );
    }
    return fmt::format(
        "last {} {:.1f}ms [{}]{}",
        kind,
        this->m_lastTotalUs / 1000.0,
        parts,
        this->m_lastBudgetHit ? " budg" : ""
    );
}

AssistSpikeWatch::Scope::Scope(char const* name) {
    this->m_name = name;
    if (!name || name[0] == '\0') return;
    this->m_active = true;
    this->m_startUs = AssistSpikeWatch::nowUs();
    if (PerfTrace::get().enabled()) {
        PerfTrace::get().begin(name);
        this->m_perf = true;
    }
}

AssistSpikeWatch::Scope::~Scope() {
    if (!this->m_active) return;
    int64_t const us = AssistSpikeWatch::nowUs() - this->m_startUs;
    AssistSpikeWatch::get().addSectionUs(this->m_name, us);
    if (this->m_perf) {
        PerfTrace::get().end(this->m_name);
    }
}

bool AssistSpikeWatch::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    AssistSpikeWatch& w = AssistSpikeWatch::get();
    // Isolate fixture from live state.
    w.m_recording = false;
    w.m_curCount = 0;
    w.m_lastCount = 0;
    w.m_lastTotalUs = 0;
    w.m_lastKind = nullptr;

    expect(w.overlayBit().empty(), "empty-overlay");

    w.beginSearch("press");
    expect(w.recording(), "recording");
    w.addSectionUs("plan.lf", 400);
    w.addSectionUs("plan.cands", 900);
    w.addSectionUs("plan.lf", 100); // accumulate
    w.addSectionUs("plan.holdProbe", 200);
    w.endSearch(1600, true);

    expect(!w.recording(), "not-recording");
    expect(w.lastTotalUs() == 1600, "total");
    expect(w.lastBudgetHit(), "budget");
    expect(std::strcmp(w.lastKind(), "press") == 0, "kind");

    // After end, cur must have been copied: lf=500, cands=900, hold=200.
    expect(w.m_lastCount == 3, "last-n");
    auto bit = w.overlayBit();
    expect(!bit.empty(), "bit-nonempty");
    expect(bit.find("press") != std::string::npos, "bit-kind");
    expect(bit.find("plan.cands") != std::string::npos, "bit-top-cands");
    expect(bit.find("budg") != std::string::npos, "bit-budg");

    // Nested Scope while recording.
    w.beginSearch("hus");
    {
        AssistSpikeWatch::Scope const a("plan.lf");
        AssistSpikeWatch::Scope const b("plan.cands");
        (void)a;
        (void)b;
    }
    expect(w.m_curCount >= 1, "scope-noted");
    w.endSearch(50, false);
    expect(!w.lastBudgetHit(), "hus-no-budg");

    if (fails == 0) {
        report = "AssistSpikeWatch selfCheck OK";
        return true;
    }
    report = fmt::format("AssistSpikeWatch selfCheck {} fails: {}", fails, report);
    return false;
}
