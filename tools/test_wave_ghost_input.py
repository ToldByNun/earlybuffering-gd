"""Exercise actual ghost input and contact cleanup bodies with a minimal GD double."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
hooks = (root / "src/hooks/player_hooks.cpp").read_text()
state = (root / "src/core/sim/runtime/shadow_player_state.cpp").read_text()
release = hooks[hooks.index("auto EbPlayerObject::releaseButton("):
                hooks.index("auto EbPlayerObject::createFadeOutDartStreak(")]
fade = hooks[hooks.index("auto EbPlayerObject::createFadeOutDartStreak("):
             hooks.index("auto EbPlayerObject::fadeOutStreak2(")]
cleanup = state[state.index("void EbShadowPlayerState::clearCollisionLogs("):
                state.index("void EbShadowPlayerState::copyPlayerState(")]
fixture = r'''
#include <cassert>
#include <iostream>
enum class PlayerButton { Jump = 1 };
class Dictionary {
public:
    int count = 3;
    void removeAllObjects() { count = 0; }
};
class PlayerObject {
public:
    virtual ~PlayerObject() = default;
    bool ghost = false, m_isDart = true, buffered = true, held = true;
    bool releaseResult = false;
    int releases = 0, fades = 0;
    Dictionary logs[4];
    Dictionary *m_collisionLogTop = &logs[0], *m_collisionLogBottom = &logs[1];
    Dictionary *m_collisionLogLeft = &logs[2], *m_collisionLogRight = &logs[3];
    int m_lastCollisionTop = 13, m_lastCollisionBottom = 14;
    int m_lastCollisionLeft = 15, m_lastCollisionRight = 16;
    virtual void createFadeOutDartStreak() { ++fades; }
    bool releaseButton(PlayerButton) {
        ++releases;
        buffered = false;
        held = false;
        createFadeOutDartStreak();
        return releaseResult;
    }
};
class EbPlayerObject : public PlayerObject {
public:
    bool releaseButton(PlayerButton button);
    void createFadeOutDartStreak() override;
};
class EbShadowSimulator {
public:
    static EbShadowSimulator& get() { static EbShadowSimulator instance; return instance; }
    bool isOwnedGhost(PlayerObject* player) { return player->ghost; }
};
class EbGhostStreakVisuals {
public:
    static void mute(PlayerObject*) {}
};
class EbShadowPlayerState {
public:
    static void clearCollisionLogs(PlayerObject* state);
};
'''
checks = r'''
int main() {
    EbPlayerObject ghost;
    ghost.ghost = true;
    assert(!ghost.releaseButton(PlayerButton::Jump));
    assert(ghost.releases == 1 && !ghost.held && !ghost.buffered && ghost.fades == 0);
    ghost.releaseResult = true;
    assert(ghost.releaseButton(PlayerButton::Jump));
    assert(ghost.releases == 2 && ghost.fades == 0);
    EbPlayerObject live;
    assert(!live.releaseButton(PlayerButton::Jump));
    assert(live.releases == 1 && live.fades == 1);
    EbShadowPlayerState::clearCollisionLogs(&ghost);
    for (auto const& log : ghost.logs) assert(log.count == 0);
    assert(ghost.m_lastCollisionTop == 13 && ghost.m_lastCollisionBottom == 14);
    assert(ghost.m_lastCollisionLeft == 15 && ghost.m_lastCollisionRight == 16);
    ghost.m_isDart = false;
    EbShadowPlayerState::clearCollisionLogs(&ghost);
    assert(ghost.m_lastCollisionTop == -1 && ghost.m_lastCollisionBottom == -1);
    std::cout << "Wave ghost release / visual isolation / contact history PASS\n";
}
'''
with tempfile.TemporaryDirectory(prefix="eb-wave-input-") as directory:
    source = Path(directory) / "test.cpp"
    executable = Path(directory) / "test.exe"
    source.write_text(fixture + release + fade + cleanup + checks)
    subprocess.run(["g++", "-std=c++23", str(source), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
