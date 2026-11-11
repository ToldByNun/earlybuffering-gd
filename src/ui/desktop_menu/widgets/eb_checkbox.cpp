#include "eb_checkbox.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include "../desktop_menu_bind.hpp"
#include "../desktop_menu_fonts.hpp"
#include "../desktop_menu_sidebar_smoke.hpp"
#include "../desktop_menu_theme.hpp"
#include <imgui.h>
#include <algorithm>

class EbCheckboxDraw {
public:
    static constexpr float kBoxSize = 18.0f;
    static constexpr float kBoxRounding = 5.0f;

    static void drawCheckMark(ImDrawList* drawList, ImVec2 const& boxMin, float boxSize,
                              ImU32 color) {
        if (!drawList) {
            return;
        }
        // Soft check: thick AA stroke + round caps/joint (no sharp V).
        float const thickness = boxSize * 0.145f;
        float const radius = thickness * 0.5f;
        ImVec2 const a(boxMin.x + boxSize * 0.22f, boxMin.y + boxSize * 0.52f);
        ImVec2 const b(boxMin.x + boxSize * 0.42f, boxMin.y + boxSize * 0.70f);
        ImVec2 const c(boxMin.x + boxSize * 0.78f, boxMin.y + boxSize * 0.30f);

        drawList->PathClear();
        drawList->PathLineTo(a);
        drawList->PathLineTo(b);
        drawList->PathLineTo(c);
        drawList->PathStroke(color, ImDrawFlags_None, thickness);
        drawList->AddCircleFilled(a, radius, color, 12);
        drawList->AddCircleFilled(b, radius, color, 12);
        drawList->AddCircleFilled(c, radius, color, 12);
    }

    static bool drawRow(char const* id, char const* label, bool* value, bool enabled) {
        if (!id || id[0] == '\0' || !label || !value) {
            return false;
        }

        ImGui::PushID(id);
        EbDesktopMenuFonts::pushUi();

        float const rowH = (std::max)(ImGui::GetFontSize() + 10.0f, kBoxSize + 4.0f);
        float const avail = ImGui::GetContentRegionAvail().x;
        ImVec2 const rowMin = ImGui::GetCursorScreenPos();

        bool changed = false;
        if (enabled) {
            if (ImGui::InvisibleButton("##eb-check-row", ImVec2(avail, rowH))) {
                *value = !*value;
                changed = true;
            }
        } else {
            ImGui::Dummy(ImVec2(avail, rowH));
        }

        bool const hovered = enabled && ImGui::IsItemHovered();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        ImVec4 labelColor = EbDesktopMenuTheme::text();
        if (!enabled) {
            // Readable dim — muted grey, not near-invisible on panel.
            labelColor = ImVec4(0.62f, 0.64f, 0.70f, 0.92f);
        } else if (hovered) {
            labelColor.w = 0.95f;
        }
        drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                          ImVec2(rowMin.x, rowMin.y + (rowH - ImGui::GetFontSize()) * 0.5f),
                          EbDesktopMenuTheme::toU32(labelColor), label);

        ImVec2 const boxMin(rowMin.x + avail - kBoxSize,
                            rowMin.y + (rowH - kBoxSize) * 0.5f);
        ImVec2 const boxMax(boxMin.x + kBoxSize, boxMin.y + kBoxSize);

        if (!enabled) {
            ImVec4 ghost = EbDesktopMenuTheme::surface();
            ghost.w = 0.95f;
            drawList->AddRectFilled(boxMin, boxMax, EbDesktopMenuTheme::toU32(ghost), kBoxRounding);
            ImVec4 stroke = ImVec4(0.55f, 0.57f, 0.64f, 0.75f);
            drawList->AddRect(boxMin, boxMax, EbDesktopMenuTheme::toU32(stroke), kBoxRounding, 0,
                              EbDesktopMenuTheme::outlineThickness());
        } else if (*value) {
            // Same wave as the sidebar — not flat accent.
            ImU32 const outside = EbDesktopMenuTheme::toU32(EbDesktopMenuTheme::panel());
            EbDesktopMenuSidebarSmoke::get().paint(drawList, boxMin, boxMax, kBoxRounding,
                                                   static_cast<float>(ImGui::GetTime()),
                                                   ImDrawFlags_RoundCornersAll, outside);
            if (hovered) {
                EbDesktopMenuTheme::drawAccentGlow(drawList, boxMin, boxMax, kBoxRounding, 0.45f);
            }
            drawCheckMark(drawList, boxMin, kBoxSize, IM_COL32(255, 255, 255, 255));
        } else {
            ImU32 const fill = EbDesktopMenuTheme::toU32(EbDesktopMenuTheme::surface());
            drawList->AddRectFilled(boxMin, boxMax, fill, kBoxRounding);
            ImVec4 stroke = EbDesktopMenuTheme::outline();
            if (hovered) {
                stroke = EbDesktopMenuTheme::accentLight();
                stroke.w = 0.75f;
            }
            drawList->AddRect(boxMin, boxMax, EbDesktopMenuTheme::toU32(stroke), kBoxRounding, 0,
                              EbDesktopMenuTheme::outlineThickness());
        }

        EbDesktopMenuFonts::pop();
        ImGui::PopID();
        return changed;
    }

private:
    EbCheckboxDraw() = delete;
};

bool EbCheckbox::draw(char const* id, char const* label, bool* value, bool enabled) {
    return EbCheckboxDraw::drawRow(id, label, value, enabled);
}

bool EbCheckbox::drawSetting(char const* key, char const* label, bool enabled) {
    if (!key || key[0] == '\0' || !label) {
        return false;
    }
    if (!EbDesktopMenuBind::exists(key)) {
        return false;
    }
    bool value = EbDesktopMenuBind::getBool(key, false);
    if (!EbCheckboxDraw::drawRow(key, label, &value, enabled)) {
        return false;
    }
    return EbDesktopMenuBind::setBool(key, value);
}

#else

bool EbCheckbox::draw(char const* id, char const* label, bool* value, bool enabled) {
    (void)id;
    (void)label;
    (void)value;
    (void)enabled;
    return false;
}

bool EbCheckbox::drawSetting(char const* key, char const* label, bool enabled) {
    (void)key;
    (void)label;
    (void)enabled;
    return false;
}

#endif
