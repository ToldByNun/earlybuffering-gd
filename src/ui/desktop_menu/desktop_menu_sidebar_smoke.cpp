#include "desktop_menu_sidebar_smoke.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include "desktop_menu_sidebar_wave_gpu.hpp"
#include "desktop_menu_theme.hpp"
#include <cmath>

class EbDesktopMenuSidebarSmokeMath {
public:
    // Same wave as shaders/sidebar_wave.frag (CPU sample for ImDrawList).
    static float waveAt(float px, float py, float resX, float resY, float timeSeconds) {
        float const minRes = resX < resY ? resX : resY;
        float uvx = (2.0f * px - resX) / minRes;
        float uvy = (2.0f * py - resY) / minRes;
        for (int i = 1; i < 6; ++i) {
            float const fi = static_cast<float>(i);
            uvx += 0.6f / fi * std::cos(fi * 2.5f * uvy + (timeSeconds / 18.0f));
            uvy += 0.6f / fi * std::cos(fi * 2.5f * uvx + (timeSeconds / 18.0f));
        }
        return 0.5f + 0.5f * std::sin(timeSeconds - uvy - uvx);
    }

    static ImU32 colorFromWave(float wave) {
        float const t = wave < 0.0f ? 0.0f : (wave > 1.0f ? 1.0f : wave);
        ImVec4 const dark = EbDesktopMenuTheme::sidebarWaveDark();
        ImVec4 const light = EbDesktopMenuTheme::sidebarWaveLight();
        ImVec4 color(dark.x + (light.x - dark.x) * t, dark.y + (light.y - dark.y) * t,
                     dark.z + (light.z - dark.z) * t, 1.0f);
        return EbDesktopMenuTheme::toU32(color);
    }

    static constexpr float kPi = 3.14159265358979323846f;

    static void fillCornerDisc(ImDrawList* drawList, ImVec2 const& center, float radius,
                               float sampleX, float sampleY, float width, float height,
                               float timeSeconds, float a0, float a1) {
        float const wave = waveAt(sampleX, sampleY, width, height, timeSeconds);
        ImU32 const insideColor = colorFromWave(wave);
        drawList->PathClear();
        drawList->PathArcTo(center, radius, a0, a1, 16);
        drawList->PathLineTo(center);
        drawList->PathFillConvex(insideColor);
    }

    // Square wave cells overwrite AA rounding — remask only the requested corners.
    static void remaskRoundedCorners(ImDrawList* drawList, ImVec2 const& min, ImVec2 const& max,
                                     float rounding, ImU32 outsideColor, float timeSeconds,
                                     ImDrawFlags cornerFlags) {
        float const r = rounding;
        if (r <= 0.5f) {
            return;
        }

        float const width = max.x - min.x;
        float const height = max.y - min.y;
        bool const topLeft = (cornerFlags & ImDrawFlags_RoundCornersTopLeft) != 0;
        bool const topRight = (cornerFlags & ImDrawFlags_RoundCornersTopRight) != 0;
        bool const bottomLeft = (cornerFlags & ImDrawFlags_RoundCornersBottomLeft) != 0;
        bool const bottomRight = (cornerFlags & ImDrawFlags_RoundCornersBottomRight) != 0;

        if (topLeft) {
            drawList->AddRectFilled(ImVec2(min.x, min.y), ImVec2(min.x + r, min.y + r),
                                    outsideColor);
            fillCornerDisc(drawList, ImVec2(min.x + r, min.y + r), r, r * 0.35f, r * 0.35f, width,
                           height, timeSeconds, kPi, kPi * 1.5f);
        }
        if (topRight) {
            drawList->AddRectFilled(ImVec2(max.x - r, min.y), ImVec2(max.x, min.y + r),
                                    outsideColor);
            fillCornerDisc(drawList, ImVec2(max.x - r, min.y + r), r, width - r * 0.35f, r * 0.35f,
                           width, height, timeSeconds, kPi * 1.5f, kPi * 2.0f);
        }
        if (bottomRight) {
            drawList->AddRectFilled(ImVec2(max.x - r, max.y - r), ImVec2(max.x, max.y),
                                    outsideColor);
            fillCornerDisc(drawList, ImVec2(max.x - r, max.y - r), r, width - r * 0.35f,
                           height - r * 0.35f, width, height, timeSeconds, 0.0f, kPi * 0.5f);
        }
        if (bottomLeft) {
            drawList->AddRectFilled(ImVec2(min.x, max.y - r), ImVec2(min.x + r, max.y),
                                    outsideColor);
            fillCornerDisc(drawList, ImVec2(min.x + r, max.y - r), r, r * 0.35f, height - r * 0.35f,
                           width, height, timeSeconds, kPi * 0.5f, kPi);
        }
    }

private:
    EbDesktopMenuSidebarSmokeMath() = delete;
};

EbDesktopMenuSidebarSmoke& EbDesktopMenuSidebarSmoke::get() {
    static EbDesktopMenuSidebarSmoke instance;
    return instance;
}

ImU32 EbDesktopMenuSidebarSmoke::sampleColor(float timeSeconds, float px, float py) {
    float const wave = EbDesktopMenuSidebarSmokeMath::waveAt(px, py, 1.0f, 1.0f, timeSeconds);
    return EbDesktopMenuSidebarSmokeMath::colorFromWave(wave);
}

void EbDesktopMenuSidebarSmoke::paint(ImDrawList* drawList, ImVec2 const& min, ImVec2 const& max,
                                      float rounding, float timeSeconds, ImDrawFlags cornerFlags,
                                      ImU32 outsideColor, float targetCellPx) {
    if (!drawList) {
        return;
    }

    float const width = max.x - min.x;
    float const height = max.y - min.y;
    if (width <= 1.0f || height <= 1.0f) {
        return;
    }

    // pillRounding() is 999 — clamp so remask corner discs cannot flood the shell.
    float const maxRounding = (width < height ? width : height) * 0.5f;
    float const clampedRounding = rounding > maxRounding ? maxRounding : rounding;
    // 0 = shell dock: keep AA cutouts transparent (don't punch opaque bg into them).
    bool const transparentOutside = outsideColor == 0;

    ImU32 const base = EbDesktopMenuTheme::toU32(EbDesktopMenuTheme::sidebarBg());
    drawList->AddRectFilled(min, max, base, clampedRounding, cornerFlags);

    // GPU path (sidebar): fragment shader into current FB via ImDrawCallback.
    // No custom FBO — that previously crashed on the ImGui swap path.
    if (transparentOutside && EbDesktopMenuSidebarWaveGpu::ensureReady()) {
        EbDesktopMenuSidebarWaveGpu::PaintRequest request;
        request.min = min;
        request.max = max;
        request.timeSeconds = timeSeconds;
        request.rounding = clampedRounding;
        request.cornerFlags = cornerFlags;
        request.baseColor = EbDesktopMenuTheme::sidebarWaveDark();
        request.lightColor = EbDesktopMenuTheme::sidebarWaveLight();
        EbDesktopMenuSidebarWaveGpu::paint(drawList, request);
        return;
    }

    // Caller picks density: sidebar ~2.25 (smooth), widgets ~5–6 (chunky / cheap).
    float const targetCell = targetCellPx < 1.5f ? 1.5f : targetCellPx;
    int cols = static_cast<int>(width / targetCell + 0.5f);
    int rows = static_cast<int>(height / targetCell + 0.5f);
    if (cols < 4) {
        cols = 4;
    }
    if (rows < 3) {
        rows = 3;
    }
    if (cols > 128) {
        cols = 128;
    }
    if (rows > 280) {
        rows = 280;
    }
    float const cellW = width / static_cast<float>(cols);
    float const cellH = height / static_cast<float>(rows);

    bool const roundTopLeft = (cornerFlags & ImDrawFlags_RoundCornersTopLeft) != 0;
    bool const roundTopRight = (cornerFlags & ImDrawFlags_RoundCornersTopRight) != 0;
    bool const roundBottomLeft = (cornerFlags & ImDrawFlags_RoundCornersBottomLeft) != 0;
    bool const roundBottomRight = (cornerFlags & ImDrawFlags_RoundCornersBottomRight) != 0;

    // Draw-list clip only — ImGui::PushClipRect also writes window->ClipRect and
    // desyncs hit-testing when callers mix drawList pushes (checkbox/slider).
    // Max is exclusive — +1 so the bottom/right pill edge is filled.
    drawList->PushClipRect(min, ImVec2(max.x + 1.0f, max.y + 1.0f), true);
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            float const px = (static_cast<float>(col) + 0.5f) * cellW;
            float const py = (static_cast<float>(row) + 0.5f) * cellH;
            ImVec2 const cellMin(min.x + static_cast<float>(col) * cellW,
                                 min.y + static_cast<float>(row) * cellH);
            // Tiny overlap kills hairline gaps without reintroducing chunky seams.
            ImVec2 const cellMax(cellMin.x + cellW + 0.35f, cellMin.y + cellH + 0.35f);

            // Skip cells that sit outside the rounded AA edge — otherwise the
            // square grid fills the transparent shell cutouts (sharp purple corners).
            if (transparentOutside && clampedRounding > 0.5f) {
                float const cx = (cellMin.x + cellMax.x) * 0.5f;
                float const cy = (cellMin.y + cellMax.y) * 0.5f;
                float const r = clampedRounding;
                float const r2 = r * r;
                bool outside = false;
                if (roundTopLeft && cx < min.x + r && cy < min.y + r) {
                    float const dx = (min.x + r) - cx;
                    float const dy = (min.y + r) - cy;
                    outside = dx * dx + dy * dy > r2;
                } else if (roundTopRight && cx > max.x - r && cy < min.y + r) {
                    float const dx = cx - (max.x - r);
                    float const dy = (min.y + r) - cy;
                    outside = dx * dx + dy * dy > r2;
                } else if (roundBottomLeft && cx < min.x + r && cy > max.y - r) {
                    float const dx = (min.x + r) - cx;
                    float const dy = cy - (max.y - r);
                    outside = dx * dx + dy * dy > r2;
                } else if (roundBottomRight && cx > max.x - r && cy > max.y - r) {
                    float const dx = cx - (max.x - r);
                    float const dy = cy - (max.y - r);
                    outside = dx * dx + dy * dy > r2;
                }
                if (outside) {
                    continue;
                }
            }

            float const wave =
                EbDesktopMenuSidebarSmokeMath::waveAt(px, py, width, height, timeSeconds);
            ImU32 const color = EbDesktopMenuSidebarSmokeMath::colorFromWave(wave);
            drawList->AddRectFilled(cellMin, cellMax, color);
        }
    }
    drawList->PopClipRect();

    if (!transparentOutside) {
        EbDesktopMenuSidebarSmokeMath::remaskRoundedCorners(
            drawList, min, max, clampedRounding, outsideColor, timeSeconds, cornerFlags);
    }
}

#else

EbDesktopMenuSidebarSmoke& EbDesktopMenuSidebarSmoke::get() {
    static EbDesktopMenuSidebarSmoke instance;
    return instance;
}

#endif
