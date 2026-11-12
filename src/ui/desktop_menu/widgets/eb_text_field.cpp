#include "eb_text_field.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include "../desktop_menu_fonts.hpp"
#include "../desktop_menu_motion.hpp"
#include "../desktop_menu_theme.hpp"
#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <vector>

class EbTextFieldDraw {
public:
    static constexpr float kPadX = 12.0f;
    static constexpr float kPadY = 8.0f;
    static constexpr float kMinHeight = 34.0f;
    static constexpr size_t kHardMaxChars = 512;

    static ImVec4 idleFill() {
        return EbDesktopMenuTheme::surface();
    }

    static ImVec4 focusFill() {
        return ImVec4(0.20f, 0.20f, 0.28f, 1.0f);
    }

    static bool drawChrome(char const* id, std::string& value, char const* placeholder,
                           bool enabled, size_t maxChars, float width) {
        if (!id || id[0] == '\0') {
            return false;
        }

        size_t limit = maxChars == 0 ? 128 : maxChars;
        if (limit > kHardMaxChars) {
            limit = kHardMaxChars;
        }

        ImGui::PushID(id);
        EbDesktopMenuFonts::pushUi();

        float const avail = ImGui::GetContentRegionAvail().x;
        float fieldW = width > 0.0f ? width : avail;
        if (fieldW > avail) {
            fieldW = avail;
        }
        float const textH = ImGui::GetFontSize();
        float const height = (std::max)(kMinHeight, textH + kPadY * 2.0f);
        float const rounding = EbDesktopMenuTheme::frameRounding();
        float const padY = (std::max)(0.0f, (height - textH) * 0.5f);

        std::vector<char> buffer(limit + 1, '\0');
        size_t copyLen = (std::min)(value.size(), limit);
        if (copyLen > 0) {
            std::memcpy(buffer.data(), value.data(), copyLen);
        }
        buffer[copyLen] = '\0';

        ImVec2 const min = ImGui::GetCursorScreenPos();

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rounding);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, EbDesktopMenuTheme::outlineThickness());
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(kPadX, padY));

        // Heat uses previous-frame focus; approximate with hover after item.
        ImGui::PushStyleColor(ImGuiCol_FrameBg, idleFill());
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, focusFill());
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, focusFill());
        ImGui::PushStyleColor(ImGuiCol_Border, EbDesktopMenuTheme::outline());
        ImGui::PushStyleColor(ImGuiCol_Text, enabled ? EbDesktopMenuTheme::text()
                                                     : ImVec4(0.62f, 0.64f, 0.70f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, EbDesktopMenuTheme::muted());

        ImGui::SetNextItemWidth(fieldW);
        ImGuiInputTextFlags flags = ImGuiInputTextFlags_None;
        if (!enabled) {
            flags |= ImGuiInputTextFlags_ReadOnly;
        }
        bool changed =
            ImGui::InputTextWithHint("##eb-tf-input", placeholder ? placeholder : "", buffer.data(),
                                     buffer.size(), flags);
        bool const hovered = ImGui::IsItemHovered();
        bool const focused = ImGui::IsItemActive() || ImGui::IsItemFocused();
        float const heat =
            enabled ? EbDesktopMenuMotion::smoothToward(ImGui::GetItemID(), hovered || focused, 0.05f)
                    : 0.0f;

        ImGui::PopStyleColor(6);
        ImGui::PopStyleVar(3);

        ImVec2 const max(min.x + fieldW, min.y + ImGui::GetItemRectSize().y);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (enabled && heat > 0.02f && EbDesktopMenuTheme::styleAlpha() > 0.01f) {
            EbDesktopMenuTheme::drawAccentGlow(drawList, min, max, rounding, 0.22f * heat);
        }
        if (enabled && heat > 0.01f) {
            ImVec4 stroke = EbDesktopMenuTheme::outline();
            ImVec4 accent = EbDesktopMenuTheme::accentLight();
            accent.w = 0.40f + 0.50f * heat;
            stroke = EbDesktopMenuMotion::lerp4(stroke, accent, heat);
            drawList->AddRect(min, max, EbDesktopMenuTheme::toU32(stroke), rounding, 0,
                              EbDesktopMenuTheme::outlineThickness());
        }

        if (changed && enabled) {
            value.assign(buffer.data());
            if (value.size() > limit) {
                value.resize(limit);
            }
        }

        EbDesktopMenuFonts::pop();
        ImGui::PopID();
        return changed && enabled;
    }

private:
    EbTextFieldDraw() = delete;
};

bool EbTextField::draw(char const* id, std::string& value, char const* placeholder, bool enabled,
                       size_t maxChars, float width) {
    return EbTextFieldDraw::drawChrome(id, value, placeholder, enabled, maxChars, width);
}

#else

bool EbTextField::draw(char const*, std::string&, char const*, bool, size_t, float) {
    return false;
}

#endif
