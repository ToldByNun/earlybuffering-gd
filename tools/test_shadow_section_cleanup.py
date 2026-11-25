"""Exercise the production cleanup against a large mocked collision grid."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
source = (root / "src/core/sim/runtime/shadow_section_safety.cpp").read_text()
start = source.index("void EbShadowSectionSafety::scrubOwnedGhostsFromSections(")
end = source.index("void EbShadowSectionSafety::evacuateGhosts(", start)
with tempfile.TemporaryDirectory(prefix="eb-section-cleanup-") as directory:
    work = Path(directory)
    test = work / "test.cpp"
    test.write_text(r'''
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <unordered_set>
#include <vector>
#include <iostream>
class GameObject {
public:
    int m_outerSectionIndex = -1, m_innerSectionIndex = -1, m_middleSectionIndex = -1;
};
class PlayerObject : public GameObject {};
class CountedSections {
public:
    using Cell = std::vector<GameObject*>;
    using Column = std::vector<Cell*>;
    std::vector<Column*> values;
    int visits = 0;
    std::size_t size() const { return values.size(); }
    Column* operator[](std::size_t i) { ++visits; return values[i]; }
};
class GJBaseGameLayer { public: CountedSections m_sections; };
class EbShadowSimulator {
public:
    static constexpr int SNAPSHOT_SLOTS = 16, DEV_EVAL_SLOTS = 2;
    PlayerObject* m_shadow = nullptr;
    PlayerObject* m_snapshots[SNAPSHOT_SLOTS]{};
    PlayerObject* m_evalGhosts[DEV_EVAL_SLOTS]{};
};
class EbCpuPhysicsWorkers {
public:
    inline static std::vector<PlayerObject*> workers;
    static PlayerObject* templateGhost() { return nullptr; }
    static std::vector<PlayerObject*> const& ghosts() { return workers; }
};
class EbShadowSectionSafety {
public:
    static void scrubOwnedGhostsFromSections(GJBaseGameLayer*, EbShadowSimulator*);
};
''' + source[start:end] + r'''
class SectionCleanupRegression {
public:
    static int run() {
        GJBaseGameLayer layer;
        EbShadowSimulator sim;
        PlayerObject active, parked[18], worker;
        GameObject real;
        CountedSections::Cell empty, occupied;
        CountedSections::Column emptyColumn{&empty}, activeColumn{&occupied};
        layer.m_sections.values.assign(100000, &emptyColumn);
        layer.m_sections.values[7] = &activeColumn;
        active.m_outerSectionIndex = 7;
        active.m_innerSectionIndex = active.m_middleSectionIndex = 0;
        sim.m_shadow = &active;
        for (int i = 0; i < 16; ++i) sim.m_snapshots[i] = &parked[i];
        for (int i = 0; i < 2; ++i) sim.m_evalGhosts[i] = &parked[16+i];
        auto* poison = reinterpret_cast<GameObject*>(~uintptr_t{0});
        occupied = {&real, &active, poison, &active};
        EbShadowSectionSafety::scrubOwnedGhostsFromSections(&layer, &sim);
        assert(occupied == CountedSections::Cell({&real, poison}));
        assert(layer.m_sections.visits <= 2); // no full scan for parked snapshots

        // The third index is not used to address this two-dimensional cell.
        active.m_middleSectionIndex = -1;
        occupied.push_back(&active);
        layer.m_sections.visits = 0;
        EbShadowSectionSafety::scrubOwnedGhostsFromSections(&layer, &sim);
        assert(occupied == CountedSections::Cell({&real, poison}));
        assert(layer.m_sections.visits <= 2);

        // Indexed workers belong to the same cleanup, even with no main ghost.
        sim.m_shadow = nullptr;
        worker.m_outerSectionIndex = 7;
        worker.m_innerSectionIndex = worker.m_middleSectionIndex = 0;
        EbCpuPhysicsWorkers::workers = {&worker};
        occupied.push_back(&worker);
        layer.m_sections.visits = 0;
        EbShadowSectionSafety::scrubOwnedGhostsFromSections(&layer, &sim);
        assert(occupied == CountedSections::Cell({&real, poison}));
        assert(layer.m_sections.visits <= 2);

        // A stale recorded cell must still trigger the full safety fallback.
        worker.m_outerSectionIndex = 4;
        layer.m_sections.values[90000] = &activeColumn;
        layer.m_sections.values[7] = &emptyColumn;
        occupied.push_back(&worker);
        layer.m_sections.visits = 0;
        EbShadowSectionSafety::scrubOwnedGhostsFromSections(&layer, &sim);
        assert(occupied == CountedSections::Cell({&real, poison}));
        assert(layer.m_sections.visits >= 90000);
        EbCpuPhysicsWorkers::workers.clear();
        layer.m_sections.visits = 0;
        EbShadowSectionSafety::scrubOwnedGhostsFromSections(&layer, &sim);
        assert(layer.m_sections.visits == 0);
        std::cout << "Section cleanup: local erase, duplicates, workers, poisoned slots, stale-cell fallback PASS\n";
        return 0;
    }
};
int main() { return SectionCleanupRegression::run(); }
''', encoding="utf-8")
    executable = work / "test.exe"
    subprocess.run(["g++", "-std=c++23", "-O2", str(test), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
