#ifndef LAG_WARNING_TOAST_HPP
#define LAG_WARNING_TOAST_HPP

#include <Geode/Geode.hpp>
#include <string>

// Bottom-left lag notification (Assist Pack AP-L7). ImGui card matching the
// desktop shell chrome. Once per level; gated by lag-warning-toast.
class EbLagWarningToast {
public:
    static constexpr float kHoldSeconds = 5.f;
    static constexpr float kFadeSeconds = 0.75f;

    static EbLagWarningToast& get();

    void attach(PlayLayer* layer);
    void detach(bool layerDying);
    void onPhysicsTick(float deltaTimeSeconds);
    void notify(int planUs, int budgetUs, bool budgetHit);

    // ImGui draw path (desktop). No-op stubs on mobile.
    static bool wantsDraw();
    static void draw();

private:
    friend class EbServiceStore;

    EbLagWarningToast() = default;

    float opacity() const;

    float m_ageSeconds = 0.f;
    bool m_showing = false;
    bool m_shownThisLevel = false;
    std::string m_reason;
};

#endif
