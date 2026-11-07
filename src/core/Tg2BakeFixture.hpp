#ifndef EARLYBUFFER_CORE_TG2BAKEFIXTURE_HPP
#define EARLYBUFFER_CORE_TG2BAKEFIXTURE_HPP

#include <string>

// Frozen bakefail corpus for The Golden 2 startpos / tg2.gdr2 (Commit 6).
// Live PlayLayer not required — contracts + residual diag format + portal Include.
class Tg2BakeFixture {
public:
    static constexpr char const* kLevelKeyPrefix = "id_80045794";
    static constexpr char const* kReplaySource = "tg2.gdr2";
    static constexpr int kDeathFrame = 185;
    static constexpr int kDeathObjId = 42181;
    static constexpr int kDeathObjType = 2; // GameObjectType::Solid
    static constexpr float kDeathX = 18001.6f;
    static constexpr float kDeathY = 177.f;

    // Matches the frozen mid-wave solid death from bakefail dumps.
    static bool matchesFrozenDeath(int deathFrame, int deathObjId, int deathObjType);

    // When TW has 0 tracks, f185 solid death is residual mover-pose failure — not portal Unknown.
    static std::string formatResidual(
        int twTracks,
        int deathFrame,
        int deathObjId,
        int deathObjType
    );

    // Bake validate err fragment: residual + parity gate token.
    static std::string enrichValidateFail(
        int twTracks,
        int deathFrame,
        int deathObjId,
        int deathObjType,
        float deathX
    );

    // All Gravity/Size/Vehicle/Teleport portals Include under bakeOffline type table.
    static bool bakeOfflinePortalsIncludeOk(std::string& detail);

    // ε LOCK still applied (Parity Gate) — Soft portal Include must not retune ε.
    static bool parityGateOk(std::string& detail);

    static bool selfCheck(std::string& report);

private:
    Tg2BakeFixture() = delete;
};

#endif // EARLYBUFFER_CORE_TG2BAKEFIXTURE_HPP
