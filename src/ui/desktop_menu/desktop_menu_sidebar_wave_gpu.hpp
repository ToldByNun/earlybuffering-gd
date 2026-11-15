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
        bool glass = false;
        float refraction = 12.f, highlight = 0.3f, edgeWidth = 20.f;
        bool blur = false;
        float classic = 1.f;
        float alpha = 1.f;
        float blurRadius = 8.f;
        float opacity = 0.7f;
        float tint = 0.35f;
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

    // Render the wave to its FBO before ImGui::Render, then append a clipped image.
    static void paint(ImDrawList* drawList, PaintRequest const& request);

private:
    EbDesktopMenuSidebarWaveGpu() = delete;
};

#endif

#endif
