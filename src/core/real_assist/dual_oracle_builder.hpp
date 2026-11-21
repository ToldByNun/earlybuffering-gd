#ifndef DUAL_ORACLE_BUILDER_HPP
#define DUAL_ORACLE_BUILDER_HPP

#include <string>

// APR1: VerifierSafeSet (diagnostic) vs PhysicsSafeSet (ground truth).
// Offline Python writes pending-probe docs; in-game fills bands after state capture.
class EbDualOracleBuilder {
public:
    static constexpr char const* kSchema = "1";
    static constexpr char const* kVerifierKind = "VerifierSafeSet";
    static constexpr char const* kPhysicsKind = "PhysicsSafeSet";

    // Hard gate: oracle construction requires reproduce.json status=pass.
    static bool reproduceAllowsOracle(std::string const& corpusId, std::string& detail);

    // True only for PhysicsSafeSet claims used in acceptance / PhysicsHitRate.
    static bool kindIsGroundTruth(std::string const& kind);

    // Verifier hits are diagnostic — never acceptance.
    static bool kindIsDiagnosticOnly(std::string const& kind);

    // Capture contract: pre-shifted press after replaying prior derived events.
    static bool captureModeOk(std::string const& mode);

    static bool selfCheck(std::string& report);

private:
    EbDualOracleBuilder() = delete;
};

#endif
