#ifndef HOOK_ORCHESTRATION_HPP
#define HOOK_ORCHESTRATION_HPP

#include <cstdint>
#include <string>

class PlayLayer;
class PlayerObject;

// Assist Pack AP-NL7 — Hook orchestration parity (Nova planner phase vs handleButton).
//
// Expected live tick order for P1:
//   Idle → PrePhysicsAssist (onPhysicsTick: plan / Autoplay / buffer fire)
//        → RealUpdate (PlayerObject::update body; CBF mid-step)
//        → Idle
//
// Single inject flag: recursive handleButton from inject must not re-enter planning.
// Single planning flag: recursive plan() refused.
// Platformer: hard-off for Autoplay (and documented for Live via scope gate).
class EbHookOrchestration {
public:
    enum class Phase : std::uint8_t {
        Idle = 0,
        PrePhysicsAssist = 1,
        RealUpdate = 2,
    };

    static char const* phaseName(Phase phase);
    static Phase phase();
    static bool injecting();
    static bool planning();

    static bool enterPrePhysicsAssist();
    static void leavePrePhysicsAssist();
    static bool enterRealUpdate();
    static void leaveRealUpdate();

    // Single inject — false if already injecting (recursive refuse).
    static bool beginInject();
    static void endInject();

    // Single planning — false if already planning or currently injecting.
    static bool beginPlanning();
    static void endPlanning();

    static bool orderAllowsPlan();
    static bool orderAllowsInject();

    // Platformer hard-off (Autoplay must not schedule; Live uses scope gate).
    static bool platformerHardOff(PlayerObject* player);

    // Autoplay may run only in PrePhysicsAssist, not injecting/planning, not platformer.
    static bool mayRunAutoplay(PlayLayer* layer, PlayerObject* player);

    class PrePhysicsGuard {
    public:
        PrePhysicsGuard();
        ~PrePhysicsGuard();
        PrePhysicsGuard(PrePhysicsGuard const&) = delete;
        PrePhysicsGuard& operator=(PrePhysicsGuard const&) = delete;
        explicit operator bool() const {
            return m_ok;
        }

    private:
        bool m_ok = false;
    };

    class RealUpdateGuard {
    public:
        RealUpdateGuard();
        ~RealUpdateGuard();
        RealUpdateGuard(RealUpdateGuard const&) = delete;
        RealUpdateGuard& operator=(RealUpdateGuard const&) = delete;
        explicit operator bool() const {
            return m_ok;
        }

    private:
        bool m_ok = false;
    };

    class InjectGuard {
    public:
        InjectGuard();
        ~InjectGuard();
        InjectGuard(InjectGuard const&) = delete;
        InjectGuard& operator=(InjectGuard const&) = delete;
        explicit operator bool() const {
            return m_ok;
        }

    private:
        bool m_ok = false;
    };

    class PlanningGuard {
    public:
        PlanningGuard();
        ~PlanningGuard();
        PlanningGuard(PlanningGuard const&) = delete;
        PlanningGuard& operator=(PlanningGuard const&) = delete;
        explicit operator bool() const {
            return m_ok;
        }

    private:
        bool m_ok = false;
    };

    static bool selfCheck(std::string& report);

private:
    EbHookOrchestration() = delete;

    static Phase& phaseRef();
    static int& injectDepth();
    static int& planningDepth();
    static int& prePhysicsDepth();
    static int& realUpdateDepth();
};

#endif
