#include "eb_slider.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include "../desktop_menu_bind.hpp"
#include "../desktop_menu_fonts.hpp"
#include "../desktop_menu_sidebar_smoke.hpp"
#include "../desktop_menu_theme.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

class EbSliderDraw {
public:
    static constexpr float kTrackH = 10.0f;
    static constexpr float kKnobR = 8.0f;

    static float clampf(float value, float lo, float hi) {
        if (value < lo) {
            return lo;
        }
        if (value > hi) {
            return hi;
        }
        return value;
    }

    static void formatFloat(char* out, size_t outSize, float value, int decimals,
                            char const* suffix) {
        if (!out || outSize == 0) {
            return;
        }
        int const dec = decimals < 0 ? 0 : (decimals > 4 ? 4 : decimals);
        if (suffix && suffix[0] != '\0') {
            std::snprintf(out, outSize, "%.*f%s", dec, static_cast<double>(value), suffix);
        } else {
            std::snprintf(out, outSize, "%.*f", dec, static_cast<double>(value));
        }
    }

    static void formatInt(char* out, size_t outSize, int value, char const* suffix) {
        if (!out || outSize == 0) {
            return;
        }
        if (suffix && suffix[0] != '\0') {
            std::snprintf(out, outSize, "%d%s", value, suffix);
        } else {
            std::snprintf(out, outSize, "%d", value);
        }
    }

    // Shared chrome: t in [0,1]. Returns true if the user dragged to a new t.
    static bool drawChrome(char const* id, char const* label, char const* valueText, float t,
                           float* outT, bool enabled) {
        if (!id || id[0] == '\0' || !label || !valueText || !outT) {
            return false;
        }

        ImGui::PushID(id);
        EbDesktopMenuFonts::pushUi();

        float const avail = ImGui::GetContentRegionAvail().x;
        float const labelH = ImGui::GetFontSize();
        float const gap = 6.0f;
        float const trackBlockH = (std::max)(kKnobR * 2.0f, kTrackH + 4.0f);
        float const totalH = labelH + gap + trackBlockH;

        ImVec2 const origin = ImGui::GetCursorScreenPos();
        if (enabled) {
            ImGui::InvisibleButton("##eb-slider", ImVec2(avail, totalH));
        } else {
            ImGui::Dummy(ImVec2(avail, totalH));
        }
        bool const hovered = enabled && ImGui::IsItemHovered();
        bool const held = enabled && ImGui::IsItemActive();

        ImVec2 const valueSize = ImGui::CalcTextSize(valueText);
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        ImVec4 labelColor = EbDesktopMenuTheme::muted();
        if (!enabled) {
            labelColor = ImVec4(0.62f, 0.64f, 0.70f, 0.92f);
        } else if (hovered || held) {
            labelColor = EbDesktopMenuTheme::text();
        }
        // Value stays grey until hover / drag, then matches bright label text.
        ImVec4 valueColor = EbDesktopMenuTheme::muted();
        if (!enabled) {
            valueColor = ImVec4(0.62f, 0.64f, 0.70f, 0.85f);
        } else if (hovered || held) {
            valueColor = EbDesktopMenuTheme::text();
        }

        drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), origin,
                          EbDesktopMenuTheme::toU32(labelColor), label);
        drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                          ImVec2(origin.x + avail - valueSize.x, origin.y),
                          EbDesktopMenuTheme::toU32(valueColor), valueText);

        float const trackY = origin.y + labelH + gap + (trackBlockH - kTrackH) * 0.5f;
        ImVec2 const trackMin(origin.x, trackY);
        ImVec2 const trackMax(origin.x + avail, trackY + kTrackH);
        float const trackRounding = kTrackH * 0.5f;

        ImU32 const trackBg = EbDesktopMenuTheme::toU32(EbDesktopMenuTheme::surface());
        ImU32 const outside = EbDesktopMenuTheme::toU32(EbDesktopMenuTheme::panel());

        float const clampedT = clampf(t, 0.0f, 1.0f);
        float const fillW = avail * clampedT;
        float const knobX = trackMin.x + fillW;
        float const knobY = trackMin.y + kTrackH * 0.5f;

        // Empty track first, then full-width wave clipped to the fill — stable UVs,
        // no hairline leak past the knob (mask/hide left AA gaps before).
        drawList->AddRectFilled(trackMin, trackMax, trackBg, trackRounding);
        if (enabled && fillW > 0.5f) {
            ImGui::PushClipRect(ImVec2(trackMin.x - 1.0f, trackMin.y - 1.0f),
                                ImVec2(knobX + 0.5f, trackMax.y + 1.0f), true);
            EbDesktopMenuSidebarSmoke::get().paint(
                drawList, trackMin, trackMax, trackRounding, static_cast<float>(ImGui::GetTime()),
                ImDrawFlags_RoundCornersAll, outside);
            // Slight lift so fill reads brighter than the panel / empty track.
            drawList->AddRectFilled(trackMin, trackMax, IM_COL32(220, 210, 255, 48), trackRounding);
            ImGui::PopClipRect();
        } else if (!enabled && fillW > 1.5f) {
            ImVec4 dim = ImVec4(0.55f, 0.57f, 0.64f, 0.45f);
            drawList->AddRectFilled(trackMin, ImVec2(trackMin.x + fillW, trackMax.y),
                                    EbDesktopMenuTheme::toU32(dim), trackRounding);
        }
        ImU32 const knobColor =
            enabled ? IM_COL32(255, 255, 255, 255) : IM_COL32(180, 184, 196, 220);
        drawList->AddCircleFilled(ImVec2(knobX, knobY), kKnobR, knobColor, 20);
        if (enabled && (hovered || held)) {
            ImVec4 ring = EbDesktopMenuTheme::accentLight();
            ring.w = held ? 0.55f : 0.35f;
            drawList->AddCircle(ImVec2(knobX, knobY), kKnobR + 2.0f,
                                EbDesktopMenuTheme::toU32(ring), 20, 1.5f);
        }

        bool changed = false;
        if (held) {
            float const mouseX = ImGui::GetIO().MousePos.x;
            float const newT = clampf((mouseX - trackMin.x) / avail, 0.0f, 1.0f);
            if (std::fabs(newT - clampedT) > 0.00001f) {
                *outT = newT;
                changed = true;
            } else {
                *outT = clampedT;
            }
        } else {
            *outT = clampedT;
        }

        EbDesktopMenuFonts::pop();
        ImGui::PopID();
        return changed;
    }

private:
    EbSliderDraw() = delete;
};

bool EbSlider::drawFloat(char const* id, char const* label, float* value, float minValue,
                         float maxValue, int decimals, char const* suffix, bool enabled) {
    if (!value || maxValue <= minValue) {
        return false;
    }
    float const span = maxValue - minValue;
    float t = (*value - minValue) / span;
    char text[64];
    EbSliderDraw::formatFloat(text, sizeof(text), *value, decimals, suffix);
    float newT = t;
    if (!EbSliderDraw::drawChrome(id, label, text, t, &newT, enabled)) {
        return false;
    }
    float const next = minValue + newT * span;
    if (std::fabs(next - *value) <= 0.00001f) {
        return false;
    }
    *value = next;
    return true;
}

bool EbSlider::drawInt(char const* id, char const* label, int* value, int minValue, int maxValue,
                       char const* suffix, bool enabled) {
    if (!value || maxValue <= minValue) {
        return false;
    }
    float const span = static_cast<float>(maxValue - minValue);
    float t = static_cast<float>(*value - minValue) / span;
    char text[64];
    EbSliderDraw::formatInt(text, sizeof(text), *value, suffix);
    float newT = t;
    if (!EbSliderDraw::drawChrome(id, label, text, t, &newT, enabled)) {
        return false;
    }
    int const next = minValue + static_cast<int>(std::lround(newT * span));
    int const clamped = next < minValue ? minValue : (next > maxValue ? maxValue : next);
    if (clamped == *value) {
        return false;
    }
    *value = clamped;
    return true;
}

bool EbSlider::drawFloatSetting(char const* key, char const* label, float minValue, float maxValue,
                                int decimals, char const* suffix, bool enabled) {
    if (!key || key[0] == '\0' || !EbDesktopMenuBind::exists(key)) {
        return false;
    }
    float value = static_cast<float>(EbDesktopMenuBind::getFloat(key, minValue));
    if (!drawFloat(key, label, &value, minValue, maxValue, decimals, suffix, enabled)) {
        return false;
    }
    return EbDesktopMenuBind::setFloat(key, static_cast<double>(value));
}

bool EbSlider::drawIntSetting(char const* key, char const* label, int64_t minValue,
                              int64_t maxValue, char const* suffix, bool enabled) {
    if (!key || key[0] == '\0' || !EbDesktopMenuBind::exists(key)) {
        return false;
    }
    int value = static_cast<int>(EbDesktopMenuBind::getInt(key, minValue));
    int const minI = static_cast<int>(minValue);
    int const maxI = static_cast<int>(maxValue);
    if (!drawInt(key, label, &value, minI, maxI, suffix, enabled)) {
        return false;
    }
    return EbDesktopMenuBind::setInt(key, static_cast<int64_t>(value));
}

#else

bool EbSlider::drawFloat(char const* id, char const* label, float* value, float minValue,
                         float maxValue, int decimals, char const* suffix, bool enabled) {
    (void)id;
    (void)label;
    (void)value;
    (void)minValue;
    (void)maxValue;
    (void)decimals;
    (void)suffix;
    (void)enabled;
    return false;
}

bool EbSlider::drawInt(char const* id, char const* label, int* value, int minValue, int maxValue,
                       char const* suffix, bool enabled) {
    (void)id;
    (void)label;
    (void)value;
    (void)minValue;
    (void)maxValue;
    (void)suffix;
    (void)enabled;
    return false;
}

bool EbSlider::drawFloatSetting(char const* key, char const* label, float minValue, float maxValue,
                                int decimals, char const* suffix, bool enabled) {
    (void)key;
    (void)label;
    (void)minValue;
    (void)maxValue;
    (void)decimals;
    (void)suffix;
    (void)enabled;
    return false;
}

bool EbSlider::drawIntSetting(char const* key, char const* label, int64_t minValue,
                              int64_t maxValue, char const* suffix, bool enabled) {
    (void)key;
    (void)label;
    (void)minValue;
    (void)maxValue;
    (void)suffix;
    (void)enabled;
    return false;
}

#endif
