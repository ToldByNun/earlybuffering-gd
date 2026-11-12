#include "desktop_menu_modal.hpp"
#include "desktop_menu_fonts.hpp"
#include "desktop_menu_motion.hpp"
#include "desktop_menu_theme.hpp"
#include "widgets/eb_button.hpp"
#include "widgets/eb_keybind.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include <imgui.h>
#include <algorithm>
#include <cfloat>

class EbDesktopMenuModalState {
public:
    enum class Kind { None = 0, Info, Confirm };

    static Kind& kind() {
        static Kind value = Kind::None;
        return value;
    }

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

    static std::string& title() {
        static std::string value;
        return value;
    }

    static std::string& body() {
        static std::string value;
        return value;
    }

    static std::function<void()>& onYes() {
        static std::function<void()> value;
        return value;
    }

    static std::function<void()>& onNo() {
        static std::function<void()> value;
        return value;
    }

    static constexpr float kOpenTau = 0.09f;
    static constexpr float kCloseDuration = 0.20f; // cubic close — lands on 0, no opaque hang
    static constexpr float kCardW = 420.0f;
    static constexpr float kSlidePx = 14.0f;
    static constexpr float kPad = 18.0f;

private:
    EbDesktopMenuModalState() = delete;
};

class EbDesktopMenuModalDraw {
public:
    static void clearCallbacks() {
        EbDesktopMenuModalState::onYes() = {};
        EbDesktopMenuModalState::onNo() = {};
    }

    static void finishClose() {
        EbDesktopMenuModalState::kind() = EbDesktopMenuModalState::Kind::None;
        EbDesktopMenuModalState::title().clear();
        EbDesktopMenuModalState::body().clear();
        clearCallbacks();
    }

    static void acceptYes() {
        auto cb = EbDesktopMenuModalState::onYes();
        EbDesktopMenuModalState::wantOpen() = false;
        clearCallbacks();
        if (cb) {
            cb();
        }
    }

    static void acceptNo() {
        auto cb = EbDesktopMenuModalState::onNo();
        EbDesktopMenuModalState::wantOpen() = false;
        clearCallbacks();
        if (cb) {
            cb();
        }
    }

    static void requestDismiss() {
        if (EbDesktopMenuModalState::kind() == EbDesktopMenuModalState::Kind::Confirm) {
            acceptNo();
            return;
        }
        EbDesktopMenuModalState::wantOpen() = false;
    }

    static float clampf(float value, float lo, float hi) {
        return (std::max)(lo, (std::min)(hi, value));
    }

    static void updateVisual(float deltaTime) {
        float& visual = EbDesktopMenuModalState::visual();
        float const dt = clampf(deltaTime, 0.0f, 1.0f / 20.0f);

        if (EbDesktopMenuModalState::wantOpen()) {
            EbDesktopMenuModalState::closing() = false;
            EbDesktopMenuModalState::closeElapsed() = 0.0f;
            visual = EbDesktopMenuMotion::approach(visual, 1.0f, EbDesktopMenuModalState::kOpenTau,
                                                   dt);
            if (visual > 0.985f) {
                visual = 1.0f;
            }
            return;
        }

        if (visual <= 0.001f) {
            visual = 0.0f;
            EbDesktopMenuModalState::closing() = false;
            EbDesktopMenuModalState::closeElapsed() = 0.0f;
            return;
        }

        if (!EbDesktopMenuModalState::closing()) {
            EbDesktopMenuModalState::closing() = true;
            EbDesktopMenuModalState::closeElapsed() = 0.0f;
            EbDesktopMenuModalState::closeStartVisual() = visual;
        }

        EbDesktopMenuModalState::closeElapsed() += dt;
        float const dur = EbDesktopMenuModalState::kCloseDuration;
        float t = dur > 0.0f ? EbDesktopMenuModalState::closeElapsed() / dur : 1.0f;
        if (t >= 1.0f) {
            visual = 0.0f;
            EbDesktopMenuModalState::closing() = false;
            return;
        }
        float const u = t * t * (3.0f - 2.0f * t);
        visual = EbDesktopMenuModalState::closeStartVisual() * (1.0f - u);
    }

    static void drawFrame() {
        updateVisual(ImGui::GetIO().DeltaTime);

        float const visual = EbDesktopMenuModalState::visual();
        if (visual <= 0.001f && !EbDesktopMenuModalState::wantOpen()) {
            if (EbDesktopMenuModalState::kind() != EbDesktopMenuModalState::Kind::None) {
                finishClose();
            }
            return;
        }

        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextFrameWantCaptureKeyboard(true);
        ImGui::SetNextFrameWantCaptureMouse(true);

        ImVec2 const display = io.DisplaySize;
        ImVec2 const displayPos = ImGui::GetMainViewport()->Pos;
        float const slide = (1.0f - visual) * EbDesktopMenuModalState::kSlidePx;
        float const cardW = EbDesktopMenuModalState::kCardW;
        float const pad = EbDesktopMenuModalState::kPad;

        // --- Scrim host (below the card window) ---
        ImGui::SetNextWindowPos(displayPos);
        ImGui::SetNextWindowSize(display);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGuiWindowFlags const hostFlags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::Begin("##eb-modal-host", nullptr, hostFlags);

        ImDrawList* hostDraw = ImGui::GetWindowDrawList();
        float const scrimA = 0.55f * visual;
        hostDraw->AddRectFilled(
            displayPos, ImVec2(displayPos.x + display.x, displayPos.y + display.y),
            IM_COL32(0, 0, 0, static_cast<int>(scrimA * 255.0f + 0.5f)));

        ImGui::SetCursorScreenPos(displayPos);
        ImGui::InvisibleButton("##eb-modal-scrim", display);
        bool const scrimClicked = ImGui::IsItemClicked();

        if (EbDesktopMenuModalState::wantOpen() && visual > 0.85f &&
            ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            requestDismiss();
        }

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        // --- Card window (on top; fixed width so EbButton avail is correct) ---
        ImVec2 const center(displayPos.x + display.x * 0.5f,
                            displayPos.y + display.y * 0.5f + slide);
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(cardW, 0.0f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(cardW, 0.0f), ImVec2(cardW, FLT_MAX));
        ImGui::SetNextWindowFocus();

        // Style.Alpha fades WindowBg/text; EbButton multiplies the same Alpha into custom chrome.
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, visual);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, EbDesktopMenuTheme::panelRounding());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, EbDesktopMenuTheme::outlineThickness());
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 10.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, EbDesktopMenuTheme::panel());
        ImGui::PushStyleColor(ImGuiCol_Border, EbDesktopMenuTheme::outline());

        ImGuiWindowFlags const cardFlags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoScrollbar;
        ImGui::Begin("##eb-modal-card", nullptr, cardFlags);

        EbDesktopMenuFonts::pushUi();
        EbDesktopMenuFonts::pushCaps();
        ImGui::PushStyleColor(ImGuiCol_Text, EbDesktopMenuTheme::text());
        ImGui::PushTextWrapPos(ImGui::GetCursorScreenPos().x + cardW - pad * 2.0f);
        ImGui::TextUnformatted(EbDesktopMenuModalState::title().c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        EbDesktopMenuFonts::pop();

        ImGui::PushStyleColor(ImGuiCol_Text, EbDesktopMenuTheme::muted());
        ImGui::PushTextWrapPos(ImGui::GetCursorScreenPos().x + cardW - pad * 2.0f);
        ImGui::TextUnformatted(EbDesktopMenuModalState::body().c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        // Content region is card-inner width — EbButton no longer spans the screen.
        if (EbDesktopMenuModalState::kind() == EbDesktopMenuModalState::Kind::Confirm) {
            EbButton::beginRow(2);
            if (EbButton::draw("##eb-modal-no", "Cancel")) {
                acceptNo();
            }
            if (EbButton::draw("##eb-modal-yes", "Yes")) {
                acceptYes();
            }
            EbButton::endRow();
        } else if (EbButton::draw("##eb-modal-ok", "OK")) {
            EbDesktopMenuModalState::wantOpen() = false;
        }

        bool const cardHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        EbDesktopMenuFonts::pop();
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(5);

        if (scrimClicked && !cardHovered && EbDesktopMenuModalState::wantOpen() && visual > 0.85f) {
            requestDismiss();
        }
    }

private:
    EbDesktopMenuModalDraw() = delete;
};

void EbDesktopMenuModal::showInfo(std::string title, std::string body) {
    EbKeybind::cancelCapture();
    EbDesktopMenuModalState::kind() = EbDesktopMenuModalState::Kind::Info;
    EbDesktopMenuModalState::title() = std::move(title);
    EbDesktopMenuModalState::body() = std::move(body);
    EbDesktopMenuModalState::onYes() = {};
    EbDesktopMenuModalState::onNo() = {};
    EbDesktopMenuModalState::wantOpen() = true;
}

void EbDesktopMenuModal::showConfirm(std::string title, std::string body,
                                     std::function<void()> onYes, std::function<void()> onNo) {
    EbKeybind::cancelCapture();
    EbDesktopMenuModalState::kind() = EbDesktopMenuModalState::Kind::Confirm;
    EbDesktopMenuModalState::title() = std::move(title);
    EbDesktopMenuModalState::body() = std::move(body);
    EbDesktopMenuModalState::onYes() = std::move(onYes);
    EbDesktopMenuModalState::onNo() = std::move(onNo);
    EbDesktopMenuModalState::wantOpen() = true;
}

bool EbDesktopMenuModal::isOpen() {
    return EbDesktopMenuModalState::wantOpen() || EbDesktopMenuModalState::visual() > 0.001f;
}

bool EbDesktopMenuModal::shouldKeepVisible() {
    return isOpen();
}

void EbDesktopMenuModal::draw() {
    EbDesktopMenuModalDraw::drawFrame();
}

void EbDesktopMenuModal::dismiss() {
    if (!EbDesktopMenuModalState::wantOpen() && EbDesktopMenuModalState::visual() <= 0.001f) {
        return;
    }
    EbDesktopMenuModalDraw::requestDismiss();
}

#else

void EbDesktopMenuModal::showInfo(std::string, std::string) {}

void EbDesktopMenuModal::showConfirm(std::string, std::string, std::function<void()>,
                                     std::function<void()>) {}

bool EbDesktopMenuModal::isOpen() {
    return false;
}

bool EbDesktopMenuModal::shouldKeepVisible() {
    return false;
}

void EbDesktopMenuModal::draw() {}

void EbDesktopMenuModal::dismiss() {}

#endif
