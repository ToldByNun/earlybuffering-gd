#include "Tg2BakeFixture.hpp"
#include "AssistContracts140.hpp"
#include "GhostParityProbe.hpp"
#include "SimCollisionPolicy.hpp"

#include <Geode/Geode.hpp>
#include <cmath>
#include <cstring>
#include <fmt/format.h>

using namespace geode::prelude;

bool Tg2BakeFixture::matchesFrozenDeath(
    int deathFrame, int deathObjId, int deathObjType
) {
    return deathFrame == Tg2BakeFixture::kDeathFrame
        && deathObjId == Tg2BakeFixture::kDeathObjId
        && deathObjType == Tg2BakeFixture::kDeathObjType;
}

std::string Tg2BakeFixture::formatResidual(
    int twTracks, int deathFrame, int deathObjId, int deathObjType
) {
    if (!Tg2BakeFixture::matchesFrozenDeath(deathFrame, deathObjId, deathObjType)) {
        return {};
    }
    if (twTracks > 0) {
        return fmt::format(
            "residual=TW-tracks-present({}) death@{} solid#{} — check mover pose at corridor",
            twTracks, deathFrame, deathObjId
        );
    }
    return fmt::format(
        "residual=TW-empty death@{} solid#{} — mover pose missing (not Soft portal Unknown)",
        deathFrame, deathObjId
    );
}

std::string Tg2BakeFixture::enrichValidateFail(
    int twTracks,
    int deathFrame,
    int deathObjId,
    int deathObjType,
    float deathX
) {
    auto residual = Tg2BakeFixture::formatResidual(
        twTracks, deathFrame, deathObjId, deathObjType
    );
    std::string msg = fmt::format(
        "Macro dies in sim at frame {} (x={:.0f}) deathObjId={} "
        "twTracks={} unknownTop3=see-diag droppedP2=see-diag",
        deathFrame, deathX, deathObjId, twTracks
    );
    if (!residual.empty()) {
        msg += " — ";
        msg += residual;
    }
    msg += " — ghost path ≠ bot. Reset→pause→Bake. Diag: /diag/bakefail_*";
    msg += " parityGate=eps-locked";
    return msg;
}

bool Tg2BakeFixture::bakeOfflinePortalsIncludeOk(std::string& detail) {
    detail.clear();
    struct Row {
        char const* name;
        int type;
    };
    Row const portals[] = {
        { "grav-normal", static_cast<int>(GameObjectType::NormalGravityPortal) },
        { "grav-inv", static_cast<int>(GameObjectType::InverseGravityPortal) },
        { "grav-toggle", static_cast<int>(GameObjectType::GravityTogglePortal) },
        { "size-reg", static_cast<int>(GameObjectType::RegularSizePortal) },
        { "size-mini", static_cast<int>(GameObjectType::MiniSizePortal) },
        { "ship", static_cast<int>(GameObjectType::ShipPortal) },
        { "cube", static_cast<int>(GameObjectType::CubePortal) },
        { "ball", static_cast<int>(GameObjectType::BallPortal) },
        { "ufo", static_cast<int>(GameObjectType::UfoPortal) },
        { "wave", static_cast<int>(GameObjectType::WavePortal) },
        { "robot", static_cast<int>(GameObjectType::RobotPortal) },
        { "spider", static_cast<int>(GameObjectType::SpiderPortal) },
        { "swing", static_cast<int>(GameObjectType::SwingPortal) },
        { "tele", static_cast<int>(GameObjectType::TeleportPortal) },
        { "tele-orb", static_cast<int>(GameObjectType::TeleportOrb) },
        { "mirror", static_cast<int>(GameObjectType::NormalMirrorPortal) },
        { "mirror-inv", static_cast<int>(GameObjectType::InverseMirrorPortal) },
    };
    for (auto const& row : portals) {
        if (SimCollisionPolicy::classifyObjectType(row.type)
            != SimObjectAction::Include) {
            detail = row.name;
            return false;
        }
    }
    // Hard dual still Unknown — bake must not Soft-Continue dual.
    if (SimCollisionPolicy::classifyObjectType(
            static_cast<int>(GameObjectType::DualPortal)
        ) != SimObjectAction::UnknownOnOverlap) {
        detail = "dual-must-unk";
        return false;
    }
    if (SimCollisionPolicy::classifyObjectType(
            static_cast<int>(GameObjectType::SoloPortal)
        ) != SimObjectAction::UnknownOnOverlap) {
        detail = "solo-must-unk";
        return false;
    }
    return true;
}

bool Tg2BakeFixture::parityGateOk(std::string& detail) {
    detail.clear();
    if (AssistContracts140::kLockedEpsX != GhostParityProbe::kDefaultEpsX
        || AssistContracts140::kLockedEpsY != GhostParityProbe::kDefaultEpsY) {
        detail = "eps-lock-mismatch-defaults";
        return false;
    }
    AssistContracts140::applyLockedEps();
    if (std::fabs(
            GhostParityProbe::get().epsX() - AssistContracts140::kLockedEpsX
        ) > 1e-6f) {
        detail = "eps-x-not-locked";
        return false;
    }
    if (std::fabs(
            GhostParityProbe::get().epsY() - AssistContracts140::kLockedEpsY
        ) > 1e-6f) {
        detail = "eps-y-not-locked";
        return false;
    }
    return true;
}

bool Tg2BakeFixture::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    expect(
        Tg2BakeFixture::matchesFrozenDeath(185, 42181, 2),
        "match-frozen"
    );
    expect(
        !Tg2BakeFixture::matchesFrozenDeath(184, 42181, 2),
        "no-match-frame"
    );
    expect(
        !Tg2BakeFixture::matchesFrozenDeath(185, 1, 2),
        "no-match-oid"
    );

    auto const emptyRes = Tg2BakeFixture::formatResidual(0, 185, 42181, 2);
    expect(
        emptyRes.find("residual=TW-empty") != std::string::npos,
        "residual-empty"
    );
    expect(
        emptyRes.find("mover pose missing") != std::string::npos,
        "residual-mover"
    );
    expect(
        emptyRes.find("portal Unknown") != std::string::npos,
        "residual-not-portal"
    );

    auto const trackRes = Tg2BakeFixture::formatResidual(12, 185, 42181, 2);
    expect(
        trackRes.find("residual=TW-tracks-present") != std::string::npos,
        "residual-tracks"
    );
    expect(Tg2BakeFixture::formatResidual(0, 10, 42181, 2).empty(), "no-residual-other");

    auto const err = Tg2BakeFixture::enrichValidateFail(0, 185, 42181, 2, 18002.f);
    expect(err.find("deathObjId=42181") != std::string::npos, "err-oid");
    expect(err.find("twTracks=0") != std::string::npos, "err-tw");
    expect(err.find("residual=TW-empty") != std::string::npos, "err-residual");
    expect(err.find("parityGate=eps-locked") != std::string::npos, "err-parity");

    std::string portalDetail;
    expect(
        Tg2BakeFixture::bakeOfflinePortalsIncludeOk(portalDetail),
        portalDetail.empty() ? "portals-include" : portalDetail.c_str()
    );

    std::string parityDetail;
    expect(
        Tg2BakeFixture::parityGateOk(parityDetail),
        parityDetail.empty() ? "parity-gate" : parityDetail.c_str()
    );

    expect(std::strstr(kLevelKeyPrefix, "80045794") != nullptr, "level-key");
    expect(std::strcmp(kReplaySource, "tg2.gdr2") == 0, "replay-src");

    if (fails == 0) {
        report = "Tg2BakeFixture selfCheck OK";
        return true;
    }
    report = fmt::format("Tg2BakeFixture selfCheck {} fails: {}", fails, report);
    return false;
}
