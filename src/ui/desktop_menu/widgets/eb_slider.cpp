#include "eb_slider.hpp"
#include "../../../core/platform/ui_capabilities.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include "../desktop_menu_bind.hpp"
#include "../desktop_menu_fonts.hpp"
#include "../desktop_menu_motion.hpp"
#include "../desktop_menu_theme.hpp"
#include "../desktop_menu_tooltip.hpp"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <imgui.h>
#include <imgui_internal.h>
#include <string>

class EbSliderDraw {
public:
    static ImGuiID& editorId() {
        static ImGuiID value = 0;
        return value;
    }
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
                           float* outT, bool enabled, char const* tooltip) {
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
        ImGuiID const sliderId = ImGui::GetID("##eb-slider");
        if (!enabled && ImGui::GetActiveID() == sliderId)
            ImGui::ClearActiveID();
        ImGui::BeginDisabled(!enabled || editorId() == ImGui::GetID("##value"));
        ImGui::InvisibleButton("##eb-slider", ImVec2(avail, totalH));
        ImGui::EndDisabled();
        EbDesktopMenuTooltip::show(tooltip);
        bool const hovered = enabled && ImGui::IsItemHovered();
        ImVec2 const mouse = ImGui::GetIO().MousePos;
        bool const valueTouch = EbUiCapabilities::mobileLayout() &&
                                mouse.x >= origin.x + avail - 96.f && mouse.y >= origin.y &&
                                mouse.y < origin.y + labelH + gap;
        bool const held = enabled && ImGui::IsItemActive() && !valueTouch;
        ImGuiID const itemId = sliderId;
        float const heat =
            enabled ? EbDesktopMenuMotion::smoothToward(itemId, hovered || held, 0.05f) : 0.0f;
        // Visual thumb/fill eases toward the live value (snappier while dragging).
        float const visualT = EbDesktopMenuMotion::smoothFloat(
            itemId ^ 0x51ADu, clampf(t, 0.0f, 1.0f), held ? 0.028f : 0.08f);

        ImVec2 const valueSize = ImGui::CalcTextSize(valueText);
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        ImVec4 labelColor = EbDesktopMenuTheme::muted();
        if (!enabled) {
            labelColor = ImVec4(0.62f, 0.64f, 0.70f, 0.92f);
        } else {
            labelColor = EbDesktopMenuMotion::lerp4(labelColor, EbDesktopMenuTheme::text(), heat);
        }
        // Value stays grey until hover / drag, then matches bright label text.
        ImVec4 valueColor = EbDesktopMenuTheme::muted();
        if (!enabled) {
            valueColor = ImVec4(0.62f, 0.64f, 0.70f, 0.85f);
        } else {
            valueColor = EbDesktopMenuMotion::lerp4(valueColor, EbDesktopMenuTheme::text(), heat);
        }

        drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), origin,
                          EbDesktopMenuTheme::toU32(labelColor), label);
        if (editorId() != ImGui::GetID("##value"))
            drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                              ImVec2(origin.x + avail - valueSize.x, origin.y),
                              EbDesktopMenuTheme::toU32(valueColor), valueText);

        float const trackY = origin.y + labelH + gap + (trackBlockH - kTrackH) * 0.5f;
        ImVec2 const trackMin(origin.x, trackY);
        ImVec2 const trackMax(origin.x + avail, trackY + kTrackH);
        float const trackRounding = kTrackH * 0.5f;

        ImU32 const trackBg = EbDesktopMenuTheme::toU32(EbDesktopMenuTheme::surface());

        float const clampedT = clampf(t, 0.0f, 1.0f);
        float const fillW = avail * visualT;
        float const knobX = trackMin.x + fillW;
        float const knobY = trackMin.y + kTrackH * 0.5f;

        // Empty track, then rounded accent fill (MultiColor has square corners — avoid it).
        drawList->AddRectFilled(trackMin, trackMax, trackBg, trackRounding);
        if (fillW > 0.5f) {
            ImVec2 const fillMax(trackMin.x + fillW, trackMax.y);
            float fillRound = trackRounding;
            float const fillSpan = fillMax.x - trackMin.x;
            if (fillSpan < fillRound * 2.0f) {
                fillRound = fillSpan * 0.5f;
            }
            if (enabled) {
                ImVec4 fill = EbDesktopMenuMotion::lerp4(EbDesktopMenuTheme::accent(),
                                                         EbDesktopMenuTheme::accentLight(), 0.35f);
                drawList->AddRectFilled(trackMin, fillMax, EbDesktopMenuTheme::toU32(fill),
                                        fillRound);
                // Soft top lift inside the rounded fill.
                drawList->PushClipRect(trackMin, fillMax, true);
                ImVec4 lift = EbDesktopMenuTheme::accentLight();
                lift.w = 0.20f;
                drawList->AddRectFilled(trackMin, ImVec2(fillMax.x, trackMin.y + kTrackH * 0.45f),
                                        EbDesktopMenuTheme::toU32(lift), fillRound);
                drawList->PopClipRect();
            } else {
                ImVec4 dim = ImVec4(0.55f, 0.57f, 0.64f, 0.45f);
                drawList->AddRectFilled(trackMin, fillMax, EbDesktopMenuTheme::toU32(dim),
                                        fillRound);
            }
        }
        ImU32 const knobColor =
            enabled ? EbDesktopMenuTheme::toU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f))
                    : EbDesktopMenuTheme::toU32(ImVec4(180.0f / 255.0f, 184.0f / 255.0f,
                                                       196.0f / 255.0f, 220.0f / 255.0f));
        drawList->AddCircleFilled(ImVec2(knobX, knobY), kKnobR, knobColor, 20);
        if (enabled && heat > 0.01f) {
            ImVec4 ring = EbDesktopMenuTheme::accentLight();
            ring.w = (held ? 0.55f : 0.35f) * heat;
            drawList->AddCircle(ImVec2(knobX, knobY), kKnobR + 1.0f + 1.5f * heat,
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

    static bool editMobile(char const* id, char const* label, double current, double minimum,
                           double maximum, bool integer, bool enabled, double* result) {
        ImVec2 const rowMin = ImGui::GetItemRectMin();
        ImVec2 const rowMax = ImGui::GetItemRectMax();
        bool const request = enabled && ImGui::IsItemActivated() &&
                             ImGui::GetIO().MousePos.x >= rowMax.x - 96.f &&
                             ImGui::GetIO().MousePos.y < rowMin.y + ImGui::GetFontSize() + 6.f;
        ImGui::PushID(id);
        ImGuiID const inputId = ImGui::GetID("##value");
        static char input[96] = {};
        static bool invalid = false;
        if (request) {
            editorId() = inputId;
            std::snprintf(input, sizeof(input), integer ? "%.0f" : "%.9g", current);
            invalid = false;
            ImGui::ClearActiveID();
            ImGui::OpenPopup("Edit value");
        }
        bool committed = false;
        auto const display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, 24.f), ImGuiCond_Always, ImVec2(0.5f, 0));
        ImGui::SetNextWindowSize(ImVec2(std::max(180.f, std::min(420.f, display.x - 24.f)), 0));
        if (ImGui::BeginPopupModal("Edit value", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoMove)) {
            EbDesktopMenuFonts::pushUi();
            ImGui::TextWrapped("%s", label);
            ImGui::TextDisabled("Range: %.9g to %.9g", minimum, maximum);
            if (request)
                ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(-1);
            bool const enter = ImGui::InputText("##number", input, sizeof(input),
                                                ImGuiInputTextFlags_EnterReturnsTrue |
                                                    ImGuiInputTextFlags_AutoSelectAll |
                                                    ImGuiInputTextFlags_CharsDecimal);
            if (invalid)
                ImGui::TextWrapped("Enter a finite %s.", integer ? "whole number" : "number");
            float const width = (ImGui::GetContentRegionAvail().x - 12.f) * 0.5f;
            bool const apply = ImGui::Button("Apply", ImVec2(width, 44.f));
            ImGui::SameLine(0, 12.f);
            bool const cancel = ImGui::Button("Cancel", ImVec2(width, 44.f));
            if ((enter || apply) && enabled) {
                errno = 0;
                char* end = nullptr;
                double const parsed = std::strtod(input, &end);
                while (end && std::isspace(static_cast<unsigned char>(*end)))
                    ++end;
                invalid = end == input || !end || *end || errno == ERANGE ||
                          !std::isfinite(parsed) || (integer && std::trunc(parsed) != parsed);
                if (!invalid) {
                    *result = std::clamp(parsed, minimum, maximum);
                    committed = true;
                }
            }
            if (committed || cancel || !enabled || editorId() != inputId ||
                ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                editorId() = 0;
                ImGui::ClearActiveID();
                ImGui::CloseCurrentPopup();
            }
            EbDesktopMenuFonts::pop();
            ImGui::EndPopup();
        }
        ImGui::PopID();
        return committed;
    }

    static bool editValue(char const* id, char const* label, double current, double minimum,
                          double maximum, bool integer, bool enabled, double* result) {
        if (EbUiCapabilities::mobileLayout())
            return editMobile(id, label, current, minimum, maximum, integer, enabled, result);
        ImVec2 const itemMin = ImGui::GetItemRectMin();
        ImVec2 const itemMax = ImGui::GetItemRectMax();
        bool const request =
            enabled && (ImGui::IsItemClicked(ImGuiMouseButton_Right) ||
                        (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter)));
        ImGui::PushID(id);
        ImGuiID const inputId = ImGui::GetID("##value");
        static char input[96] = {};
        static bool invalid = false;
        if (request) {
            editorId() = inputId;
            std::snprintf(input, sizeof(input), integer ? "%.0f" : "%.9g", current);
            invalid = false;
            ImGui::ClearActiveID();
        }
        bool committed = false;
        if (editorId() == inputId) {
            if (!enabled) {
                editorId() = 0;
            } else {
                ImVec2 const cursor = ImGui::GetCursorScreenPos();
                EbDesktopMenuFonts::pushUi();
                float const width = std::min(itemMax.x - itemMin.x,
                                             std::max(16.f, ImGui::CalcTextSize(input).x + 8.f));
                ImGui::SetCursorScreenPos(ImVec2(itemMax.x - width, itemMin.y));
                ImGui::SetNextItemWidth(width);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.f, 0.f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.f);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, EbDesktopMenuTheme::panel());
                if (request)
                    ImGui::SetKeyboardFocusHere();
                bool submit = ImGui::InputText("##value", input, sizeof(input),
                                               ImGuiInputTextFlags_EnterReturnsTrue |
                                                   ImGuiInputTextFlags_AutoSelectAll);
                bool const hovered = ImGui::IsItemHovered();
                ImGui::PopStyleColor();
                ImGui::PopStyleVar(3);
                EbDesktopMenuFonts::pop();
                if (submit) {
                    errno = 0;
                    char* end = nullptr;
                    double parsed = std::strtod(input, &end);
                    while (end && std::isspace(static_cast<unsigned char>(*end)))
                        ++end;
                    invalid = end == input || !end || *end || errno == ERANGE ||
                              !std::isfinite(parsed) || (integer && std::trunc(parsed) != parsed);
                    if (!invalid) {
                        *result = std::clamp(parsed, minimum, maximum);
                        committed = true;
                        editorId() = 0;
                    }
                }
                if (invalid && hovered)
                    ImGui::SetTooltip("Enter a finite %s. Range %.9g to %.9g.",
                                      integer ? "whole number" : "number", minimum, maximum);
                if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                    (!request && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !hovered))
                    editorId() = 0;
                ImGui::SetCursorScreenPos(cursor);
                ImGui::Dummy(ImVec2(0, 0));
            }
        }
        ImGui::PopID();
        return committed;
    }

private:
    EbSliderDraw() = delete;
};

void EbSlider::cancelEditing() {
    if (!EbSliderDraw::editorId())
        return;
    EbSliderDraw::editorId() = 0;
    if (!ImGui::GetCurrentContext())
        return;
    ImGui::ClearActiveID();
    if (EbUiCapabilities::mobileLayout() && ImGui::GetCurrentContext()->OpenPopupStack.Size > 0)
        ImGui::ClosePopupToLevel(0, true);
}

bool EbSlider::drawFloat(char const* id, char const* label, float* value, float minValue,
                         float maxValue, int decimals, char const* suffix, bool enabled,
                         char const* tooltip, float step) {
    if (!value || maxValue <= minValue) {
        return false;
    }
    float const span = maxValue - minValue;
    float t = (*value - minValue) / span;
    char text[64];
    EbSliderDraw::formatFloat(text, sizeof(text), *value, decimals, suffix);
    float newT = t;
    bool dragged = EbSliderDraw::drawChrome(id, label, text, t, &newT, enabled, tooltip);
    double entered = *value;
    bool direct =
        EbSliderDraw::editValue(id, label, *value, minValue, maxValue, false, enabled, &entered);
    if (!dragged && !direct)
        return false;
    float increment =
        step > 0 && std::isfinite(step) ? step : std::pow(10.f, -std::clamp(decimals, 0, 4));
    float next = direct      ? float(entered)
                 : newT >= 1 ? maxValue
                 : newT <= 0
                     ? minValue
                     : std::clamp(minValue + std::round((newT * span) / increment) * increment,
                                  minValue, maxValue);
    if (std::fabs(next - *value) <= 0.00001f) {
        return false;
    }
    *value = next;
    return true;
}

bool EbSlider::drawInt(char const* id, char const* label, int* value, int minValue, int maxValue,
                       char const* suffix, bool enabled, char const* tooltip, int step) {
    if (!value || maxValue <= minValue) {
        return false;
    }
    double const span = double(maxValue) - minValue;
    float t = float((double(*value) - minValue) / span);
    char text[64];
    EbSliderDraw::formatInt(text, sizeof(text), *value, suffix);
    float newT = t;
    bool dragged = EbSliderDraw::drawChrome(id, label, text, t, &newT, enabled, tooltip);
    double entered = *value;
    bool direct =
        EbSliderDraw::editValue(id, label, *value, minValue, maxValue, true, enabled, &entered);
    if (!dragged && !direct)
        return false;
    double increment = std::max(1, step);
    double next = direct      ? entered
                  : newT >= 1 ? maxValue
                  : newT <= 0
                      ? minValue
                      : double(minValue) + std::round((newT * span) / increment) * increment;
    int clamped = static_cast<int>(std::clamp(next, double(minValue), double(maxValue)));
    if (clamped == *value) {
        return false;
    }
    *value = clamped;
    return true;
}

bool EbSlider::drawFloatSetting(char const* key, char const* label, float minValue, float maxValue,
                                int decimals, char const* suffix, bool enabled, float step) {
    if (!key || key[0] == '\0' || !EbDesktopMenuBind::exists(key)) {
        return false;
    }
    float value = static_cast<float>(EbDesktopMenuBind::getFloat(key, minValue));
    std::string const tip = EbDesktopMenuTooltip::descriptionForKey(key);
    if (!drawFloat(key, label, &value, minValue, maxValue, decimals, suffix, enabled, tip.c_str(),
                   step)) {
        return false;
    }
    return EbDesktopMenuBind::setFloat(key, static_cast<double>(value));
}

bool EbSlider::drawIntSetting(char const* key, char const* label, int64_t minValue,
                              int64_t maxValue, char const* suffix, bool enabled, int step) {
    if (!key || key[0] == '\0' || !EbDesktopMenuBind::exists(key)) {
        return false;
    }
    int value = static_cast<int>(EbDesktopMenuBind::getInt(key, minValue));
    int const minI = static_cast<int>(minValue);
    int const maxI = static_cast<int>(maxValue);
    std::string const tip = EbDesktopMenuTooltip::descriptionForKey(key);
    if (!drawInt(key, label, &value, minI, maxI, suffix, enabled, tip.c_str(), step)) {
        return false;
    }
    return EbDesktopMenuBind::setInt(key, static_cast<int64_t>(value));
}

#else

void EbSlider::cancelEditing() {}

bool EbSlider::drawFloat(char const* id, char const* label, float* value, float minValue,
                         float maxValue, int decimals, char const* suffix, bool enabled,
                         char const* tooltip, float step) {
    (void)id;
    (void)label;
    (void)value;
    (void)minValue;
    (void)maxValue;
    (void)decimals;
    (void)suffix;
    (void)enabled;
    (void)tooltip;
    return false;
}

bool EbSlider::drawInt(char const* id, char const* label, int* value, int minValue, int maxValue,
                       char const* suffix, bool enabled, char const* tooltip, int step) {
    (void)id;
    (void)label;
    (void)value;
    (void)minValue;
    (void)maxValue;
    (void)suffix;
    (void)enabled;
    (void)tooltip;
    return false;
}

bool EbSlider::drawFloatSetting(char const* key, char const* label, float minValue, float maxValue,
                                int decimals, char const* suffix, bool enabled, float step) {
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
                              int64_t maxValue, char const* suffix, bool enabled, int step) {
    (void)key;
    (void)label;
    (void)minValue;
    (void)maxValue;
    (void)suffix;
    (void)enabled;
    return false;
}

#endif
