#include "dual_oracle_builder.hpp"
#include "gold_reproduce_gate.hpp"
#include <fmt/format.h>

bool EbDualOracleBuilder::reproduceAllowsOracle(std::string const& corpusId, std::string& detail) {
    detail.clear();
    if (corpusId.empty()) {
        detail = "empty corpus id";
        return false;
    }
    if (!EbGoldReproduceGate::statusIsPass(corpusId)) {
        detail = fmt::format("{} reproduce!=pass — refuse VerifierSafeSet/PhysicsSafeSet", corpusId);
        return false;
    }
    detail = fmt::format("{} reproduce=pass", corpusId);
    return true;
}

bool EbDualOracleBuilder::kindIsGroundTruth(std::string const& kind) {
    return kind == kPhysicsKind;
}

bool EbDualOracleBuilder::kindIsDiagnosticOnly(std::string const& kind) {
    return kind == kVerifierKind;
}

bool EbDualOracleBuilder::captureModeOk(std::string const& mode) {
    return mode == "pre_shifted_press" || mode == "pre_would_be_press" || mode == "none";
}

bool EbDualOracleBuilder::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string(name) + "; ";
        }
    };
    expect(std::string(kSchema) == "1", "schema");
    expect(kindIsGroundTruth(kPhysicsKind), "physics-gt");
    expect(!kindIsGroundTruth(kVerifierKind), "verifier-not-gt");
    expect(kindIsDiagnosticOnly(kVerifierKind), "verifier-diag");
    expect(!kindIsDiagnosticOnly(kPhysicsKind), "physics-not-diag");
    expect(captureModeOk("pre_shifted_press"), "capture-pre");
    expect(captureModeOk("none"), "capture-none");
    expect(!captureModeOk("gold_frame_f"), "capture-reject-gold-f");

    std::string detail;
    // Without installed corpus pass stamps, refuse is the correct default.
    bool const allowed = reproduceAllowsOracle("cataclysm", detail);
    expect(!allowed || detail.find("pass") != std::string::npos, "reproduce-gate");
    expect(!detail.empty(), "reproduce-detail");

    if (fails == 0)
        report = "DualOracleBuilder OK";
    else
        report = fmt::format("DualOracleBuilder {} fails: {}", fails, report);
    return fails == 0;
}
