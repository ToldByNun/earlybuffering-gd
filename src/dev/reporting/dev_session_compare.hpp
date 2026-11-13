#ifndef DEV_SESSION_COMPARE_HPP
#define DEV_SESSION_COMPARE_HPP

#include <filesystem>
#include <string>
#include <vector>

// AP11: compare last N sealed Dev Lab sessions under devtraces/.
// Newest vs baseline median of the older N−1 → gained / flat / regressed.
class EbDevSessionCompare {
public:
    static constexpr int kCompareSchema = 1;
    static constexpr int kDefaultLastN = 5;
    static constexpr int kMaxLastN = 20;

    enum class Verdict { Gained = 0, Flat, Regressed };

    class Options {
    public:
        int lastN = kDefaultLastN;
        bool sameLevelOnly = true;
        std::string levelKeyFilter; // empty → newest session's levelKey when sameLevelOnly
        std::filesystem::path tracesDir; // empty → EbDevTraceStore::tracesDir()

    private:
    };

    class MetricSample {
    public:
        std::string id;
        float value = 0.f;
        bool valid = false;
        bool higherIsBetter = true;

    private:
    };

    class SessionMetrics {
    public:
        std::string sessionId;
        std::string levelKey;
        std::string path;
        int attemptCount = 0;
        int evaluatedAttempts = 0;

        float liveEvalMatchRate = -1.f;
        float bestSurvShare = -1.f;
        float bestEvalShare = -1.f;
        float deathAfterFireRate = -1.f;
        float medianTicksToDeath = -1.f;
        float fidelityReproducedShare = -1.f;
        float fidelityApproxShare = -1.f;
        float fidelityNotShare = -1.f;
        float gatePassRate = -1.f;
        float hopelessRate = -1.f;
        float budgetHitRate = -1.f;
        float meanChosenEndX = -1.f;
        float simNetHarmShare = -1.f;

    private:
    };

    class MetricDelta {
    public:
        std::string id;
        float newest = -1.f;
        float baseline = -1.f;
        float delta = 0.f;
        bool valid = false;
        bool higherIsBetter = true;
        Verdict verdict = Verdict::Flat;

    private:
    };

    class Result {
    public:
        bool ok = false;
        Verdict verdict = Verdict::Flat;
        std::string summaryLine;
        std::string driverMetric;
        std::vector<std::string> drivers;
        std::vector<SessionMetrics> sessions; // oldest → newest
        std::vector<MetricDelta> deltas;
        std::filesystem::path outPath;
        std::string levelKey;

    private:
    };

    static char const* verdictId(Verdict v);
    static bool parseVerdict(std::string const& s, Verdict& out);

    // Load last N session-*.json (mtime order), filter, score, write compare_<stamp>.json.
    static bool run(Options const& opts, Result& out, std::string& errMsg);

    // Metrics for one already-parsed session path (selfCheck / CLI helpers).
    static bool metricsFromFile(std::filesystem::path const& path, SessionMetrics& out,
                                std::string& errMsg);

    static std::string formatSummary(Result const& r);
    static bool selfCheck(std::string& report);

private:
    EbDevSessionCompare() = delete;
};

#endif
