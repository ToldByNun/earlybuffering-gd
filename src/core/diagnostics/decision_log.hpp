#ifndef DECISION_LOG_HPP
#define DECISION_LOG_HPP

#include "../../config/stage_config.hpp"
#include "../sim/shadow_simulator.hpp"
#include <Geode/Geode.hpp>
#include <optional>
#include <vector>

class EbDecisionEntry {
public:
    int tick = 0; // effect / decision tick
    float x = 0.f;
    GameModeKind mode = GameModeKind::Cube;
    bool down = true; // press vs release
    int delay = 0;    // planned delay (0 if Pass)
    int limit = 0;
    int firstDeathTick = -1;
    PlanAction action = PlanAction::Pass;
    bool nowIsSafe = false;
    int snapshotSlot = -1;
    int horizon = 48;      // ticks after decision to consider for survive/death
    bool resolved = false; // survival / pessimism already applied
    bool assisted = false; // Delay was chosen (or would have been)

    std::vector<EbInputEvent> timeline; // absolute ticks from decision onward

private:
};

class EbDecisionLog {
public:
    static constexpr int CAPACITY = EbShadowSimulator::SNAPSHOT_SLOTS;

    void clear();
    EbDecisionEntry& push(EbDecisionEntry entry);
    EbDecisionEntry* latest();
    EbDecisionEntry const* latest() const;

    // Append an input to the latest open decision's timeline (and any still open).
    void noteInput(int tick, bool down);

    // Iterate mutable entries (newest last).
    std::vector<EbDecisionEntry>& entries() {
        return m_entries;
    }
    std::vector<EbDecisionEntry> const& entries() const {
        return m_entries;
    }

    // Most recent unresolved decision whose window still covers `tick`.
    EbDecisionEntry* findForDeath(int tick);

private:
    std::vector<EbDecisionEntry> m_entries;
};

#endif
