"""Compile the production Shadow filtering branch against a synthetic large level."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
source = (root / "src/hooks/game_layer_hooks.cpp").read_text()
start = source.index("auto EbGJBaseGameLayer::collisionCheckObjects(")
end = source.index("    // Live: strip owned ghosts", start)
function = source[start:end] + "}\n"
with tempfile.TemporaryDirectory(prefix="eb-collision-window-") as directory:
    work = Path(directory)
    test = work / "test.cpp"
    test.write_text(r'''
#include "src/core/sim/world/simulation_window.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <typeinfo>
#include <vector>
class Rect {
public:
    struct Origin { float x=0, y=0; } origin;
    struct Size { float width=30, height=30; } size;
    bool intersectsRect(Rect const& other) const {
        return origin.x < other.origin.x + other.size.width &&
            origin.x + size.width > other.origin.x &&
            origin.y < other.origin.y + other.size.height &&
            origin.y + size.height > other.origin.y;
    }
};
class GameObject {
public:
    virtual ~GameObject() = default;
    Rect box;
    bool unknown = false;
    Rect getObjectRect() { return box; }
    float getPositionX() { return box.origin.x; }
};
class PlayerObject : public GameObject {
public:
    Rect::Origin m_lastPosition;
    float getPositionX() { return box.origin.x; }
};
namespace geode { namespace prelude {
template<class T> T typeinfo_cast(GameObject* value) { return dynamic_cast<T>(value); }
}}
enum class SimObjectAction { Include, UnknownOnOverlap, Skip };
class EbSimCollisionPolicy {
public:
    inline static int visits = 0;
    static SimObjectAction classify(GameObject* object) {
        ++visits;
        return object->unknown ? SimObjectAction::UnknownOnOverlap : SimObjectAction::Include;
    }
    static void noteLimitedSimOnce() {}
};
class EbShadowSimulator {
public:
    PlayerObject* shadow = nullptr;
    bool unknown = false;
    std::vector<GameObject*> scratch;
    static EbShadowSimulator& get() { static EbShadowSimulator value; return value; }
    bool isSimulating() { return true; }
    bool isShadow(PlayerObject* player) { return player == shadow; }
    std::vector<GameObject*>& collisionFilterScratch() { return scratch; }
    void noteUnknown(GameObject*) { unknown = true; }
};
class GJBaseGameLayer {
public:
    std::vector<GameObject*> received;
    void collisionCheckObjects(PlayerObject*, std::vector<GameObject*>* objects, int count, float) {
        received.assign(objects->begin(), objects->begin()+count);
    }
};
class EbGJBaseGameLayer : public GJBaseGameLayer {
public:
    void collisionCheckObjects(PlayerObject*, std::vector<GameObject*>*, int, float);
};
''' + function + r'''
class CollisionWindowRegression {
public:
    static int run() {
        EbGJBaseGameLayer layer;
        PlayerObject player, other;
        auto& sim = EbShadowSimulator::get();
        sim.shadow = &player;
        player.box.origin.x = 1000;
        player.m_lastPosition.x = 1000;
        std::vector<GameObject> distant(20000);
        std::vector<GameObject*> objects;
        for (auto& object : distant) { object.box.origin.x = 50000; objects.push_back(&object); }
        GameObject near, wide, vertical, rear, unknown;
        near.box.origin.x = 1100;
        wide.box.origin.x = 1400; wide.box.size.width = 1000;
        vertical.box.origin.x = 1000; vertical.box.origin.y = 1000000;
        rear.box.origin.x = 100;
        unknown.box.origin.x = 1000; unknown.unknown = true;
        objects.insert(objects.end(), {&near, &wide, &vertical, &rear, &unknown, &other});
        layer.collisionCheckObjects(&player, &objects, static_cast<int>(objects.size()), .25f);
        assert(EbSimCollisionPolicy::visits == 4); // no per-object policy on distant level data
        assert(layer.received == std::vector<GameObject*>({&near, &wide, &vertical}));
        assert(sim.unknown);
        // Preserve swept travel when a dash or teleport crosses the local strip.
        player.m_lastPosition.x = 0;
        layer.collisionCheckObjects(&player, &objects, static_cast<int>(objects.size()), .25f);
        assert(layer.received == std::vector<GameObject*>({&near, &wide, &vertical, &rear}));
        std::cout << "Production Shadow filter: 20,000 distant objects culled; full Y, wide bodies, swept travel and unknowns PASS\n";
        return 0;
    }
};
int main() { return CollisionWindowRegression::run(); }
''', encoding="utf-8")
    executable = work / "test.exe"
    subprocess.run(["g++", "-std=c++23", "-O2", str(test),
                    str(root / "src/core/sim/world/simulation_window.cpp"),
                    "-I", str(root), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
