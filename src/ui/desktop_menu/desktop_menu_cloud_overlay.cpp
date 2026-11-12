#include "desktop_menu_cloud_overlay.hpp"
#include "desktop_menu_fonts.hpp"
#include "desktop_menu_modal.hpp"
#include "desktop_menu_motion.hpp"
#include "desktop_menu_theme.hpp"
#include "widgets/eb_button.hpp"
#include "widgets/eb_keybind.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include <imgui.h>
#include <algorithm>
#include <cfloat>

class EbDesktopCloudOverlayState {
public:
    static bool& wantOpen() {
        static bool value = false;
        return value;
    }

    static float& visual() {
        static float value = 0.0f;
        return value;
    }

    static bool& closing() {
        static bool value = false;
        return value;
    }

    static float& closeElapsed() {
        static float value = 0.0f;
        return value;
    }

    static float& closeStartVisual() {
        static float value = 1.0f;
        return value;
    }

    // Nested share panel (AP7). Esc closes share before the overlay.
    static bool& shareOpen() {
        static bool value = false;
        return value;
    }

    static constexpr float kOpenTau = 0.09f;
    static constexpr float kCloseDuration = 0.20f;
    static constexpr float kCardW = 680.0f;
    static constexpr float kCardH = 460.0f;
    static constexpr float kSlidePx = 14.0f;
    static constexpr float kPad = 18.0f;

private:
    EbDesktopCloudOverlayState() = delete;
};

class EbDesktopCloudOverlayDraw {
public:
    static void finishClose() {
        EbDesktopCloudOverlayState::shareOpen() = false;
    }

    static void requestDismiss() {
        if (EbDesktopCloudOverlayState::shareOpen()) {
            EbDesktopCloudOverlayState::shareOpen() = false;
            return;
        }
        EbDesktopCloudOverlayState::wantOpen() = false;
    }

    static float clampf(float value, float lo, float hi) {
        return (std::max)(lo, (std::min)(hi, value));
    }

    static void updateVisual(float deltaTime) {
        float& visual = EbDesktopCloudOverlayState::visual();
        float const dt = clampf(deltaTime, 0.0f, 1.0f / 20.0f);

        if (EbDesktopCloudOverlayState::wantOpen()) {
            EbDesktopCloudOverlayState::closing() = false;
            EbDesktopCloudOverlayState::closeElapsed() = 0.0f;
            visual = EbDesktopMenuMotion::approach(visual, 1.0f, EbDesktopCloudOverlayState::kOpenTau,
                                                   dt);
            if (visual > 0.985f) {
                visual = 1.0f;
            }
            return;
        }

        if (visual <= 0.001f) {
            visual = 0.0f;
            EbDesktopCloudOverlayState::closing() = false;
            EbDesktopCloudOverlayState::closeElapsed() = 0.0f;
            return;
        }

        if (!EbDesktopCloudOverlayState::closing()) {
            EbDesktopCloudOverlayState::closing() = true;
            EbDesktopCloudOverlayState::closeElapsed() = 0.0f;
            EbDesktopCloudOverlayState::closeStartVisual() = visual;
        }

        EbDesktopCloudOverlayState::closeElapsed() += dt;
        float const dur = EbDesktopCloudOverlayState::kCloseDuration;
        float t = dur > 0.0f ? EbDesktopCloudOverlayState::closeElapsed() / dur : 1.0f;
        if (t >= 1.0f) {
            visual = 0.0f;
            EbDesktopCloudOverlayState::closing() = false;
            return;
        }
        float const u = t * t * (3.0f - 2.0f * t);
        visual = EbDesktopCloudOverlayState::closeStartVisual() * (1.0f - u);
    }

    static void drawPlaceholderBody(float innerW) {
        EbDesktopMenuFonts::pushUi();
        EbDesktopMenuFonts::pushCaps();
        ImGui::PushStyleColor(ImGuiCol_Text, EbDesktopMenuTheme::text());
        ImGui::TextUnformatted("Cloud Configs");
        ImGui::PopStyleColor();
        EbDesktopMenuFonts::pop();

        ImGui::PushStyleColor(ImGuiCol_Text, EbDesktopMenuTheme::muted());
        ImGui::PushTextWrapPos(ImGui::GetCursorScreenPos().x + innerW);
        ImGui::TextUnformatted("Browse, install, rate, and share community packs.");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        EbDesktopMenuFonts::pop();

        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        if (EbButton::draw("##eb-cloud-close", "Close")) {
            requestDismiss();
        }
    }

    static void drawFrame() {
        updateVisual(ImGui::GetIO().DeltaTime);

        float const visual = EbDesktopCloudOverlayState::visual();
        if (visual <= 0.001f && !EbDesktopCloudOverlayState::wantOpen()) {
            finishClose();
            return;
        }

        // Modal owns Esc / capture when stacked above this overlay.
        bool const modalUp = EbDesktopMenuModal::isOpen();
        if (!modalUp) {
            ImGui::SetNextFrameWantCaptureKeyboard(true);
            ImGui::SetNextFrameWantCaptureMouse(true);
        }

        ImGuiIO& io = ImGui::GetIO();
        ImVec2 const display = io.DisplaySize;
        ImVec2 const displayPos = ImGui::GetMainViewport()->Pos;
        float const slide = (1.0f - visual) * EbDesktopCloudOverlayState::kSlidePx;
        float const cardW = EbDesktopCloudOverlayState::kCardW;
        float const cardH = EbDesktopCloudOverlayState::kCardH;
        float const pad = EbDesktopCloudOverlayState::kPad;

        ImGui::SetNextWindowPos(displayPos);
        ImGui::SetNextWindowSize(display);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGuiWindowFlags const hostFlags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::Begin("##eb-cloud-host", nullptr, hostFlags);

        ImDrawList* hostDraw = ImGui::GetWindowDrawList();
        float const scrimA = 0.55f * visual;
        hostDraw->AddRectFilled(
            displayPos, ImVec2(displayPos.x + display.x, displayPos.y + display.y),
            IM_COL32(0, 0, 0, static_cast<int>(scrimA * 255.0f + 0.5f)));

        ImGui::SetCursorScreenPos(displayPos);
        ImGui::InvisibleButton("##eb-cloud-scrim", display);
        bool const scrimClicked = ImGui::IsItemClicked();

        if (!modalUp && EbDesktopCloudOverlayState::wantOpen() && visual > 0.85f &&
            ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            requestDismiss();
        }

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        ImVec2 const center(displayPos.x + display.x * 0.5f,
                            displayPos.y + display.y * 0.5f + slide);
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(cardW, cardH));
        if (!modalUp) {
            ImGui::SetNextWindowFocus();
        }

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, visual);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, EbDesktopMenuTheme::panelRounding());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, EbDesktopMenuTheme::outlineThickness());
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 10.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, EbDesktopMenuTheme::panel());
        ImGui::PushStyleColor(ImGuiCol_Border, EbDesktopMenuTheme::outline());

        ImGuiWindowFlags const cardFlags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar;
        ImGui::Begin("##eb-cloud-card", nullptr, cardFlags);

        drawPlaceholderBody(cardW - pad * 2.0f);

        bool const cardHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(5);

        if (!modalUp && scrimClicked && !cardHovered && EbDesktopCloudOverlayState::wantOpen() &&
            visual > 0.85f) {
            requestDismiss();
        }
    }

private:
    EbDesktopCloudOverlayDraw() = delete;
};

void EbDesktopCloudOverlay::open() {
    EbKeybind::cancelCapture();
    EbDesktopCloudOverlayState::shareOpen() = false;
    EbDesktopCloudOverlayState::wantOpen() = true;
}

void EbDesktopCloudOverlay::dismiss() {
    if (!EbDesktopCloudOverlayState::wantOpen() &&
        EbDesktopCloudOverlayState::visual() <= 0.001f) {
        return;
    }
    EbDesktopCloudOverlayState::shareOpen() = false;
    EbDesktopCloudOverlayState::wantOpen() = false;
}

bool EbDesktopCloudOverlay::isOpen() {
    return EbDesktopCloudOverlayState::wantOpen() ||
           EbDesktopCloudOverlayState::visual() > 0.001f;
}

bool EbDesktopCloudOverlay::shouldKeepVisible() {
    return isOpen();
}

void EbDesktopCloudOverlay::draw() {
    EbDesktopCloudOverlayDraw::drawFrame();
}

#else

void EbDesktopCloudOverlay::open() {}

void EbDesktopCloudOverlay::dismiss() {}

bool EbDesktopCloudOverlay::isOpen() {
    return false;
}

bool EbDesktopCloudOverlay::shouldKeepVisible() {
    return false;
}

void EbDesktopCloudOverlay::draw() {}

#endif
