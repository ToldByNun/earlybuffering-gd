#include "AssistSimEstimate.hpp"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>

AssistSimEstimate& AssistSimEstimate::get() {
    static AssistSimEstimate inst;
    return inst;
}

void AssistSimEstimate::resetSession() {
    this->m_prev = 0;
    this->m_caused = 0;
    this->m_neutral = 0;
    this->m_inconclusive = 0;
    this->m_evalArms = 0;
}

char const* AssistSimEstimate::netClassId(NetClass c) {
    switch (c) {
        case NetClass::Neutral: return "neutral";
        case NetClass::Prev: return "prev";
        case NetClass::Caused: return "caused";
        case NetClass::Inconclusive: return "inconclusive";
    }
    return "neutral";
}

AssistSimEstimate::NetClass AssistSimEstimate::classifyArm(
    bool d0SurvivedPrimary,
    bool d0Inconclusive,
    bool delaySurvivedPrimary,
    bool delayInconclusive,
    int plannedDelay
) {
    if (plannedDelay <= 0) return NetClass::Neutral;
    if (d0Inconclusive || delayInconclusive) return NetClass::Inconclusive;
    if (delaySurvivedPrimary && !d0SurvivedPrimary) return NetClass::Prev;
    if (!delaySurvivedPrimary && d0SurvivedPrimary) return NetClass::Caused;
    return NetClass::Neutral;
}

AssistSimEstimate::NetClass AssistSimEstimate::classifyFromSamples(
    std::vector<SampleView> const& samples,
    int plannedDelay
) {
    if (plannedDelay <= 0) return NetClass::Neutral;
    SampleView const* d0 = nullptr;
    SampleView const* arm = nullptr;
    for (auto const& s : samples) {
        if (s.delay == 0) d0 = &s;
        if (s.delay == plannedDelay) arm = &s;
    }
    if (!d0 || !arm) return NetClass::Inconclusive;
    return classifyArm(
        d0->survivedPrimary,
        d0->inconclusive,
        arm->survivedPrimary,
        arm->inconclusive,
        plannedDelay
    );
}

void AssistSimEstimate::noteEval(NetClass c, bool claimsAllowed) {
    if (!claimsAllowed) {
        ++this->m_inconclusive;
        return;
    }
    ++this->m_evalArms;
    switch (c) {
        case NetClass::Prev: ++this->m_prev; break;
        case NetClass::Caused: ++this->m_caused; break;
        case NetClass::Inconclusive: ++this->m_inconclusive; break;
        case NetClass::Neutral: ++this->m_neutral; break;
    }
}

float AssistSimEstimate::harmRateSim() const {
    int const d = std::max(1, this->m_evalArms);
    return static_cast<float>(this->m_caused) / static_cast<float>(d);
}

std::string AssistSimEstimate::reportLine() const {
    return fmt::format(
        "simEstimate Prev={} Caused={} Net={} HarmRate_sim={:.4f} "
        "evalArms={} neutral={} inconclusive={} (G10 diagnostic only)",
        this->m_prev,
        this->m_caused,
        this->net(),
        this->harmRateSim(),
        this->m_evalArms,
        this->m_neutral,
        this->m_inconclusive
    );
}

std::string AssistSimEstimate::overlayBit() const {
    if (this->m_evalArms <= 0 && this->m_inconclusive <= 0) return {};
    // net=+Prev/−Caused (simEstimate); never a ship gate (G10).
    return fmt::format(
        "simEst net=+{}/-{} harm={:.0f}%",
        this->m_prev,
        this->m_caused,
        this->harmRateSim() * 100.f
    );
}

bool AssistSimEstimate::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    expect(
        classifyArm(false, false, true, false, 4) == NetClass::Prev,
        "prev"
    );
    expect(
        classifyArm(true, false, false, false, 4) == NetClass::Caused,
        "caused"
    );
    expect(
        classifyArm(true, false, true, false, 4) == NetClass::Neutral,
        "both-survive"
    );
    expect(
        classifyArm(false, false, false, false, 4) == NetClass::Neutral,
        "both-die"
    );
    expect(
        classifyArm(false, true, true, false, 4) == NetClass::Inconclusive,
        "d0-unk"
    );
    expect(
        classifyArm(false, false, true, false, 0) == NetClass::Neutral,
        "no-arm"
    );

    std::vector<SampleView> samples = {
        {0, false, false},
        {3, true, false},
        {5, false, true}
    };
    expect(classifyFromSamples(samples, 3) == NetClass::Prev, "from-prev");
    expect(
        classifyFromSamples(samples, 5) == NetClass::Inconclusive, "from-inc"
    );
    expect(classifyFromSamples(samples, 9) == NetClass::Inconclusive, "miss");

    AssistSimEstimate& m = AssistSimEstimate::get();
    m.resetSession();
    m.noteEval(NetClass::Prev, true);
    m.noteEval(NetClass::Prev, true);
    m.noteEval(NetClass::Caused, true);
    m.noteEval(NetClass::Neutral, true);
    m.noteEval(NetClass::Prev, false); // claims denied → inconclusive bucket
    expect(m.prev() == 2 && m.caused() == 1 && m.net() == 1, "acc");
    expect(m.evalArms() == 4, "eval-arms");
    expect(m.inconclusive() == 1, "claims-deny");
    expect(std::fabs(m.harmRateSim() - 0.25f) < 1e-4f, "harm");

    auto const line = m.reportLine();
    expect(line.find("simEstimate") != std::string::npos, "tag");
    expect(line.find("G10") != std::string::npos, "g10");
    auto const bit = m.overlayBit();
    expect(bit.find("simEst") != std::string::npos, "overlay");

    // G10: never claim ship Pass/Fail tokens.
    expect(line.find("G5=") == std::string::npos, "no-ship-g5");
    expect(line.find("PASS") == std::string::npos, "no-pass");

    m.resetSession();
    expect(m.prev() == 0 && m.evalArms() == 0, "reset");

    if (fails == 0) {
        report = "AssistSimEstimate selfCheck OK";
        return true;
    }
    report = fmt::format("AssistSimEstimate selfCheck {} fails: {}", fails, report);
    return false;
}
