#include "desktop_menu_tooltip.hpp"
#include "desktop_menu_fonts.hpp"
#include "desktop_menu_theme.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <imgui.h>

std::string EbDesktopMenuTooltip::descriptionForKey(char const* key) {
    if (!key || key[0] == '\0') {
        return {};
    }
    auto* mod = geode::prelude::Mod::get();
    if (!mod) {
        return {};
    }
    std::shared_ptr<geode::SettingV3> setting = mod->getSetting(key);
    if (!setting) {
        return {};
    }
    std::optional<std::string> const desc = setting->getDescription();
    if (!desc || desc->empty()) {
        return {};
    }
    return *desc;
}

void EbDesktopMenuTooltip::show(char const* text) {
    if (!text || text[0] == '\0') {
        return;
    }
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) {
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_PopupBg, EbDesktopMenuTheme::panel());
    ImGui::PushStyleColor(ImGuiCol_Border, EbDesktopMenuTheme::outline());
    ImGui::PushStyleColor(ImGuiCol_Text, EbDesktopMenuTheme::text());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::BeginTooltip();
    EbDesktopMenuFonts::pushUi();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    EbDesktopMenuFonts::pop();
    ImGui::EndTooltip();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);
}

#else

std::string EbDesktopMenuTooltip::descriptionForKey(char const* key) {
    (void)key;
    return {};
}

void EbDesktopMenuTooltip::show(char const* text) {
    (void)text;
}

#endif
