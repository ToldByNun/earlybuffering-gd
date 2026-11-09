#ifndef SIM_SESSION_GUARD_HPP
#define SIM_SESSION_GUARD_HPP

class PlayLayer;
class EbShadowSimulator;

// RAII around a shadow-sim session: beginCapture + m_simulating on enter;
// restoreCapture, clear flags, and parkGhosts on exit (every early return /
// exception path). Keeps live playerTouchedRing from seeing half-restored
// ring/activated state — especially important on iOS with multi-mod update hooks.
class EbSimSessionGuard {
public:
    EbSimSessionGuard(EbShadowSimulator& sim, PlayLayer* layer);
    ~EbSimSessionGuard();

    EbSimSessionGuard(EbSimSessionGuard const&) = delete;
    EbSimSessionGuard& operator=(EbSimSessionGuard const&) = delete;
    EbSimSessionGuard(EbSimSessionGuard&& other) noexcept;
    EbSimSessionGuard& operator=(EbSimSessionGuard&& other) noexcept;

private:
    EbShadowSimulator* m_sim = nullptr;
    PlayLayer* m_layer = nullptr;
};

#endif
