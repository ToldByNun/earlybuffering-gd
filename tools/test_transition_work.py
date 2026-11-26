"""Compile transition production bodies: lazy slots and owned, ordered async saves."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
lifecycle = (root / "src/core/sim/runtime/shadow_ghost_lifecycle.cpp").read_text()
ensure = lifecycle[lifecycle.index("PlayerObject* EbShadowGhostLifecycle::ensureSnapshot("):
                   lifecycle.index("void EbShadowGhostLifecycle::attach(")]
store = (root / "src/dev/capture/dev_trace_store.cpp").read_text()
flush = store[store.index("bool EbDevTraceStore::flush("):
              store.index("void EbDevTraceStore::closeSession(")]
key_header = (root / "src/core/level/level_content_key.hpp").read_text()
key_source = (root / "src/core/level/level_content_key.cpp").read_text()
key_code = "\n".join(line for line in (key_header + key_source).splitlines()
                     if not line.startswith("#include"))
fixture = r'''
#include <cassert>
#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include <cstdint>
#include <fmt/format.h>
class GJGameLevel {
public:
    std::string m_levelString = "abc", m_levelName = "level";
    int m_levelLength = 1, m_levelID = 42;
};
namespace geode { namespace log { void warn(char const*) {} } }
class PlayerObject {};
class EbShadowSimulator {
public:
    static constexpr int SNAPSHOT_SLOTS = 16;
    void* m_hostLayer = nullptr;
    void* m_ghostRoot = nullptr;
    PlayerObject* m_snapshots[16]{};
};
class EbShadowGhostLifecycle {
public:
    static PlayerObject* ensureSnapshot(EbShadowSimulator&, int);
    inline static int creations = 0;
    inline static bool failCreation = false;
    static PlayerObject* makeHidden(EbShadowSimulator&, void*, char const*) {
        ++creations;
        return failCreation ? nullptr : new PlayerObject;
    }
};
class Session {
public:
    int value = 1;
};
static std::promise<void> started, releaseWorker;
static auto gate = releaseWorker.get_future().share();
class EbDevTraceStoreImplementation {
public:
    static int sessionToJsonValue(Session const& session) {
        if (session.value == 1) {
            started.set_value();
            gate.wait();
        }
        return session.value;
    }
};
class EbDevTraceStore {
public:
    bool m_open = true;
    Session m_session;
    std::future<bool> m_pendingFlush;
    std::filesystem::path path = "first.json";
    inline static std::vector<std::pair<std::filesystem::path, int>> writes;
    std::filesystem::path sessionPath() { return path; }
    static bool writeAtomic(std::filesystem::path const& path, int value) {
        writes.emplace_back(path, value);
        return true;
    }
    bool flush(bool background);
};
'''
checks = r'''
int main() {
    GJGameLevel level;
    auto originalKey = EbLevelContentKey::fullKey(&level);
    {
        EbLevelContentKey::SetupScope scope(&level);
        assert(EbLevelContentKey::fullKey(&level) == originalKey);
        // Scope is explicitly limited to immutable setup. Its cached identity
        // must not survive the transition, even after a same-length edit.
        level.m_levelString = "xyz";
        assert(EbLevelContentKey::fullKey(&level) == originalKey);
    }
    auto editedKey = EbLevelContentKey::fullKey(&level);
    assert(editedKey != originalKey);
    {
        EbLevelContentKey::SetupScope scope(&level);
        assert(EbLevelContentKey::fullKey(&level) == editedKey);
    }
    assert(EbLevelContentKey::fullKey(nullptr) == "unknown_rev_0");
    EbShadowSimulator simulator;
    assert(!EbShadowGhostLifecycle::ensureSnapshot(simulator, 0));
    simulator.m_hostLayer = &simulator;
    simulator.m_ghostRoot = &simulator;
    assert(!EbShadowGhostLifecycle::ensureSnapshot(simulator, -1));
    assert(!EbShadowGhostLifecycle::ensureSnapshot(simulator, 16));
    assert(EbShadowGhostLifecycle::creations == 0);
    auto* slot = EbShadowGhostLifecycle::ensureSnapshot(simulator, 7);
    assert(slot && EbShadowGhostLifecycle::ensureSnapshot(simulator, 7) == slot);
    assert(EbShadowGhostLifecycle::creations == 1);
    assert(!simulator.m_snapshots[0] && !simulator.m_snapshots[15]);
    EbShadowGhostLifecycle::failCreation = true;
    assert(!EbShadowGhostLifecycle::ensureSnapshot(simulator, 8));
    EbShadowGhostLifecycle::failCreation = false;
    assert(EbShadowGhostLifecycle::ensureSnapshot(simulator, 8));
    for (auto* snapshot : simulator.m_snapshots) delete snapshot;
    EbDevTraceStore store;
    auto entered = started.get_future();
    assert(store.flush(true));
    assert(entered.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    assert(store.m_pendingFlush.wait_for(std::chrono::seconds(0)) == std::future_status::timeout);
    store.m_session.value = 2;
    store.path = "second.json";
    releaseWorker.set_value();
    assert(store.flush(false));
    assert(store.writes.size() == 2);
    assert(store.writes[0].first == "first.json" && store.writes[0].second == 1);
    assert(store.writes[1].first == "second.json" && store.writes[1].second == 2);
    store.m_open = false;
    assert(!store.flush(true));
    std::cout << "Lazy snapshot reuse / allocation failure / async ownership and ordering PASS\n";
}
'''
with tempfile.TemporaryDirectory(prefix="eb-transitions-") as directory:
    source = Path(directory) / "test.cpp"
    executable = Path(directory) / "test.exe"
    source.write_text(fixture + key_code + ensure + flush + checks)
    subprocess.run(["g++", "-std=c++23", "-pthread", "-DFMT_HEADER_ONLY",
                    "-I", str(root / "build/_deps/fmt-src/include"),
                    str(source), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True, timeout=15)
