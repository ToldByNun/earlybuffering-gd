#include "eb_button.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include "../desktop_menu_fonts.hpp"
#include "../desktop_menu_motion.hpp"
#include "../desktop_menu_theme.hpp"
#include "../desktop_menu_tooltip.hpp"
#include "../../settings/action_button_setting.hpp"
#include <imgui.h>
#include <algorithm>
#include <string>

class EbButtonRowState {
public:
    static int& count() {
        static int value = 0;
        return value;
    }

    static int& index() {
        static int value = 0;
        return value;
    }

    static float& gap() {
        static float value = 10.0f;
        return value;
    }

    static float& width() {
        static float value = 0.0f;
        return value;
    }

    static bool active() {
        return count() > 0;
    }

private:
    EbButtonRowState() = delete;
};

class EbButtonDraw {
public:
    // Compact chip — matches dropdown/keybind field density inside section cards.
    static constexpr float kPadY = 10.0f;
    static constexpr float kPadX = 14.0f;
    static constexpr float kMinHeight = 34.0f;

    static ImVec4 idleFill() {
        // Lifted above panel() so chips read as controls, not hollow outlines.
        ImVec4 fill = EbDesktopMenuTheme::surface();
        ImVec4 lift(0.17f, 0.17f, 0.22f, 1.0f);
        return EbDesktopMenuMotion::lerp4(fill, lift, 0.55f);
    }

    static ImVec4 hoverFill() {
        return ImVec4(0.22f, 0.22f, 0.30f, 1.0f);
    }

    static ImVec4 heldFill() {
        return ImVec4(0.28f, 0.28f, 0.36f, 1.0f);
    }

    static float resolveWidth() {
        if (EbButtonRowState::active()) {
            return EbButtonRowState::width();
        }
        return ImGui::GetContentRegionAvail().x;
    }

    static void advanceRowSlot() {
        if (!EbButtonRowState::active()) {
            return;
        }
        int& index = EbButtonRowState::index();
        index += 1;
        if (index < EbButtonRowState::count()) {
            ImGui::SameLine(0.0f, EbButtonRowState::gap());
        }
    }

    static bool drawChrome(char const* id, char const* label, bool enabled, char const* tooltip) {
        if (!id || id[0] == '\0' || !label) {
            return false;
        }

        ImGui::PushID(id);
        EbDesktopMenuFonts::pushUi();

        ImVec2 const textSize = ImGui::CalcTextSize(label);
        float const height = (std::max)(kMinHeight, textSize.y + kPadY * 2.0f);
        float const width = resolveWidth();
        float const rounding = EbDesktopMenuTheme::frameRounding();

        ImVec2 const min = ImGui::GetCursorScreenPos();
        ImVec2 const max(min.x + width, min.y + height);

        bool pressed = false;
        if (enabled) {
            pressed = ImGui::InvisibleButton("##eb-btn", ImVec2(width, height));
        } else {
            ImGui::Dummy(ImVec2(width, height));
        }
        EbDesktopMenuTooltip::show(tooltip);
        bool const hovered = enabled && ImGui::IsItemHovered();
        bool const held = enabled && ImGui::IsItemActive();
        float const heat =
            enabled ? EbDesktopMenuMotion::smoothToward(ImGui::GetItemID(), hovered || held, 0.05f)
                    : 0.0f;
        float const press =
            enabled ? EbDesktopMenuMotion::smoothToward(ImGui::GetItemID() ^ 0x9E3779B9u, held, 0.04f)
                    : 0.0f;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        // Custom draw-list paint ignores ImGuiStyleVar_Alpha unless we apply it.
        float const styleAlpha = ImGui::GetStyle().Alpha;

        if (enabled && heat > 0.02f && styleAlpha > 0.01f) {
            EbDesktopMenuTheme::drawAccentGlow(drawList, min, max, rounding,
                                               0.28f * heat * styleAlpha);
        }

        ImVec4 fill = idleFill();
        if (!enabled) {
            fill.w = 0.72f;
        } else {
            ImVec4 accentWash = EbDesktopMenuTheme::accent();
            accentWash.w = 0.16f;
            fill = EbDesktopMenuMotion::lerp4(fill, hoverFill(), heat);
            fill = EbDesktopMenuMotion::lerp4(fill, accentWash, heat * 0.50f);
            fill = EbDesktopMenuMotion::lerp4(fill, heldFill(), press);
        }
        fill.w *= styleAlpha;
        drawList->AddRectFilled(min, max, EbDesktopMenuTheme::toU32(fill), rounding);

        // Soft top edge — cheap depth without a second box.
        if (enabled) {
            ImVec4 tip(1.0f, 1.0f, 1.0f, (0.05f + 0.04f * heat) * styleAlpha);
            drawList->AddRectFilled(ImVec2(min.x + 1.0f, min.y + 1.0f),
                                    ImVec2(max.x - 1.0f, min.y + 2.0f),
                                    EbDesktopMenuTheme::toU32(tip), rounding);
        }

        ImVec4 stroke = EbDesktopMenuTheme::outline();
        stroke.w = enabled ? 0.78f : 0.42f;
        if (enabled) {
            ImVec4 accent = EbDesktopMenuTheme::accentLight();
            accent.w = 0.45f + 0.45f * heat + 0.10f * press;
            stroke = EbDesktopMenuMotion::lerp4(stroke, accent, heat);
        }
        stroke.w *= styleAlpha;
        drawList->AddRect(min, max, EbDesktopMenuTheme::toU32(stroke), rounding, 0,
                          EbDesktopMenuTheme::outlineThickness());

        ImVec4 textColor = EbDesktopMenuTheme::text();
        if (!enabled) {
            textColor = ImVec4(0.62f, 0.64f, 0.70f, 0.85f);
        } else {
            ImVec4 muted = EbDesktopMenuTheme::muted();
            muted.w = 0.95f;
            textColor = EbDesktopMenuMotion::lerp4(muted, textColor, 0.72f + 0.28f * heat);
        }
        textColor.w *= styleAlpha;
        float const textMaxW = (std::max)(0.0f, width - kPadX * 2.0f);
        float textDrawW = textSize.x > textMaxW ? textMaxW : textSize.x;
        ImVec2 const textPos(min.x + (width - textDrawW) * 0.5f,
                             min.y + (height - textSize.y) * 0.5f);
        ImVec4 const clip(min.x + kPadX, min.y, max.x - kPadX, max.y);
        drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), textPos,
                          EbDesktopMenuTheme::toU32(textColor), label, nullptr, 0.0f, &clip);

        EbDesktopMenuFonts::pop();
        ImGui::PopID();
        advanceRowSlot();
        return pressed;
    }

private:
    EbButtonDraw() = delete;
};

bool EbButton::draw(char const* id, char const* label, bool enabled, char const* tooltip) {
    return EbButtonDraw::drawChrome(id, label, enabled, tooltip);
}

bool EbButton::drawAction(char const* actionKey, char const* fallbackLabel) {
    if (!actionKey || actionKey[0] == '\0') {
        return false;
    }
    std::string const fallback = fallbackLabel ? fallbackLabel : "Go";
    std::string const label =
        EbActionButtonSettingImplementation::dynamicButtonLabel(actionKey, fallback);
    bool const enabled = EbActionButtonSettingImplementation::actionShouldEnable(actionKey);
    std::string const tip = EbDesktopMenuTooltip::descriptionForKey(actionKey);
    if (!EbButtonDraw::drawChrome(actionKey, label.c_str(), enabled, tip.c_str())) {
        return false;
    }
    EbActionButtonSettingImplementation::fireSettingAction(actionKey);
    return true;
}

void EbButton::beginRow(int buttonCount, float gapPx) {
    if (EbButtonRowState::active() || buttonCount < 1) {
        return;
    }
    // Match section ItemSpacing so H-gap == V-gap between rows (no extra Dummy).
    float gap = gapPx;
    if (gap < 0.0f) {
        gap = ImGui::GetStyle().ItemSpacing.x;
    }
    float const avail = ImGui::GetContentRegionAvail().x;
    float const totalGap = gap * static_cast<float>(buttonCount - 1);
    float width = (avail - totalGap) / static_cast<float>(buttonCount);
    if (width < 1.0f) {
        width = 1.0f;
    }
    EbButtonRowState::count() = buttonCount;
    EbButtonRowState::index() = 0;
    EbButtonRowState::gap() = gap;
    EbButtonRowState::width() = width;
}

void EbButton::endRow() {
    if (!EbButtonRowState::active()) {
        return;
    }
    while (EbButtonRowState::index() < EbButtonRowState::count()) {
        ImGui::Dummy(ImVec2(EbButtonRowState::width(), 0.0f));
        EbButtonDraw::advanceRowSlot();
    }
    EbButtonRowState::count() = 0;
    EbButtonRowState::index() = 0;
    EbButtonRowState::width() = 0.0f;
}

#else

bool EbButton::draw(char const* id, char const* label, bool enabled, char const* tooltip) {
    (void)id;
    (void)label;
    (void)enabled;
    (void)tooltip;
    return false;
}

bool EbButton::drawAction(char const* actionKey, char const* fallbackLabel) {
    (void)actionKey;
    (void)fallbackLabel;
    return false;
}

void EbButton::beginRow(int, float) {}

void EbButton::endRow() {}

#endif
