#ifndef EARLYBUFFER_CORE_SIMTRUST_HPP
#define EARLYBUFFER_CORE_SIMTRUST_HPP

#include "AssistVerdict.hpp"

#include <string>

// Attempt + spot trust gate for PerfectAssist (Commit 1 subset).
// Later commits add TW-ready / hard-unknown flags without changing callers.
class SimTrust {
public:
    static SimTrust& get();

    void resetAttempt();
    void onLevelQuit();

    void noteParityDrift();
    void clearParityDrift();
    bool parityDrift() const { return this->m_parityDrift; }

    // True when assist may Delay-search at world X (Commit 1: parity + spot).
    bool okAt(float x) const;

    // If assist must Pass, sets reason (PassParityDrift / PassSpotUntrusted).
    bool forcePassReason(float x, AssistReason& outReason) const;

    char const* overlayBit() const;

    static bool selfCheck(std::string& report);

private:
    SimTrust() = default;

    bool m_parityDrift = false;
};

#endif // EARLYBUFFER_CORE_SIMTRUST_HPP
