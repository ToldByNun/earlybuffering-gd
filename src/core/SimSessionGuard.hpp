#ifndef EARLYBUFFER_CORE_SIMSESSIONGUARD_HPP
#define EARLYBUFFER_CORE_SIMSESSIONGUARD_HPP

class PlayLayer;
class ShadowSimulator;

// RAII around a shadow-sim session: beginCapture + m_simulating on enter;
// restoreCapture, clear flags, and parkGhosts on exit (every early return /
// exception path). Keeps live playerTouchedRing from seeing half-restored
// ring/activated state — especially important on iOS with multi-mod update hooks.
class SimSessionGuard {
public:
    SimSessionGuard(ShadowSimulator& sim, PlayLayer* layer);
    ~SimSessionGuard();

    SimSessionGuard(SimSessionGuard const&) = delete;
    SimSessionGuard& operator=(SimSessionGuard const&) = delete;
    SimSessionGuard(SimSessionGuard&& other) noexcept;
    SimSessionGuard& operator=(SimSessionGuard&& other) noexcept;

private:
    ShadowSimulator* m_sim = nullptr;
    PlayLayer* m_layer = nullptr;
};

#endif
