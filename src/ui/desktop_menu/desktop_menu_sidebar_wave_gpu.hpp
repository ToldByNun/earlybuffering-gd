#ifndef DESKTOP_MENU_SIDEBAR_WAVE_GPU_HPP
#define DESKTOP_MENU_SIDEBAR_WAVE_GPU_HPP

#if defined(EB_DESKTOP_IMGUI)
#include <imgui.h>

// GPU sidebar wave — render to FBO during ImGui build (before Render), then AddImage.
// No ImDrawCallback (gd-imgui-cocos treats ResetRenderState as a real fn ptr and
// mid-batch GL corrupts the ImGui VAO). Falls back to CPU if init fails.
class EbDesktopMenuSidebarWaveGpu {
public:
    class PaintRequest {
    public:
        ImVec2 min{};
        ImVec2 max{};
        float timeSeconds = 0.0f;
        float rounding = 0.0f;
        ImDrawFlags cornerFlags = ImDrawFlags_RoundCornersLeft;
        ImVec4 baseColor{};
        ImVec4 lightColor{};

    private:
    };

    // true = program ready (or already failed permanently).
    static bool ensureReady();
    static bool isAvailable();

    // Queue a GPU wave draw for this ImDrawList (clip + callback + state reset).
    static void paint(ImDrawList* drawList, PaintRequest const& request);

private:
    EbDesktopMenuSidebarWaveGpu() = delete;
};

#endif

#endif
