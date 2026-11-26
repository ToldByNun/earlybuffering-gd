#include "hook_orchestration.hpp"
#include "../assist_scope_gate.hpp"
#include <fmt/format.h>
#include <Geode/Geode.hpp>

EbHookOrchestration::Phase& EbHookOrchestration::phaseRef() {
    static Phase s_phase = Phase::Idle;
    return s_phase;
}
int& EbHookOrchestration::injectDepth() {
    static int s_depth = 0;
    return s_depth;
}
int& EbHookOrchestration::planningDepth() {
    static int s_depth = 0;
    return s_depth;
}
int& EbHookOrchestration::prePhysicsDepth() {
    static int s_depth = 0;
    return s_depth;
}
int& EbHookOrchestration::realUpdateDepth() {
    static int s_depth = 0;
    return s_depth;
}

char const* EbHookOrchestration::phaseName(Phase phase) {
    switch (phase) {
    case Phase::Idle:
        return "Idle";
    case Phase::PrePhysicsAssist:
        return "PrePhysicsAssist";
    case Phase::RealUpdate:
        return "RealUpdate";
    }
    return "Idle";
}

EbHookOrchestration::Phase EbHookOrchestration::phase() {
    return phaseRef();
}
bool EbHookOrchestration::injecting() {
    return injectDepth() > 0;
}
bool EbHookOrchestration::planning() {
    return planningDepth() > 0;
}

bool EbHookOrchestration::enterPrePhysicsAssist() {
    // Nested PrePhysics refused — one assist tick at a time.
    if (prePhysicsDepth() > 0 || realUpdateDepth() > 0)
        return false;
    ++prePhysicsDepth();
    phaseRef() = Phase::PrePhysicsAssist;
    return true;
}

void EbHookOrchestration::leavePrePhysicsAssist() {
    if (prePhysicsDepth() <= 0)
        return;
    --prePhysicsDepth();
    if (prePhysicsDepth() == 0 && realUpdateDepth() == 0)
        phaseRef() = Phase::Idle;
}

bool EbHookOrchestration::enterRealUpdate() {
    // RealUpdate follows PrePhysics on the same tick (PrePhysics already left)
    // or can nest only once.
    if (realUpdateDepth() > 0 || injecting() || planning())
        return false;
    ++realUpdateDepth();
    phaseRef() = Phase::RealUpdate;
    return true;
}

void EbHookOrchestration::leaveRealUpdate() {
    if (realUpdateDepth() <= 0)
        return;
    --realUpdateDepth();
    if (realUpdateDepth() == 0 && prePhysicsDepth() == 0)
        phaseRef() = Phase::Idle;
}

bool EbHookOrchestration::beginInject() {
    if (!orderAllowsInject())
        return false;
    ++injectDepth();
    return true;
}

void EbHookOrchestration::endInject() {
    if (injectDepth() > 0)
        --injectDepth();
}

bool EbHookOrchestration::beginPlanning() {
    if (!orderAllowsPlan())
        return false;
    ++planningDepth();
    return true;
}

void EbHookOrchestration::endPlanning() {
    if (planningDepth() > 0)
        --planningDepth();
}

bool EbHookOrchestration::orderAllowsPlan() {
    // Plan only during PrePhysicsAssist, never while injecting or already planning.
    if (phaseRef() != Phase::PrePhysicsAssist)
        return false;
    if (injecting())
        return false;
    if (planning())
        return false;
    return true;
}

bool EbHookOrchestration::orderAllowsInject() {
    // Inject from PrePhysics (buffer fire / Autoplay) or RealUpdate (mid-step CBF).
    // Never nest inject; never inject while planning (shadow must not fire live).
    if (injecting())
        return false;
    if (planning())
        return false;
    Phase const p = phaseRef();
    return p == Phase::PrePhysicsAssist || p == Phase::RealUpdate || p == Phase::Idle;
}

bool EbHookOrchestration::platformerHardOff(PlayerObject* player) {
    return EbAssistScopeGate::isPlatformer(player);
}

bool EbHookOrchestration::mayRunAutoplay(PlayLayer* layer, PlayerObject* player) {
    (void)layer;
    if (!player || player->m_isDead)
        return false;
    if (platformerHardOff(player))
        return false;
    if (phaseRef() != Phase::PrePhysicsAssist)
        return false;
    if (injecting() || planning())
        return false;
    return true;
}

EbHookOrchestration::PrePhysicsGuard::PrePhysicsGuard() {
    m_ok = enterPrePhysicsAssist();
}
EbHookOrchestration::PrePhysicsGuard::~PrePhysicsGuard() {
    if (m_ok)
        leavePrePhysicsAssist();
}

EbHookOrchestration::RealUpdateGuard::RealUpdateGuard() {
    m_ok = enterRealUpdate();
}
EbHookOrchestration::RealUpdateGuard::~RealUpdateGuard() {
    if (m_ok)
        leaveRealUpdate();
}

EbHookOrchestration::InjectGuard::InjectGuard() {
    m_ok = beginInject();
}
EbHookOrchestration::InjectGuard::~InjectGuard() {
    if (m_ok)
        endInject();
}

EbHookOrchestration::PlanningGuard::PlanningGuard() {
    m_ok = beginPlanning();
}
EbHookOrchestration::PlanningGuard::~PlanningGuard() {
    if (m_ok)
        endPlanning();
}

bool EbHookOrchestration::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok)
            return;
        ++fails;
        if (!report.empty())
            report += "; ";
        report += name;
    };

    // Reset any leaked depths from a prior failed check.
    while (injectDepth() > 0)
        endInject();
    while (planningDepth() > 0)
        endPlanning();
    while (prePhysicsDepth() > 0)
        leavePrePhysicsAssist();
    while (realUpdateDepth() > 0)
        leaveRealUpdate();
    expect(phase() == Phase::Idle && !injecting() && !planning(), "fresh-idle");

    expect(std::string(phaseName(Phase::PrePhysicsAssist)) == "PrePhysicsAssist", "name-pre");
    expect(std::string(phaseName(Phase::RealUpdate)) == "RealUpdate", "name-real");

    // Tick order: PrePhysics → plan/inject → leave → RealUpdate.
    {
        PrePhysicsGuard pre;
        expect(static_cast<bool>(pre), "enter-pre");
        expect(phase() == Phase::PrePhysicsAssist, "phase-pre");
        expect(orderAllowsPlan(), "plan-in-pre");
        expect(orderAllowsInject(), "inject-in-pre");
        {
            PlanningGuard plan;
            expect(static_cast<bool>(plan), "plan-ok");
            expect(!orderAllowsPlan(), "no-replan");
            expect(!beginPlanning(), "replan-refuse");
            expect(!orderAllowsInject(), "no-inject-while-plan");
            expect(!beginInject(), "inject-during-plan-refuse");
        }
        expect(orderAllowsPlan(), "plan-after");
        {
            InjectGuard inj;
            expect(static_cast<bool>(inj), "inject-ok");
            expect(injecting(), "injecting");
            expect(!beginInject(), "reinject-refuse");
            expect(!orderAllowsPlan(), "no-plan-while-inject");
        }
        expect(!injecting(), "inject-cleared");
        // Nested PrePhysics refused.
        expect(!enterPrePhysicsAssist(), "nested-pre-refuse");
    }
    expect(phase() == Phase::Idle, "back-idle");

    {
        RealUpdateGuard real;
        expect(static_cast<bool>(real), "enter-real");
        expect(phase() == Phase::RealUpdate, "phase-real");
        expect(!orderAllowsPlan(), "no-plan-in-real");
        expect(orderAllowsInject(), "inject-in-real"); // mid-step CBF
        expect(!enterRealUpdate(), "nested-real-refuse");
    }
    expect(phase() == Phase::Idle, "idle-after-real");

    // Idle inject allowed (sync paths / tests); plan not.
    expect(!orderAllowsPlan(), "no-plan-idle");
    expect(orderAllowsInject(), "inject-idle-ok");

    // Platformer hard-off: null player is not platformer; mayRunAutoplay needs PrePhysics.
    expect(!platformerHardOff(nullptr), "plat-null");
    expect(!mayRunAutoplay(nullptr, nullptr), "autoplay-null");
    {
        PrePhysicsGuard pre;
        expect(static_cast<bool>(pre) && !mayRunAutoplay(nullptr, nullptr), "autoplay-still-null");
    }

    // Full order smoke: PrePhysics plan → inject → RealUpdate.
    {
        PrePhysicsGuard pre;
        expect(static_cast<bool>(pre), "order-pre");
        {
            PlanningGuard plan;
            expect(static_cast<bool>(plan), "order-plan");
        }
        {
            InjectGuard inj;
            expect(static_cast<bool>(inj), "order-inj");
        }
    }
    {
        RealUpdateGuard real;
        expect(static_cast<bool>(real), "order-real");
    }
    expect(phase() == Phase::Idle && !injecting() && !planning(), "order-done");

    if (fails == 0) {
        report = "HookOrchestration selfCheck OK";
        return true;
    }
    report = fmt::format("HookOrchestration selfCheck {} fails: {}", fails, report);
    return false;
}
