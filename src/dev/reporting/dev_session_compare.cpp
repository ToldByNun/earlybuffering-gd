#include "dev_session_compare.hpp"
#include "../capture/dev_trace_store.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fmt/format.h>
#include <fstream>
#include <system_error>

class EbDevSessionCompareImplementation {
public:
    static constexpr float kFlatEpsAbs = 0.02f;
    static constexpr float kFlatEpsRel = 0.05f;

    static std::string stampNow() {
        using clock = std::chrono::system_clock;
        auto const timestamp = clock::to_time_t(clock::now());
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &timestamp);
#else
        localtime_r(&timestamp, &tm);
#endif
        char buffer[32]{};
        std::strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &tm);
        return std::string(buffer);
    }

    static float medianSorted(std::vector<float>& values) {
        if (values.empty())
            return -1.f;
        std::sort(values.begin(), values.end());
        size_t const n = values.size();
        if (n % 2 == 1)
            return values[n / 2];
        return 0.5f * (values[n / 2 - 1] + values[n / 2]);
    }

    static float meanOrNeg(std::vector<float> const& values) {
        if (values.empty())
            return -1.f;
        double sum = 0.0;
        for (float v : values)
            sum += static_cast<double>(v);
        return static_cast<float>(sum / static_cast<double>(values.size()));
    }

    static float rateOrNeg(int numer, int denom) {
        if (denom <= 0)
            return -1.f;
        return static_cast<float>(numer) / static_cast<float>(denom);
    }

    static bool isHopelessReason(std::string const& reason) {
        return reason.rfind("pass-hopeless", 0) == 0;
    }

    static bool isHarmNetClass(std::string const& net) {
        return net.find("harm") != std::string::npos || net.find("worse") != std::string::npos;
    }

    static EbDevSessionCompare::SessionMetrics
    metricsFromSession(EbDevSessionFile const& session, std::filesystem::path const& path) {
        EbDevSessionCompare::SessionMetrics m;
        m.sessionId = session.sessionId;
        m.levelKey = session.levelKey;
        m.path = path.string();
        m.attemptCount = static_cast<int>(session.attempts.size());

        int matchOk = 0;
        int matchDenom = 0;
        int bestSurv = 0;
        int bestEval = 0;
        int fidRepr = 0;
        int fidApprox = 0;
        int fidNot = 0;
        int fidDenom = 0;
        int dafYes = 0;
        int dafDenom = 0;
        std::vector<float> ticksToDeath;
        int planDown = 0;
        int gatePass = 0;
        int hopeless = 0;
        int budgetHit = 0;
        std::vector<float> chosenEndX;
        int harmNet = 0;
        int netDenom = 0;

        for (auto const& att : session.attempts) {
            if (att.eval.valid) {
                ++m.evaluatedAttempts;
                ++fidDenom;
                if (att.eval.fidelity == DevFidelity::Reproduced)
                    ++fidRepr;
                else if (att.eval.fidelity == DevFidelity::PlayerOnlyApproximated)
                    ++fidApprox;
                else
                    ++fidNot;

                if (att.eval.claimsAllowed && att.eval.hasBestSurviving)
                    ++bestSurv;
                if (att.eval.claimsAllowed && att.eval.hasBestEvaluated)
                    ++bestEval;

                if (att.eval.claimsAllowed && att.eval.hasBestSurviving &&
                    att.eval.plannedDelay >= 0) {
                    ++matchDenom;
                    if (att.eval.plannedDelay == att.eval.bestSurvivingDelay)
                        ++matchOk;
                }

                if (!att.eval.simNetClass.empty()) {
                    ++netDenom;
                    if (isHarmNetClass(att.eval.simNetClass))
                        ++harmNet;
                }

                if (att.eval.plannedDelay >= 0) {
                    for (auto const& sample : att.eval.samples) {
                        if (sample.delay != att.eval.plannedDelay)
                            continue;
                        chosenEndX.push_back(sample.endX);
                        break;
                    }
                }
            }

            for (auto const& ev : att.events) {
                if (ev.kind == DevEventKind::PlanFinished && ev.down) {
                    ++planDown;
                    if (ev.budgetHit)
                        ++budgetHit;
                    if (isHopelessReason(ev.assistReason))
                        ++hopeless;
                }
                if (ev.kind == DevEventKind::GatePass && ev.down)
                    ++gatePass;
                if (ev.kind == DevEventKind::Outcome && ev.hasAssistDiag) {
                    ++dafDenom;
                    if (ev.deathAfterFire) {
                        ++dafYes;
                        if (ev.ticksToDeath >= 0)
                            ticksToDeath.push_back(static_cast<float>(ev.ticksToDeath));
                    }
                }
            }
        }

        m.liveEvalMatchRate = rateOrNeg(matchOk, matchDenom);
        m.bestSurvShare = rateOrNeg(bestSurv, m.evaluatedAttempts);
        m.bestEvalShare = rateOrNeg(bestEval, m.evaluatedAttempts);
        m.deathAfterFireRate = rateOrNeg(dafYes, dafDenom);
        m.medianTicksToDeath = medianSorted(ticksToDeath);
        m.fidelityReproducedShare = rateOrNeg(fidRepr, fidDenom);
        m.fidelityApproxShare = rateOrNeg(fidApprox, fidDenom);
        m.fidelityNotShare = rateOrNeg(fidNot, fidDenom);
        m.gatePassRate = rateOrNeg(gatePass, planDown);
        m.hopelessRate = rateOrNeg(hopeless, planDown);
        m.budgetHitRate = rateOrNeg(budgetHit, planDown);
        m.meanChosenEndX = meanOrNeg(chosenEndX);
        m.simNetHarmShare = rateOrNeg(harmNet, netDenom);
        return m;
    }

    static std::vector<std::filesystem::path>
    listSessionFilesNewestFirst(std::filesystem::path const& tracesDir) {
        std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> entries;
        std::error_code ec;
        if (!std::filesystem::is_directory(tracesDir, ec) || ec)
            return {};
        for (auto const& entry : std::filesystem::directory_iterator(tracesDir, ec)) {
            if (ec)
                break;
            if (!entry.is_regular_file(ec) || ec)
                continue;
            auto const name = entry.path().filename().string();
            if (name.rfind("session-", 0) != 0 || entry.path().extension() != ".json")
                continue;
            auto const time = entry.last_write_time(ec);
            if (ec)
                continue;
            entries.emplace_back(time, entry.path());
        }
        std::sort(entries.begin(), entries.end(),
                  [](auto const& a, auto const& b) { return a.first > b.first; });
        std::vector<std::filesystem::path> out;
        out.reserve(entries.size());
        for (auto const& e : entries)
            out.push_back(e.second);
        return out;
    }

    static EbDevSessionCompare::Verdict
    verdictForDelta(float newest, float baseline, bool higherIsBetter) {
        float const scale = std::max(std::fabs(baseline), 1.f);
        float const eps = std::max(kFlatEpsAbs, kFlatEpsRel * scale);
        float const delta = newest - baseline;
        if (std::fabs(delta) <= eps)
            return EbDevSessionCompare::Verdict::Flat;
        bool const improved = higherIsBetter ? (delta > 0.f) : (delta < 0.f);
        return improved ? EbDevSessionCompare::Verdict::Gained
                        : EbDevSessionCompare::Verdict::Regressed;
    }

    static float baselineMedian(std::vector<EbDevSessionCompare::SessionMetrics> const& older,
                                float EbDevSessionCompare::SessionMetrics::*field) {
        std::vector<float> values;
        values.reserve(older.size());
        for (auto const& s : older) {
            float const v = s.*field;
            if (v >= 0.f)
                values.push_back(v);
        }
        return medianSorted(values);
    }

    static void pushDelta(std::vector<EbDevSessionCompare::MetricDelta>& deltas, char const* id,
                          float newest, float baseline, bool higherIsBetter) {
        EbDevSessionCompare::MetricDelta d;
        d.id = id ? id : "";
        d.newest = newest;
        d.baseline = baseline;
        d.higherIsBetter = higherIsBetter;
        if (newest < 0.f || baseline < 0.f) {
            d.valid = false;
            d.verdict = EbDevSessionCompare::Verdict::Flat;
            deltas.push_back(std::move(d));
            return;
        }
        d.valid = true;
        d.delta = newest - baseline;
        d.verdict = verdictForDelta(newest, baseline, higherIsBetter);
        deltas.push_back(std::move(d));
    }

    static matjson::Value metricsToJson(EbDevSessionCompare::SessionMetrics const& m) {
        matjson::Value j = matjson::Value::object();
        j["sessionId"] = m.sessionId;
        j["levelKey"] = m.levelKey;
        j["path"] = m.path;
        j["attemptCount"] = m.attemptCount;
        j["evaluatedAttempts"] = m.evaluatedAttempts;
        auto put = [&](char const* key, float v) {
            if (v >= 0.f)
                j[key] = v;
        };
        put("liveEvalMatchRate", m.liveEvalMatchRate);
        put("bestSurvShare", m.bestSurvShare);
        put("bestEvalShare", m.bestEvalShare);
        put("deathAfterFireRate", m.deathAfterFireRate);
        put("medianTicksToDeath", m.medianTicksToDeath);
        put("fidelityReproducedShare", m.fidelityReproducedShare);
        put("fidelityApproxShare", m.fidelityApproxShare);
        put("fidelityNotShare", m.fidelityNotShare);
        put("gatePassRate", m.gatePassRate);
        put("hopelessRate", m.hopelessRate);
        put("budgetHitRate", m.budgetHitRate);
        put("meanChosenEndX", m.meanChosenEndX);
        put("simNetHarmShare", m.simNetHarmShare);
        return j;
    }

private:
    EbDevSessionCompareImplementation() = delete;
};

char const* EbDevSessionCompare::verdictId(Verdict v) {
    switch (v) {
    case Verdict::Gained:
        return "gained";
    case Verdict::Regressed:
        return "regressed";
    case Verdict::Flat:
        break;
    }
    return "flat";
}

bool EbDevSessionCompare::parseVerdict(std::string const& s, Verdict& out) {
    if (s == "gained") {
        out = Verdict::Gained;
        return true;
    }
    if (s == "regressed") {
        out = Verdict::Regressed;
        return true;
    }
    if (s == "flat") {
        out = Verdict::Flat;
        return true;
    }
    return false;
}

bool EbDevSessionCompare::metricsFromFile(std::filesystem::path const& path, SessionMetrics& out,
                                          std::string& errMsg) {
    errMsg.clear();
    out = {};
    auto const body = geode::utils::file::readString(path);
    if (!body.isOk()) {
        errMsg = body.unwrapErr();
        if (errMsg.empty())
            errMsg = "read-failed";
        return false;
    }
    EbDevSessionFile session;
    if (!EbDevTraceStore::sessionFromJsonString(body.unwrap(), session, errMsg)) {
        if (errMsg.empty())
            errMsg = "parse-failed";
        return false;
    }
    out = EbDevSessionCompareImplementation::metricsFromSession(session, path);
    return true;
}

std::string EbDevSessionCompare::formatSummary(Result const& r) {
    if (!r.ok)
        return "compare failed";
    std::string drivers;
    for (size_t i = 0; i < r.drivers.size() && i < 3; ++i) {
        if (!drivers.empty())
            drivers += ",";
        drivers += r.drivers[i];
    }
    if (drivers.empty())
        drivers = r.driverMetric.empty() ? "-" : r.driverMetric;
    return fmt::format("COMPARE {} | verdict={} | level={} | sessions={} | drivers={}",
                       r.outPath.filename().string(), verdictId(r.verdict),
                       r.levelKey.empty() ? "-" : r.levelKey,
                       static_cast<int>(r.sessions.size()), drivers);
}

bool EbDevSessionCompare::run(Options const& opts, Result& out, std::string& errMsg) {
    errMsg.clear();
    out = {};

    int lastN = opts.lastN;
    if (lastN < 2)
        lastN = 2;
    if (lastN > kMaxLastN)
        lastN = kMaxLastN;

    auto tracesDir = opts.tracesDir;
    if (tracesDir.empty())
        tracesDir = EbDevTraceStore::get().tracesDir();

    auto newestFirst = EbDevSessionCompareImplementation::listSessionFilesNewestFirst(tracesDir);
    if (newestFirst.empty()) {
        errMsg = "no session-*.json under devtraces/";
        return false;
    }

    std::string levelFilter = opts.levelKeyFilter;
    if (opts.sameLevelOnly && levelFilter.empty()) {
        SessionMetrics newestMetrics;
        std::string loadErr;
        if (metricsFromFile(newestFirst.front(), newestMetrics, loadErr) &&
            !newestMetrics.levelKey.empty()) {
            levelFilter = newestMetrics.levelKey;
        }
    }

    std::vector<SessionMetrics> selected; // newest → older
    for (auto const& path : newestFirst) {
        if (static_cast<int>(selected.size()) >= lastN)
            break;
        SessionMetrics metrics;
        std::string loadErr;
        if (!metricsFromFile(path, metrics, loadErr))
            continue;
        if (opts.sameLevelOnly && !levelFilter.empty() && metrics.levelKey != levelFilter)
            continue;
        selected.push_back(std::move(metrics));
    }

    if (static_cast<int>(selected.size()) < 2) {
        errMsg = opts.sameLevelOnly
                     ? "need ≥2 sessions for this levelKey (record more / turn off same-level)"
                     : "need ≥2 session-*.json files under devtraces/";
        return false;
    }

    // Store oldest → newest for report readability.
    out.sessions.assign(selected.rbegin(), selected.rend());
    out.levelKey = levelFilter;
    SessionMetrics const& newest = selected.front();
    std::vector<SessionMetrics> older(selected.begin() + 1, selected.end());

    auto& push = EbDevSessionCompareImplementation::pushDelta;
    auto& base = EbDevSessionCompareImplementation::baselineMedian;
    push(out.deltas, "liveEvalMatchRate", newest.liveEvalMatchRate,
         base(older, &SessionMetrics::liveEvalMatchRate), true);
    push(out.deltas, "bestSurvShare", newest.bestSurvShare,
         base(older, &SessionMetrics::bestSurvShare), true);
    push(out.deltas, "fidelityReproducedShare", newest.fidelityReproducedShare,
         base(older, &SessionMetrics::fidelityReproducedShare), true);
    push(out.deltas, "deathAfterFireRate", newest.deathAfterFireRate,
         base(older, &SessionMetrics::deathAfterFireRate), false);
    push(out.deltas, "medianTicksToDeath", newest.medianTicksToDeath,
         base(older, &SessionMetrics::medianTicksToDeath), false);
    push(out.deltas, "hopelessRate", newest.hopelessRate, base(older, &SessionMetrics::hopelessRate),
         false);
    push(out.deltas, "budgetHitRate", newest.budgetHitRate,
         base(older, &SessionMetrics::budgetHitRate), false);
    push(out.deltas, "gatePassRate", newest.gatePassRate, base(older, &SessionMetrics::gatePassRate),
         true);
    push(out.deltas, "meanChosenEndX", newest.meanChosenEndX,
         base(older, &SessionMetrics::meanChosenEndX), true);
    push(out.deltas, "simNetHarmShare", newest.simNetHarmShare,
         base(older, &SessionMetrics::simNetHarmShare), false);

    int score = 0;
    float bestMag = -1.f;
    for (auto const& d : out.deltas) {
        if (!d.valid)
            continue;
        if (d.verdict == Verdict::Gained) {
            ++score;
            out.drivers.push_back(d.id);
        } else if (d.verdict == Verdict::Regressed) {
            --score;
            out.drivers.push_back(d.id);
        }
        float const mag = std::fabs(d.delta);
        if (mag > bestMag && d.verdict != Verdict::Flat) {
            bestMag = mag;
            out.driverMetric = d.id;
        }
    }

    if (score > 0)
        out.verdict = Verdict::Gained;
    else if (score < 0)
        out.verdict = Verdict::Regressed;
    else
        out.verdict = Verdict::Flat;

    // Keep only drivers matching overall verdict (or all flats → none).
    if (out.verdict != Verdict::Flat) {
        std::vector<std::string> filtered;
        for (auto const& d : out.deltas) {
            if (d.valid && d.verdict == out.verdict)
                filtered.push_back(d.id);
        }
        out.drivers = std::move(filtered);
        if (!out.drivers.empty())
            out.driverMetric = out.drivers.front();
    } else {
        out.drivers.clear();
        out.driverMetric.clear();
    }

    matjson::Value root = matjson::Value::object();
    root["schema"] = kCompareSchema;
    root["verdict"] = verdictId(out.verdict);
    root["levelKey"] = out.levelKey;
    root["lastN"] = lastN;
    root["sameLevelOnly"] = opts.sameLevelOnly;
    root["sessionCount"] = static_cast<int>(out.sessions.size());
    if (!out.driverMetric.empty())
        root["driverMetric"] = out.driverMetric;
    matjson::Value drivers = matjson::Value::array();
    for (auto const& id : out.drivers)
        drivers.push(id);
    root["drivers"] = drivers;

    matjson::Value sessions = matjson::Value::array();
    for (auto const& s : out.sessions)
        sessions.push(EbDevSessionCompareImplementation::metricsToJson(s));
    root["sessions"] = sessions;

    matjson::Value deltas = matjson::Value::array();
    for (auto const& d : out.deltas) {
        matjson::Value dj = matjson::Value::object();
        dj["id"] = d.id;
        dj["valid"] = d.valid;
        dj["higherIsBetter"] = d.higherIsBetter;
        dj["verdict"] = verdictId(d.verdict);
        if (d.valid) {
            dj["newest"] = d.newest;
            dj["baseline"] = d.baseline;
            dj["delta"] = d.delta;
        }
        deltas.push(dj);
    }
    root["deltas"] = deltas;

    std::error_code ec;
    std::filesystem::create_directories(tracesDir, ec);
    out.outPath = tracesDir / fmt::format("compare_{}.json", EbDevSessionCompareImplementation::stampNow());
    auto const written = geode::utils::file::writeString(out.outPath, root.dump(2));
    if (!written.isOk()) {
        errMsg = written.unwrapErr();
        if (errMsg.empty())
            errMsg = "compare write failed";
        return false;
    }

    out.ok = true;
    out.summaryLine = formatSummary(out);
    return true;
}

bool EbDevSessionCompare::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok)
            return;
        ++fails;
        if (!report.empty())
            report += ", ";
        report += name;
    };

    expect(std::strcmp(verdictId(Verdict::Gained), "gained") == 0, "verdict-gained");
    expect(std::strcmp(verdictId(Verdict::Flat), "flat") == 0, "verdict-flat");
    Verdict parsed = Verdict::Flat;
    expect(parseVerdict("regressed", parsed) && parsed == Verdict::Regressed, "parse-regress");

    auto v = EbDevSessionCompareImplementation::verdictForDelta(0.80f, 0.50f, true);
    expect(v == Verdict::Gained, "delta-gain");
    v = EbDevSessionCompareImplementation::verdictForDelta(0.20f, 0.50f, true);
    expect(v == Verdict::Regressed, "delta-regress");
    v = EbDevSessionCompareImplementation::verdictForDelta(0.51f, 0.50f, true);
    expect(v == Verdict::Flat, "delta-flat");
    v = EbDevSessionCompareImplementation::verdictForDelta(0.10f, 0.40f, false);
    expect(v == Verdict::Gained, "delta-lower-better");

    if (fails == 0) {
        report = "DevSessionCompare selfCheck OK";
        return true;
    }
    report = std::string("DevSessionCompare selfCheck fails: ") + report;
    return false;
}
