#ifndef FRIRECRACKER_CORE_DECISIONLOG_HPP
#define FRIRECRACKER_CORE_DECISIONLOG_HPP

#include "ShadowSimulator.hpp"
#include "../config/StageConfig.hpp"

#include <Geode/Geode.hpp>
#include <vector>
#include <optional>


struct DecisionEntry {
    int tick = 0;                 // effect / decision tick
    float x = 0.f;
    GameModeKind mode = GameModeKind::Cube;
    bool down = true;             // press vs release
    int delay = 0;                // planned delay (0 if Pass)
    int limit = 0;
    int firstDeathTick = -1;
    PlanAction action = PlanAction::Pass;
    bool nowIsSafe = false;
    int snapshotSlot = -1;
    int horizon = 48;             // ticks after decision to consider for survive/death
    bool resolved = false;        // survival / pessimism already applied
    bool assisted = false;        // Delay was chosen (or would have been)

    std::vector<InputEvent> timeline; // absolute ticks from decision onward
};

class DecisionLog {
public:
    static constexpr int CAPACITY = ShadowSimulator::SNAPSHOT_SLOTS;

    void clear();
    DecisionEntry& push(DecisionEntry entry);
    DecisionEntry* latest();
    DecisionEntry const* latest() const;

    // Append an input to the latest open decision's timeline (and any still open).
    void noteInput(int tick, bool down);

    // Iterate mutable entries (newest last).
    std::vector<DecisionEntry>& entries() { return m_entries; }
    std::vector<DecisionEntry> const& entries() const { return m_entries; }

    // Most recent unresolved decision whose window still covers `tick`.
    DecisionEntry* findForDeath(int tick);

private:
    std::vector<DecisionEntry> m_entries;
};

#endif // FRIRECRACKER_CORE_DECISIONLOG_HPP
