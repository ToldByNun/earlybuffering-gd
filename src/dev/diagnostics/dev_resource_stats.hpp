#ifndef DEV_RESOURCE_STATS_HPP
#define DEV_RESOURCE_STATS_HPP

#include <cstdint>
#include <string>

// Session-scoped process RAM / CPU sampler for Dev Lab traces + reports.
// Cheap: Working Set + process CPU delta. Never gates assist / Pass / Fail.
class EbDevResourceStats {
public:
    class Snapshot {
    public:
        bool valid = false;
        int sampleCount = 0;
        double ramMbMin = 0.0;
        double ramMbMax = 0.0;
        double ramMbLast = 0.0;
        // Process CPU time / wall * 100 (one-core scale; can exceed 100).
        double cpuPctMin = 0.0;
        double cpuPctMax = 0.0;
        double cpuPctLast = 0.0;
        bool hasCpu = false;

    private:
    };

    void reset();
    // Take one OS sample and fold into min/max. Returns false if platform
    // cannot sample (snapshot stays invalid until a successful sample).
    bool noteSample();

    Snapshot const& snapshot() const {
        return m_snap;
    }

    // Compact bit for overlay / report file, empty when !valid.
    // Example: "ram=512.0-640.2MB cpu=8.1-95.4%"
    static std::string formatCompact(Snapshot const& snap);

    static bool selfCheck(std::string& report);

private:
    Snapshot m_snap{};
    bool m_havePrevCpu = false;
    uint64_t m_prevProc100ns = 0;
    int64_t m_prevWallNs = 0;
};

#endif
