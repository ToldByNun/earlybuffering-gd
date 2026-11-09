#include "bake_window_index.hpp"
#include "../../../config/stage_config.hpp"
#include "../../../debug/assist_log.hpp"
#include "../../replay/replay_import.hpp"
#include "../../sim/shadow_simulator.hpp"
#include "../bake_map.hpp"
#include <Geode/utils/file.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

int EbBakeWindowIndex::refineSelfWindows(EbBakeMap& owner, PlayLayer* layer, int startSnapshotSlot,
                                       std::vector<EbBakedClick>& clicks, int bakeRadius,
                                       int budgetSims, std::atomic<bool>* cancel) {
    if (!layer || startSnapshotSlot < 0 || clicks.empty() || budgetSims <= 0)
        return 0;
    if (owner.isBaking())
        return 0;

    bakeRadius = EbBakeMapImplementation::resolveRadius(bakeRadius, BakeMethod::Replay);
    double tps = owner.m_data.tps > 0.0 ? owner.m_data.tps : 240.0;
    float deltaTime = static_cast<float>(1.0 / tps);
    int const horizon =
        EbShadowSimulator::modelFor(layer->m_player1 ? layer->m_player1 : layer->m_player2,
                                  EbStageConfig::ticksAll())
            .cap;

    std::stable_sort(clicks.begin(), clicks.end(), [](auto const& a, auto const& b) {
        if (a.nominalFrame != b.nominalFrame)
            return a.nominalFrame < b.nominalFrame;
        return a.player2 < b.player2;
    });

    std::vector<EbReplayEvent> timeline;
    timeline.reserve(clicks.size());
    for (auto const& c : clicks) {
        timeline.push_back(EbReplayEvent{c.nominalFrame, 1, c.player2, c.down});
    }

    int simsLeft = budgetSims;
    int expanded = 0;
    auto cancelled = [&]() -> bool { return cancel && cancel->load(); };
    auto spend = [&]() -> bool {
        if (simsLeft <= 0)
            return true;
        --simsLeft;
        return false;
    };

    for (size_t index = 0; index < clicks.size(); ++index) {
        if (cancelled() || simsLeft <= 0)
            break;

        auto& click = clicks[index];
        int const F = click.nominalFrame;
        int next = -1;
        for (size_t otherIndex = index + 1; otherIndex < clicks.size(); ++otherIndex) {
            if (clicks[otherIndex].player2 != click.player2)
                continue;
            next = clicks[otherIndex].nominalFrame;
            break;
        }
        int until = next > 0 ? std::min(next, F + horizon) : F + horizon;
        until = std::max(until, F + 1);

        // Keep timeline event frames in sync with current click nominals.
        timeline[index].frame = F;

        std::lock_guard lock(owner.m_simMutex);
        if (spend())
            break;
        if (!owner.testShiftedClick(layer, startSnapshotSlot, timeline, static_cast<int>(index), F,
                                    until, deltaTime)) {
            // Nominal dies from start snapshot — keep point window.
            continue;
        }

        int lowerBound = std::max(0, F - bakeRadius);
        int upperBound = F;
        while (lowerBound < upperBound) {
            if (cancelled() || simsLeft <= 0)
                break;
            int mid = lowerBound + (upperBound - lowerBound) / 2;
            if (spend())
                break;
            if (owner.testShiftedClick(layer, startSnapshotSlot, timeline, static_cast<int>(index),
                                       mid, until, deltaTime)) {
                upperBound = mid;
            } else {
                lowerBound = mid + 1;
            }
        }
        int earliest = lowerBound;

        lowerBound = F;
        upperBound = F + bakeRadius;
        while (lowerBound < upperBound) {
            if (cancelled() || simsLeft <= 0)
                break;
            int mid = lowerBound + (upperBound - lowerBound + 1) / 2;
            if (spend())
                break;
            if (owner.testShiftedClick(layer, startSnapshotSlot, timeline, static_cast<int>(index),
                                       mid, until, deltaTime)) {
                lowerBound = mid;
            } else {
                upperBound = mid - 1;
            }
        }
        int latest = lowerBound;

        if (earliest > latest)
            std::swap(earliest, latest);
        if (earliest < click.earliest || latest > click.latest || earliest != click.nominalFrame ||
            latest != click.nominalFrame) {
            if (earliest != click.earliest || latest != click.latest)
                ++expanded;
            click.earliest = std::min(click.earliest, earliest);
            click.latest = std::max(click.latest, latest);
        }
    }

    EbAssistLog::debug(fmt::format("self-bake refine: {}/{} expanded, sims left {}", expanded,
                                 clicks.size(), simsLeft));
    return expanded;
}

std::vector<EbBakedClick const*>
EbBakeWindowIndex::nextClicksAfter(EbBakeMap const& owner, float playerX, bool down, bool player2,
                                 std::optional<GameModeKind> preferMode, int maxCount) {
    std::vector<EbBakedClick const*> out;
    if (maxCount <= 0 || !owner.m_loaded)
        return out;

    auto collect = [&](bool requireMode) {
        std::vector<EbBakedClick const*> hits;
        for (auto const& c : owner.m_data.clicks) {
            if (c.down != down || c.player2 != player2)
                continue;
            if (c.x <= playerX + 1.f)
                continue;
            if (requireMode && preferMode && c.mode != *preferMode)
                continue;
            hits.push_back(&c);
        }
        std::sort(hits.begin(), hits.end(), [](auto const* firstValue, auto const* secondValue) {
            if (firstValue->x != secondValue->x)
                return firstValue->x < secondValue->x;
            return firstValue->nominalFrame < secondValue->nominalFrame;
        });
        return hits;
    };

    auto hits = preferMode ? collect(true) : collect(false);
    if (hits.empty() && preferMode)
        hits = collect(false);
    for (auto const* candidate : hits) {
        out.push_back(candidate);
        if (static_cast<int>(out.size()) >= maxCount)
            break;
    }
    return out;
}

int EbBakeWindowIndex::mergeSelfClicks(EbBakeMap& owner, std::vector<EbBakedClick> const& incoming) {
    if (incoming.empty() || !owner.m_loaded)
        return 0;
    if (owner.isBaking())
        return 0;

    bool const wasEmpty = owner.m_data.clicks.empty();
    int touched = 0;

    for (auto const& neu : incoming) {
        EbBakedClick* match = nullptr;
        for (auto& old : owner.m_data.clicks) {
            if (old.down != neu.down || old.player2 != neu.player2)
                continue;
            if (std::fabs(old.x - neu.x) > EbBakeMap::MERGE_X_TOL)
                continue;
            if (std::abs(old.nominalFrame - neu.nominalFrame) > EbBakeMap::MERGE_FRAME_TOL)
                continue;
            match = &old;
            break;
        }
        if (match) {
            int const prevE = match->earliest;
            int const prevL = match->latest;
            bool const prevHold = match->holdAssist;
            match->earliest = std::min(match->earliest, neu.earliest);
            match->latest = std::max(match->latest, neu.latest);
            match->holdAssist = match->holdAssist || neu.holdAssist;
            if (match->earliest != prevE || match->latest != prevL ||
                match->holdAssist != prevHold) {
                ++touched;
            }
            continue;
        }
        if (owner.m_data.clicks.size() >= EbBakeMap::kMaxMapClicks)
            continue;
        owner.m_data.clicks.push_back(neu);
        ++touched;
    }

    if (touched <= 0)
        return 0;

    if (wasEmpty || owner.m_data.method == "self" || owner.m_data.method.empty()) {
        owner.m_data.method = "self";
        if (owner.m_data.sourceReplay.empty())
            owner.m_data.sourceReplay = "self-bake";
    }
    // Do not sync-write here — death/reset flushSegment hit this on the main
    // thread and stalled restarts. Persist on quit / level complete / explicit save.
    owner.m_dirty = true;
    return touched;
}

float EbBakeWindowIndex::windowHalfW(int earliest, int latest) {
    // Keep the drawn band tight — oversized boxes invited late presses that
    // still look "in window" after latest (salvage then dies).
    int const span = (std::max)(0, latest - earliest);
    return (std::clamp)(2.5f + static_cast<float>(span) * 0.28f, 2.5f, 16.f);
}

EbBakedClick const* EbBakeWindowIndex::findNear(EbBakeMap const& owner, float playerX, int approxFrame,
                                            bool down, bool player2,
                                            std::optional<GameModeKind> preferMode) {
    // Temporal gate: do not pick a window whose absolute bake frames are far
    // from approxFrame. LOOKUP_X_TOL alone can match ~100f early at cube speed
    // and the old relative frameToTick then treated that as "in window".
    static constexpr int kFramePastSlop = 240;
    static constexpr int kFrameFutureSlop = 48; // slightly before earliest OK

    // Prefer clicks whose drawn band contains playerX (viz overlap), else nearest X.
    // Among temporal candidates, prefer those with approxFrame inside [earliest, latest].
    auto pick = [&](bool requireMode) -> EbBakedClick const* {
        EbBakedClick const* best = nullptr;
        float bestScore = 1e9f;
        for (auto const& c : owner.m_data.clicks) {
            if (c.down != down || c.player2 != player2)
                continue;
            if (requireMode && preferMode && c.mode != *preferMode)
                continue;
            float const deltaX = std::fabs(c.x - playerX);
            if (deltaX > EbBakeMap::LOOKUP_X_TOL)
                continue;
            if (approxFrame >= 0) {
                if (c.latest + kFramePastSlop < approxFrame)
                    continue; // long past
                if (approxFrame + kFrameFutureSlop < c.earliest)
                    continue; // too early
            }
            float const halfW = owner.windowHalfW(c.earliest, c.latest);
            bool const inBand = deltaX <= halfW;
            bool const inTime =
                approxFrame < 0 || (approxFrame >= c.earliest && approxFrame <= c.latest);
            // Score: in-time+in-band best, then in-time, then in-band, then dx.
            float score = deltaX;
            if (!inBand)
                score += EbBakeMap::LOOKUP_X_TOL;
            if (!inTime)
                score += EbBakeMap::LOOKUP_X_TOL * 2.f;
            if (score < bestScore) {
                bestScore = score;
                best = &c;
            }
        }
        return best;
    };

    if (preferMode) {
        if (auto const* hit = pick(true))
            return hit;
    }
    return pick(false);
}

bool EbBakeWindowIndex::testShiftedClick(EbBakeMap const& owner, PlayLayer* layer,
                                       int startSnapshotSlot, std::vector<EbReplayEvent> timeline,
                                       int eventIndex, int testFrame, int untilFrame,
                                       float deltaTime, int substeps) {
    if (eventIndex < 0 || eventIndex >= static_cast<int>(timeline.size()))
        return false;
    timeline[static_cast<size_t>(eventIndex)].frame = testFrame;
    std::stable_sort(timeline.begin(), timeline.end(),
                     [](auto const& a, auto const& b) { return a.frame < b.frame; });
    auto result = EbShadowSimulator::get().runBakeTimeline(
        layer, startSnapshotSlot, timeline, deltaTime, untilFrame, -1, true, false, true, substeps);
    return !result.died;
}
