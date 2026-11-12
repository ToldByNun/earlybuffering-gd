#ifndef DESKTOP_MENU_MOTION_HPP
#define DESKTOP_MENU_MOTION_HPP

#if defined(EB_DESKTOP_IMGUI)
#include <imgui.h>
#endif

// Shared exponential smoothing for widget hover, values, and shell motion.
class EbDesktopMenuMotion {
public:
#if defined(EB_DESKTOP_IMGUI)
    static float approach(float current, float target, float tau, float deltaTime);
    static ImVec2 approach2(ImVec2 const& current, ImVec2 const& target, float tau,
                            float deltaTime);
    static ImVec4 lerp4(ImVec4 const& from, ImVec4 const& to, float t);

    // Call once at the start of the shell frame (prunes stale slots).
    static void beginFrame();

    // Smooth 0..1 toward `active` for this ImGui id (hover / press / open).
    static float smoothToward(ImGuiID id, bool active, float tau = 0.05f);

    // Smooth any float toward target, keyed by id (check anim, slider visual, scroll…).
    // New/reclaimed slots seed to `target` (no 0→target replay after scroll).
    static float smoothFloat(ImGuiID id, float target, float tau = 0.07f);
#endif

private:
    EbDesktopMenuMotion() = delete;
};

#endif
