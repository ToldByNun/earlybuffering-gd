#ifndef EARLYBUFFER_CORE_REALCORPUSFIXTURES_HPP
#define EARLYBUFFER_CORE_REALCORPUSFIXTURES_HPP

#include <cstddef>
#include <string>

// Commit 15 — minimal real-corpus traces for PlanGold (no live PlayLayer).
// GRIEF + bakefail/tg2 pose crumbs so horizon / death contracts stay pinned.
class RealCorpusFixtures {
public:
    struct Pose {
        int frame = 0;
        float x = 0.f;
        float y = 0.f;
        bool holding = false;
    };

    struct Trace {
        char const* name = "";
        char const* levelKeyPrefix = "";
        int levelId = 0;
        int deathFrame = -1;
        int deathObjId = 0;
        int deathObjType = 0; // GameObjectType
        float deathX = 0.f;
        float deathY = 0.f;
        Pose const* poses = nullptr;
        size_t poseCount = 0;
    };

    // The Golden 2 bakefail (same frozen death as Tg2BakeFixture).
    static Trace const& tg2Bakefail();

    // GRIEF (id 149992717) — minimal wave-corridor crumb (synthetic contract pin).
    static Trace const& griefMinimal();

    static size_t traceCount();
    static Trace const& traceAt(size_t i);

    // Monotone frames, death pose present, deathFrame matches last-or-marked.
    static bool traceWellFormed(Trace const& t, std::string& detail);

    // Hold vs coast length rule: both traces may differ in length; neither empty
    // when horizon > 0; success needs long survive (AssistHorizon contract).
    static bool holdCoastLengthOk(
        int holdLen,
        int coastLen,
        int trajDrawCap,
        std::string& detail
    );

    static bool selfCheck(std::string& report);

private:
    RealCorpusFixtures() = delete;
};

#endif // EARLYBUFFER_CORE_REALCORPUSFIXTURES_HPP
