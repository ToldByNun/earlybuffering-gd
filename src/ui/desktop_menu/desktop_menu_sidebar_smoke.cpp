#include "desktop_menu_sidebar_smoke.hpp"

#if defined(EB_DESKTOP_IMGUI)

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
        int const r = static_cast<int>((0.439f + (0.514f - 0.439f) * t) * 255.0f + 0.5f);
        int const g = static_cast<int>((0.306f + (0.376f - 0.306f) * t) * 255.0f + 0.5f);
        return IM_COL32(r, g, 255, 255);
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

void EbDesktopMenuSidebarSmoke::paint(ImDrawList* drawList, ImVec2 const& min, ImVec2 const& max,
                                      float rounding, float timeSeconds, ImDrawFlags cornerFlags) {
    if (!drawList) {
        return;
    }

    float const width = max.x - min.x;
    float const height = max.y - min.y;
    if (width <= 1.0f || height <= 1.0f) {
        return;
    }

    ImU32 const base = EbDesktopMenuTheme::toU32(EbDesktopMenuTheme::sidebarBg());
    ImU32 const outside = EbDesktopMenuTheme::toU32(EbDesktopMenuTheme::bg());

    drawList->AddRectFilled(min, max, base, rounding, cornerFlags);

    constexpr int kCols = 12;
    constexpr int kRows = 32;
    float const cellW = width / static_cast<float>(kCols);
    float const cellH = height / static_cast<float>(kRows);

    ImGui::PushClipRect(min, max, true);
    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < kCols; ++col) {
            float const px = (static_cast<float>(col) + 0.5f) * cellW;
            float const py = (static_cast<float>(row) + 0.5f) * cellH;
            float const wave =
                EbDesktopMenuSidebarSmokeMath::waveAt(px, py, width, height, timeSeconds);
            ImU32 const color = EbDesktopMenuSidebarSmokeMath::colorFromWave(wave);
            ImVec2 const cellMin(min.x + static_cast<float>(col) * cellW,
                                 min.y + static_cast<float>(row) * cellH);
            ImVec2 const cellMax(cellMin.x + cellW + 0.5f, cellMin.y + cellH + 0.5f);
            drawList->AddRectFilled(cellMin, cellMax, color);
        }
    }
    ImGui::PopClipRect();

    EbDesktopMenuSidebarSmokeMath::remaskRoundedCorners(drawList, min, max, rounding, outside,
                                                        timeSeconds, cornerFlags);
}

#else

EbDesktopMenuSidebarSmoke& EbDesktopMenuSidebarSmoke::get() {
    static EbDesktopMenuSidebarSmoke instance;
    return instance;
}

#endif
