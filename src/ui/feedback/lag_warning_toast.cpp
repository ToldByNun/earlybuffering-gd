#include "lag_warning_toast.hpp"
#include "../../app/class_manager.hpp"
#include "../../config/stage_config.hpp"
#include "../../core/assist/contracts/live_plan_contracts.hpp"
#include "../../core/platform/platform_limits.hpp"
#include "../desktop_menu/desktop_menu.hpp"
#include "../desktop_menu/desktop_menu_fonts.hpp"
#include "../desktop_menu/desktop_menu_theme.hpp"
#include <algorithm>
#include <fmt/format.h>

#if defined(EB_DESKTOP_IMGUI)
#include <imgui-cocos.hpp>
#include <imgui.h>
#endif

EbLagWarningToast& EbLagWarningToast::get() {
    return EbClassManager::get().service<EbLagWarningToast>();
}

float EbLagWarningToast::opacity() const {
    if (!this->m_showing)
        return 0.f;
    if (this->m_ageSeconds <= kHoldSeconds)
        return 1.f;
    float const fadeT = (this->m_ageSeconds - kHoldSeconds) / kFadeSeconds;
    return std::clamp(1.f - fadeT, 0.f, 1.f);
}

void EbLagWarningToast::attach(PlayLayer*) {
    this->m_showing = false;
    this->m_shownThisLevel = false;
    this->m_ageSeconds = 0.f;
    this->m_reason.clear();
}

void EbLagWarningToast::detach(bool) {
    this->m_showing = false;
    this->m_shownThisLevel = false;
    this->m_ageSeconds = 0.f;
    this->m_reason.clear();
}

void EbLagWarningToast::onPhysicsTick(float deltaTimeSeconds) {
    if (!this->m_showing)
        return;
    this->m_ageSeconds += std::max(0.f, deltaTimeSeconds);
    if (this->m_ageSeconds < kHoldSeconds + kFadeSeconds)
        return;

    this->m_showing = false;
    this->m_ageSeconds = 0.f;
#if defined(EB_DESKTOP_IMGUI)
    // Drop the forced ImGui layer when the menu is not open.
    if (!EbDesktopMenu::isOpen())
        ImGuiCocos::get().setVisible(false);
#endif
}

void EbLagWarningToast::notify(int planUs, int budgetUs, bool budgetHit) {
    (void)budgetUs;
    if (this->m_shownThisLevel)
        return;
    if (!EbLivePlanContracts::lagWarningShouldNotify(EbStageConfig::lagWarningToast(), planUs,
                                                     budgetHit)) {
        return;
    }

#if !defined(EB_DESKTOP_IMGUI)
    (void)planUs;
    (void)budgetHit;
    return;
#else
    if (budgetHit)
        this->m_reason = fmt::format("Soft wall · {:.1f}ms", planUs / 1000.f);
    else
        this->m_reason = fmt::format("Planning · {:.1f}ms", planUs / 1000.f);

    this->m_shownThisLevel = true;
    this->m_ageSeconds = 0.f;
    this->m_showing = true;
    // ImGuiCocos only paints while visible — keep it up for the card even if
    // the desktop shell is closed (window is NoInputs).
    ImGuiCocos::get().setVisible(true);
#endif
}

bool EbLagWarningToast::wantsDraw() {
#if !defined(EB_DESKTOP_IMGUI)
    return false;
#else
    auto& toast = get();
    return toast.m_showing && toast.opacity() > 0.01f;
#endif
}

void EbLagWarningToast::draw() {
#if !defined(EB_DESKTOP_IMGUI)
    return;
#else
    if (!wantsDraw())
        return;

    // Theme normally reapplies inside the shell; toast can show with the menu closed.
    EbDesktopMenuTheme::apply();

    auto& toast = get();
    float const alpha = toast.opacity();
    float const padX = 18.0f;
    float const padY = 16.0f;
    float const cardW = 280.0f;
    float const rounding = EbDesktopMenuTheme::panelRounding();

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, EbDesktopMenuTheme::outlineThickness());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padX, padY));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 10.0f));

    ImVec4 border = EbDesktopMenuTheme::outline();
    border.w = 0.55f;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, EbDesktopMenuTheme::panel());
    ImGui::PushStyleColor(ImGuiCol_Border, border);
    ImGui::PushStyleColor(ImGuiCol_Text, EbDesktopMenuTheme::text());

    float const left = EbPlatformLimits::get().overlayLeftInset();
    float const bottom = 20.0f;
    ImVec2 const display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(left, display.y - bottom), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(cardW, 0.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(cardW, 0.0f), ImVec2(cardW, FLT_MAX));

    ImGuiWindowFlags const flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar;

    if (ImGui::Begin("##eb-lag-warning", nullptr, flags)) {
        ImVec2 const winPos = ImGui::GetWindowPos();
        ImVec2 const winSize = ImGui::GetWindowSize();
        ImVec2 const winMax(winPos.x + winSize.x, winPos.y + winSize.y);
        ImDrawList* bg = ImGui::GetBackgroundDrawList();
        // Soft drop shadow — same language as cloud sheet / shell presence.
        for (int index = 0; index < 3; ++index) {
            float const grow = 5.0f + static_cast<float>(index) * 4.0f;
            float const shadowA = (0.18f - static_cast<float>(index) * 0.05f) * alpha;
            bg->AddRectFilled(ImVec2(winPos.x - grow * 0.15f, winPos.y + grow * 0.3f),
                              ImVec2(winMax.x + grow * 0.15f, winMax.y + grow),
                              IM_COL32(0, 0, 0, static_cast<int>(shadowA * 255.0f + 0.5f)),
                              rounding + grow * 0.2f);
        }

        // Section-card title treatment: muted caps label.
        EbDesktopMenuFonts::pushCaps();
        ImGui::PushStyleColor(ImGuiCol_Text, EbDesktopMenuTheme::muted());
        ImGui::TextUnformatted("THERE MIGHT BE LAGS");
        ImGui::PopStyleColor();
        EbDesktopMenuFonts::pop();

        ImGui::Dummy(ImVec2(0.0f, 2.0f));

        EbDesktopMenuFonts::pushUi();
        ImGui::TextUnformatted(toast.m_reason.c_str());
        EbDesktopMenuFonts::pop();
    }
    ImGui::End();

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(5);
#endif
}
