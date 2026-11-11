#include "eb_checkbox.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include "../desktop_menu_bind.hpp"
#include "../desktop_menu_fonts.hpp"
#include "../desktop_menu_motion.hpp"
#include "../desktop_menu_sidebar_smoke.hpp"
#include "../desktop_menu_theme.hpp"
#include <imgui.h>
#include <algorithm>

class EbCheckboxDraw {
public:
    static constexpr float kBoxSize = 18.0f;
    static constexpr float kBoxRounding = 5.0f;

    static ImVec2 lerp2(ImVec2 const& from, ImVec2 const& to, float t) {
        return ImVec2(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t);
    }

    // `progress` 0..1 draws the check stroke in two segments (short leg, then long).
    static void drawCheckMark(ImDrawList* drawList, ImVec2 const& boxMin, float boxSize,
                              ImU32 color, float progress) {
        if (!drawList || progress <= 0.001f) {
            return;
        }
        float const thickness = boxSize * 0.145f;
        float const radius = thickness * 0.5f;
        ImVec2 const a(boxMin.x + boxSize * 0.22f, boxMin.y + boxSize * 0.52f);
        ImVec2 const b(boxMin.x + boxSize * 0.42f, boxMin.y + boxSize * 0.70f);
        ImVec2 const c(boxMin.x + boxSize * 0.78f, boxMin.y + boxSize * 0.30f);
        float const amt = progress > 1.0f ? 1.0f : progress;

        drawList->AddCircleFilled(a, radius, color, 12);
        if (amt < 0.42f) {
            float const t = amt / 0.42f;
            ImVec2 const tip = lerp2(a, b, t);
            drawList->AddLine(a, tip, color, thickness);
            drawList->AddCircleFilled(tip, radius, color, 12);
            return;
        }

        drawList->AddLine(a, b, color, thickness);
        drawList->AddCircleFilled(b, radius, color, 12);
        float const t = (amt - 0.42f) / 0.58f;
        ImVec2 const tip = lerp2(b, c, t);
        drawList->AddLine(b, tip, color, thickness);
        drawList->AddCircleFilled(tip, radius, color, 12);
        if (amt > 0.98f) {
            drawList->AddCircleFilled(c, radius, color, 12);
        }
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
        ImGuiID const itemId = ImGui::GetItemID();
        float const heat =
            enabled ? EbDesktopMenuMotion::smoothToward(itemId, hovered, 0.05f) : 0.0f;
        // Animated check amount — drives fill fade + checkmark scale/alpha.
        float const check =
            EbDesktopMenuMotion::smoothFloat(itemId ^ 0xC4E55u, (*value && enabled) ? 1.0f : 0.0f,
                                             0.07f);
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        ImVec4 labelColor = EbDesktopMenuTheme::text();
        if (!enabled) {
            // Readable dim — muted grey, not near-invisible on panel.
            labelColor = ImVec4(0.62f, 0.64f, 0.70f, 0.92f);
        } else {
            labelColor.w = 0.88f + 0.12f * heat;
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
        } else {
            // Empty surface always underneath; wave fill fades in with `check`.
            drawList->AddRectFilled(boxMin, boxMax, EbDesktopMenuTheme::toU32(EbDesktopMenuTheme::surface()),
                                    kBoxRounding);
            if (check > 0.01f) {
                ImU32 const outside = EbDesktopMenuTheme::toU32(EbDesktopMenuTheme::panel());
                drawList->PushClipRect(boxMin, boxMax, true);
                EbDesktopMenuSidebarSmoke::get().paint(drawList, boxMin, boxMax, kBoxRounding,
                                                       static_cast<float>(ImGui::GetTime()),
                                                       ImDrawFlags_RoundCornersAll, outside);
                // Soft veil so partial check reads as a fade, not a hard cut.
                int const veil = static_cast<int>((1.0f - check) * 200.0f);
                if (veil > 0) {
                    drawList->AddRectFilled(boxMin, boxMax, IM_COL32(22, 22, 33, veil), kBoxRounding);
                }
                drawList->PopClipRect();
                if (heat > 0.01f) {
                    EbDesktopMenuTheme::drawAccentGlow(drawList, boxMin, boxMax, kBoxRounding,
                                                       0.45f * heat * check);
                }
                int const alpha = static_cast<int>((0.55f + 0.45f * check) * 255.0f + 0.5f);
                drawCheckMark(drawList, boxMin, kBoxSize, IM_COL32(255, 255, 255, alpha), check);
            }
            ImVec4 stroke = EbDesktopMenuTheme::outline();
            ImVec4 accent = EbDesktopMenuTheme::accentLight();
            accent.w = 0.75f;
            stroke = EbDesktopMenuMotion::lerp4(stroke, accent, (std::max)(heat, check * 0.5f));
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
