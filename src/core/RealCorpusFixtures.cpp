#include "RealCorpusFixtures.hpp"
#include "AssistHorizon.hpp"
#include "Tg2BakeFixture.hpp"

#include <algorithm>
#include <fmt/format.h>
#include <string>

namespace {

// tg2 bakefail: coast into solid#42181 @f185 (from bakefail diag).
RealCorpusFixtures::Pose const kTg2Poses[] = {
    { 0, 17639.f, 165.f, true },
    { 40, 17720.f, 170.f, true },
    { 80, 17810.f, 175.f, true },
    { 120, 17900.f, 178.f, true },
    { 160, 17970.f, 176.f, false }, // release ~168 in diag
    { 168, 17990.f, 174.f, false },
    { 185, Tg2BakeFixture::kDeathX, Tg2BakeFixture::kDeathY, false },
};

// GRIEF: minimal synthetic wave crumb (level id pinned; poses are contract pins,
// not a claimed bot path). Death marks a mid-wave solid for horizon tests.
RealCorpusFixtures::Pose const kGriefPoses[] = {
    { 0, 400.f, 120.f, true },
    { 24, 470.f, 140.f, true },
    { 48, 540.f, 110.f, false },
    { 72, 610.f, 150.f, true },
    { 96, 680.f, 125.f, true },
    { 128, 760.f, 160.f, false },
};

} // namespace

RealCorpusFixtures::Trace const& RealCorpusFixtures::tg2Bakefail() {
    static Trace const t{
        "tg2-bakefail",
        Tg2BakeFixture::kLevelKeyPrefix,
        80045794,
        Tg2BakeFixture::kDeathFrame,
        Tg2BakeFixture::kDeathObjId,
        Tg2BakeFixture::kDeathObjType,
        Tg2BakeFixture::kDeathX,
        Tg2BakeFixture::kDeathY,
        kTg2Poses,
        sizeof(kTg2Poses) / sizeof(kTg2Poses[0]),
    };
    return t;
}

RealCorpusFixtures::Trace const& RealCorpusFixtures::griefMinimal() {
    static Trace const t{
        "grief-minimal",
        "id_149992717",
        149992717,
        128,
        1,   // placeholder solid-ish id for contract
        2,   // GameObjectType::Solid
        760.f,
        160.f,
        kGriefPoses,
        sizeof(kGriefPoses) / sizeof(kGriefPoses[0]),
    };
    return t;
}

size_t RealCorpusFixtures::traceCount() { return 2; }

RealCorpusFixtures::Trace const& RealCorpusFixtures::traceAt(size_t i) {
    if (i == 0) return tg2Bakefail();
    return griefMinimal();
}

bool RealCorpusFixtures::traceWellFormed(Trace const& t, std::string& detail) {
    detail.clear();
    if (!t.name || !*t.name) {
        detail = "empty-name";
        return false;
    }
    if (!t.levelKeyPrefix || !*t.levelKeyPrefix) {
        detail = "empty-key";
        return false;
    }
    if (t.levelId <= 0) {
        detail = "bad-level-id";
        return false;
    }
    if (!t.poses || t.poseCount < 2) {
        detail = "too-few-poses";
        return false;
    }
    for (size_t i = 0; i < t.poseCount; ++i) {
        if (i > 0 && t.poses[i].frame < t.poses[i - 1].frame) {
            detail = "frame-regress";
            return false;
        }
    }
    if (t.deathFrame < 0) {
        detail = "no-death";
        return false;
    }
    auto const& last = t.poses[t.poseCount - 1];
    if (last.frame != t.deathFrame) {
        detail = "death-frame-mismatch";
        return false;
    }
    float const dx = last.x - t.deathX;
    float const dy = last.y - t.deathY;
    if (dx * dx + dy * dy > 1.f) {
        detail = "death-xy-mismatch";
        return false;
    }
    return true;
}

bool RealCorpusFixtures::holdCoastLengthOk(
    int holdLen,
    int coastLen,
    int trajDrawCap,
    std::string& detail
) {
    detail.clear();
    if (trajDrawCap < 16) {
        detail = "cap-too-small";
        return false;
    }
    // Both lines must be attempted to full draw; death may shorten one.
    if (holdLen < 2 || coastLen < 2) {
        detail = "empty-path";
        return false;
    }
    if (holdLen > trajDrawCap + 2 || coastLen > trajDrawCap + 2) {
        detail = "over-cap";
        return false;
    }
    // At least one path should reach a meaningful fraction of the trust draw
    // (the other may die early — that is expected).
    int const need = std::max(8, trajDrawCap / 4);
    if (holdLen < need && coastLen < need) {
        detail = "both-too-short";
        return false;
    }
    return true;
}

bool RealCorpusFixtures::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    expect(traceCount() == 2, "trace-count");

    std::string detail;
    expect(traceWellFormed(tg2Bakefail(), detail), "tg2-wellformed");
    expect(traceWellFormed(griefMinimal(), detail), "grief-wellformed");

    auto const& tg2 = tg2Bakefail();
    expect(tg2.deathFrame == Tg2BakeFixture::kDeathFrame, "tg2-death-frame");
    expect(tg2.deathObjId == Tg2BakeFixture::kDeathObjId, "tg2-death-obj");
    expect(Tg2BakeFixture::matchesFrozenDeath(
            tg2.deathFrame, tg2.deathObjId, tg2.deathObjType
        ), "tg2-matches-fixture");

    auto const& grief = griefMinimal();
    expect(grief.levelId == 149992717, "grief-level-id");
    expect(std::string(grief.levelKeyPrefix) == "id_149992717", "grief-key");

    // Hold/coast length contract vs trust-long wave draw.
    auto const caps = AssistHorizon::resolve(GameModeKind::Wave, true, 8, 1.f);
    expect(
        holdCoastLengthOk(caps.trajDraw + 1, caps.trajDraw / 2, caps.trajDraw, detail),
        "hold-coast-ok"
    );
    expect(
        !holdCoastLengthOk(2, 2, caps.trajDraw, detail),
        "hold-coast-both-short"
    );

    // Long-path success ties AssistHorizon (platform-after-jump).
    int const minS = AssistHorizon::minSurviveTicksForSuccess(caps.scoreHorizon, true);
    expect(
        AssistHorizon::longPathSuccessOk(minS, false, false, minS),
        "long-success-alive"
    );
    expect(
        !AssistHorizon::longPathSuccessOk(minS / 2, false, false, minS),
        "long-fail-short-alive"
    );

    if (fails == 0) {
        report = "RealCorpusFixtures selfCheck OK";
        return true;
    }
    report = fmt::format("RealCorpusFixtures selfCheck {} fails: {}", fails, report);
    return false;
}
