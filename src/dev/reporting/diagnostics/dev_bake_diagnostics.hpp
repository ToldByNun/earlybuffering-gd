#ifndef DEV_BAKE_DIAGNOSTICS_HPP
#define DEV_BAKE_DIAGNOSTICS_HPP

#include "../../../core/replay/replay_import.hpp"
#include <string>
#include <vector>

#include "../dev_diag_dump.hpp"

class EbDevBakeDiagnostics {
public:
    static bool prepareBakeEvents(PlayLayer*& outLayer, int& outSlot, float& outDt,
                                  double& outSrcTps, double& outBakeTps,
                                  std::vector<EbReplayEvent>& outEv, std::string& outErr,
                                  int* outSubsteps, EbDevDiagDump::BakePrepStats* outStats);
    static std::string buildReplayRemap();
    static std::string buildBakeProbe(int maxJobs);
    static std::string buildBakeFrameTrace();
    static std::string buildUnknownHistogram();

private:
    EbDevBakeDiagnostics() = delete;
};

#endif
