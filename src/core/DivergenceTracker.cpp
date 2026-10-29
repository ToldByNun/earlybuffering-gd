#include "DivergenceTracker.hpp"
#include <fmt/format.h>

DivergenceTracker& DivergenceTracker::get() {
    static DivergenceTracker inst;
    return inst;
}

void DivergenceTracker::note(bool planDelay, bool died, bool expectOrb, bool gotOrb) {
    // Bad: delayed assist still died, or orb expected but missed (death, or we
    // delayed for the orb and never hit it).
    bool bad = false;
    if (planDelay && died) bad = true;
    if (expectOrb && !gotOrb && (died || planDelay)) bad = true;
    m_bad[m_i] = bad;
    m_i = (m_i + 1) % N;
    if (m_count < N) ++m_count;
    if (mismatchRate() >= 0.45f && m_count >= 12) m_untrusted = true;
}

float DivergenceTracker::mismatchRate() const {
    if (m_count <= 0) return 0.f;
    int b = 0;
    for (int i = 0; i < m_count; ++i) if (m_bad[i]) ++b;
    return static_cast<float>(b) / static_cast<float>(m_count);
}

bool DivergenceTracker::shouldWarn() const {
    return mismatchRate() >= 0.35f && m_count >= 8;
}

void DivergenceTracker::clear() {
    m_i = 0; m_count = 0; m_untrusted = false;
}

std::string DivergenceTracker::overlayBit() const {
    if (!shouldWarn() && !m_untrusted) return {};
    return fmt::format(
        "{}div{:.0f}%",
        m_untrusted ? "UNTRUST " : "WARN ",
        mismatchRate() * 100.f
    );
}

bool DivergenceTracker::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    auto& d = get();
    d.clear();
    expect(!d.simUntrusted(), "fresh-clear");
    expect(d.mismatchRate() == 0.f, "fresh-rate");
    expect(!d.shouldWarn(), "fresh-warn");

    // 12 delayed deaths → UNTRUST (≥45% / n≥12).
    for (int i = 0; i < 12; ++i) {
        d.note(true, true, false, false);
    }
    expect(d.simUntrusted(), "untrust-after-12");
    expect(d.shouldWarn(), "warn-after-12");

    d.clear();
    expect(!d.simUntrusted(), "clear-resets-untrust");
    expect(d.mismatchRate() == 0.f, "clear-resets-rate");
    expect(!d.shouldWarn(), "clear-resets-warn");

    if (fails == 0) {
        report = "DivergenceTracker selfCheck OK";
        return true;
    }
    report = fmt::format("DivergenceTracker {} fails: {}", fails, report);
    return false;
}