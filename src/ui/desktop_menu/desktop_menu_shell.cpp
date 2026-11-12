#include "desktop_menu_shell.hpp"
#include "desktop_menu_fonts.hpp"
#include "desktop_menu_icons.hpp"
#include "desktop_menu_motion.hpp"
#include "desktop_menu_panels.hpp"
#include "desktop_menu_sidebar_smoke.hpp"
#include "desktop_menu_theme.hpp"
#include "desktop_menu_tooltip.hpp"
#include "widgets/eb_keybind.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include <Geode/Geode.hpp>
#include <imgui.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>

class EbDesktopMenuShellState {
public:
    static SettingsCategory& active() {
        static SettingsCategory category = SettingsCategory::Main;
        return category;
    }

    static float& wheelDelta() {
        static float delta = 0.0f;
        return delta;
    }

    static bool& wantOpen() {
        static bool value = false;
        return value;
    }

    // Displayed open amount (0..1). Exponentially approaches wantOpen — reversing
    // mid-animation stays continuous (no ease hitch / pos snap).
    static float& openVisual() {
        static float value = 0.0f;
        return value;
    }

    // 1 = settled tab content. Dips on switch, eases back — interrupt-safe.
    static float& tabVisual() {
        static float value = 1.0f;
        return value;
    }

    static int& tabDir() {
        static int value = 1;
        return value;
    }

    static bool& tabResetScroll() {
        static bool value = false;
        return value;
    }

    static float& scrollTarget() {
        static float value = 0.0f;
        return value;
    }

    static float& scrollVisual() {
        static float value = 0.0f;
        return value;
    }

    static constexpr float kSidebarWidth = 196.0f;
    static constexpr float kMenuWidth = 900.0f;
    static constexpr float kMenuHeight = 600.0f;
    // Inset so section cards sit inside the tab — bg shows around the card.
    // Top and bottom use the same WindowPadding (kContentPadY); no extra end Dummy.
    static constexpr float kContentPadX = 28.0f;
    static constexpr float kContentPadY = 24.0f;

    static constexpr float kOpenTau = 0.11f;    // ~settle in ~0.35s
    static constexpr float kCloseDuration = 0.20f; // fixed cubic close — hits 0, no end snap hitch
    static constexpr float kTabTau = 0.10f;     // ~settle in ~0.30s
    static constexpr float kOpenSlidePx = 14.0f;
    static constexpr float kTabSlidePx = 16.0f;
    static constexpr float kTabDip = 0.62f; // don't hard-reset to 0 on re-click
    static constexpr float kDragFollowTau = 0.055f;
    static constexpr int kTrailCount = 4;

    static ImVec2& targetCenter() {
        static ImVec2 value(0.0f, 0.0f);
        return value;
    }

    static ImVec2& visualCenter() {
        static ImVec2 value(0.0f, 0.0f);
        return value;
    }

    static bool& dragging() {
        static bool value = false;
        return value;
    }

    static bool& haveAnchor() {
        static bool value = false;
        return value;
    }

    static ImVec2* trail() {
        static ImVec2 values[kTrailCount];
        return values;
    }

    static float& prevOpenVisual() {
        static float value = 0.0f;
        return value;
    }

    // Close curve state — captured when wantOpen flips false.
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

    // Sticky for one frame — widgets close their BeginPopup when set.
    static bool& dismissFloatingPopups() {
        static bool value = false;
        return value;
    }

private:
    EbDesktopMenuShellState() = delete;
};

class EbDesktopMenuShellAnim {
public:
    static float clampf(float value, float lo, float hi) {
        if (value < lo) {
            return lo;
        }
        if (value > hi) {
            return hi;
        }
        return value;
    }

    static float approach(float current, float target, float tau, float deltaTime) {
        if (tau <= 0.0001f) {
            return target;
        }
        float const dt = clampf(deltaTime, 0.0f, 1.0f / 20.0f);
        float const blend = 1.0f - std::exp(-dt / tau);
        float const next = current + (target - current) * blend;
        if (std::fabs(target - next) < 0.0008f) {
            return target;
        }
        return next;
    }

    static void clearTrailToCenter() {
        ImVec2 const center = EbDesktopMenuShellState::visualCenter();
        ImVec2* trail = EbDesktopMenuShellState::trail();
        for (int index = 0; index < EbDesktopMenuShellState::kTrailCount; ++index) {
            trail[index] = center;
        }
        EbDesktopMenuShellState::dragging() = false;
    }

    static void updateOpen(float deltaTime) {
        float& visual = EbDesktopMenuShellState::openVisual();
        float const dt = clampf(deltaTime, 0.0f, 1.0f / 20.0f);

        if (EbDesktopMenuShellState::wantOpen()) {
            // Interrupted close → resume exponential open from current visual.
            EbDesktopMenuShellState::closing() = false;
            EbDesktopMenuShellState::closeElapsed() = 0.0f;
            visual = approach(visual, 1.0f, EbDesktopMenuShellState::kOpenTau, dt);
            if (visual > 0.985f) {
                visual = 1.0f;
            }
            return;
        }

        // Already fully closed — stay parked (don't restart a tiny close loop).
        if (visual <= 0.001f) {
            visual = 0.0f;
            EbDesktopMenuShellState::closing() = false;
            EbDesktopMenuShellState::closeElapsed() = 0.0f;
            return;
        }

        // Begin close: capture start visual, kill trail ghosts immediately.
        if (!EbDesktopMenuShellState::closing()) {
            EbDesktopMenuShellState::closing() = true;
            EbDesktopMenuShellState::closeElapsed() = 0.0f;
            EbDesktopMenuShellState::closeStartVisual() = visual;
            clearTrailToCenter();
        }

        EbDesktopMenuShellState::closeElapsed() += dt;
        float const dur = EbDesktopMenuShellState::kCloseDuration;
        float t = dur > 0.0f ? EbDesktopMenuShellState::closeElapsed() / dur : 1.0f;
        if (t >= 1.0f) {
            visual = 0.0f;
            EbDesktopMenuShellState::closing() = false;
            return;
        }
        // Smoothstep — continuous velocity at ends, lands exactly on 0 at t=1.
        float const u = t * t * (3.0f - 2.0f * t);
        visual = EbDesktopMenuShellState::closeStartVisual() * (1.0f - u);
    }

    static void updateTab(float deltaTime) {
        EbDesktopMenuShellState::tabVisual() =
            approach(EbDesktopMenuShellState::tabVisual(), 1.0f, EbDesktopMenuShellState::kTabTau,
                     deltaTime);
    }

    static void beginTabSwitch(SettingsCategory next) {
        SettingsCategory const prev = EbDesktopMenuShellState::active();
        if (next == prev || next == SettingsCategory::Count) {
            return;
        }
        int const from = static_cast<int>(prev);
        int const to = static_cast<int>(next);
        EbDesktopMenuShellState::tabDir() = to >= from ? 1 : -1;
        // Soft dip from wherever we are — no snap back to a full slide.
        float& tab = EbDesktopMenuShellState::tabVisual();
        if (tab > EbDesktopMenuShellState::kTabDip) {
            tab = EbDesktopMenuShellState::kTabDip;
        } else {
            tab = (std::max)(0.35f, tab * 0.85f);
        }
        EbDesktopMenuShellState::tabResetScroll() = true;
        EbDesktopMenuShellState::active() = next;
        EbDesktopMenuShellState::wheelDelta() = 0.0f;
        EbDesktopMenuShell::requestDismissFloatingPopups();
    }

private:
    EbDesktopMenuShellAnim() = delete;
};

class EbDesktopMenuShellDraw {
public:
    static void navGroupLabel(char const* label) {
        ImGui::Dummy(ImVec2(0.0f, 14.0f));
        EbDesktopMenuFonts::pushCaps();
        ImVec2 const pos = ImGui::GetCursorScreenPos();
        ImFont* const font = ImGui::GetFont();
        float const fontSize = ImGui::GetFontSize();
        ImVec2 const textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
        EbDesktopMenuTheme::drawTextShadowed(ImGui::GetWindowDrawList(), font, fontSize, pos,
                                             ImVec4(1.0f, 1.0f, 1.0f, 0.45f), label);
        ImGui::Dummy(ImVec2(textSize.x, textSize.y));
        EbDesktopMenuFonts::pop();
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
    }

    static bool navTab(char const* label, bool selected, float width) {
        ImGui::PushID(label);
        EbDesktopMenuFonts::pushUi();
        float const fontSize = ImGui::GetFontSize();
        float const rowH = fontSize + 12.0f;
        ImVec2 const pos = ImGui::GetCursorScreenPos();

        bool const pressed = ImGui::InvisibleButton("##tab", ImVec2(width, rowH));
        bool const hovered = ImGui::IsItemHovered();
        {
            char tip[64];
            std::snprintf(tip, sizeof(tip), "Open the %s settings tab", label);
            EbDesktopMenuTooltip::show(tip);
        }
        ImGuiID const itemId = ImGui::GetItemID();
        // Brightness: selected snaps bright; hover eases toward full white.
        float const heat = EbDesktopMenuMotion::smoothToward(itemId, hovered || selected,
                                                             selected ? 0.04f : 0.055f);
        float const underlineTarget = selected ? 1.0f : (hovered ? 0.7f : 0.0f);
        float const underlineT = EbDesktopMenuMotion::smoothFloat(
            itemId ^ 0x554E44u, underlineTarget, selected ? 0.045f : 0.07f);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImFont* const font = ImGui::GetFont();
        ImVec2 const textPos(pos.x + 12.0f, pos.y + (rowH - fontSize) * 0.5f);
        float const textW = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label).x;

        // Idle muted → selected/hover full white (no soft fill plate).
        ImVec4 const idle(1.0f, 1.0f, 1.0f, 0.62f);
        ImVec4 const bright(1.0f, 1.0f, 1.0f, 1.0f);
        float const brightT = selected ? (0.85f + 0.15f * heat) : (0.35f * heat);
        ImVec4 textColor = EbDesktopMenuMotion::lerp4(idle, bright, brightT);
        EbDesktopMenuTheme::drawTextShadowed(drawList, font, fontSize, textPos, textColor, label);

        if (underlineT > 0.01f) {
            float const underlineY = pos.y + rowH - 3.0f;
            ImVec4 line = EbDesktopMenuTheme::accentLight();
            line.w = (selected ? 1.0f : 0.55f) * underlineT;
            float const lineW = textW * underlineT;
            drawList->AddLine(ImVec2(textPos.x, underlineY), ImVec2(textPos.x + lineW, underlineY),
                              EbDesktopMenuTheme::toU32(line), selected ? 2.25f : 1.75f);
        }

        EbDesktopMenuFonts::pop();
        ImGui::PopID();
        return pressed;
    }

    static void drawShellGhost(ImDrawList* drawList, ImVec2 const& center, ImVec2 const& size,
                               float rounding, float alpha) {
        if (!drawList || alpha <= 0.01f) {
            return;
        }
        ImVec2 const min(center.x - size.x * 0.5f, center.y - size.y * 0.5f);
        ImVec2 const max(center.x + size.x * 0.5f, center.y + size.y * 0.5f);
        ImVec4 bg = EbDesktopMenuTheme::bg();
        bg.w = alpha;
        drawList->AddRectFilled(min, max, EbDesktopMenuTheme::toU32(bg), rounding);
        ImVec4 side = EbDesktopMenuTheme::sidebarBg();
        side.w = alpha * 0.85f;
        drawList->AddRectFilled(min, ImVec2(min.x + EbDesktopMenuShellState::kSidebarWidth, max.y),
                                EbDesktopMenuTheme::toU32(side), rounding,
                                ImDrawFlags_RoundCornersLeft);
    }

    static void drawSidebarNav(ImVec2 const& origin, float width) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + 16.0f, origin.y + 20.0f));
        ImGui::BeginGroup();

        ImDrawList* const sideDraw = ImGui::GetWindowDrawList();
        EbDesktopMenuFonts::pushHeader();
        {
            ImVec2 const pos = ImGui::GetCursorScreenPos();
            ImFont* const font = ImGui::GetFont();
            float const fontSize = ImGui::GetFontSize();
            char const* title = "EARLYBUFFER";
            ImVec2 const textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, title);
            EbDesktopMenuTheme::drawTextShadowed(sideDraw, font, fontSize, pos,
                                                 EbDesktopMenuTheme::text(), title);
            ImGui::Dummy(ImVec2(textSize.x, textSize.y));
        }
        EbDesktopMenuFonts::pop();

        std::string version = "v?.?.?";
        if (auto* mod = geode::prelude::Mod::get()) {
            version = mod->getVersion().toVString();
        }
        EbDesktopMenuFonts::pushCaps();
        {
            ImVec2 const pos = ImGui::GetCursorScreenPos();
            ImFont* const font = ImGui::GetFont();
            float const fontSize = ImGui::GetFontSize();
            ImVec2 const textSize =
                font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, version.c_str());
            EbDesktopMenuTheme::drawTextShadowed(sideDraw, font, fontSize, pos,
                                                 ImVec4(1.0f, 1.0f, 1.0f, 0.42f), version.c_str());
            ImGui::Dummy(ImVec2(textSize.x, textSize.y));
        }
        EbDesktopMenuFonts::pop();

        float const tabWidth = width - 32.0f;

        navGroupLabel("SETTINGS");
        SettingsCategory const settingsTabs[] = {
            SettingsCategory::Main,  SettingsCategory::Assist, SettingsCategory::Learn,
            SettingsCategory::Bake,  SettingsCategory::Modes,  SettingsCategory::Visual,
            SettingsCategory::UI,    SettingsCategory::Keys};
        for (SettingsCategory category : settingsTabs) {
            bool const selected = EbDesktopMenuShellState::active() == category;
            if (navTab(EbInLevelSettingsFilter::categoryName(category), selected, tabWidth)) {
                EbDesktopMenuShellAnim::beginTabSwitch(category);
            }
        }

        navGroupLabel("DEVELOPER");
        {
            bool const selected = EbDesktopMenuShellState::active() == SettingsCategory::Dev;
            if (navTab(EbInLevelSettingsFilter::categoryName(SettingsCategory::Dev), selected,
                       tabWidth)) {
                EbDesktopMenuShellAnim::beginTabSwitch(SettingsCategory::Dev);
            }
        }

        ImGui::EndGroup();
    }

    static void drawSidebarFooter(ImVec2 const& sideMin, float sideW, float sideH) {
        EbDesktopMenuIcons::drawSidebarLinks(sideMin, sideW, sideH);
    }

    static void drawContentBody(ImVec2 const& origin, float width, float height, float tabAlpha,
                                float tabSlideX) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + tabSlideX, origin.y));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * tabAlpha);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            ImVec2(EbDesktopMenuShellState::kContentPadX,
                                   EbDesktopMenuShellState::kContentPadY));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 8.0f));
        // Hide bar via size 0 — keep scroll extent (NoScrollbar can clip last rows).
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0.0f);
        // AlwaysUseWindowPadding: borderless children ignore WindowPadding otherwise,
        // so the section card would fill the tab 1:1 with no bg gutter.
        ImGui::BeginChild("##eb-shell-tab", ImVec2(width, height),
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);

        if (EbDesktopMenuShellState::tabResetScroll()) {
            EbDesktopMenuShellState::scrollTarget() = 0.0f;
            EbDesktopMenuShellState::scrollVisual() = 0.0f;
            ImGui::SetScrollY(0.0f);
            EbDesktopMenuShellState::tabResetScroll() = false;
        }

        // Scroll + dismiss BEFORE panels so combo/dropdown/color can CloseCurrentPopup
        // inside their BeginPopup this frame (shell-side CloseCurrentPopup is a no-op).
        float const wheel = EbDesktopMenuShell::takeWheelDelta();
        ImVec2 const mouse = ImGui::GetIO().MousePos;
        bool const overTab = mouse.x >= origin.x && mouse.x < origin.x + width &&
                             mouse.y >= origin.y && mouse.y < origin.y + height;
        float const maxY = ImGui::GetScrollMaxY();
        float const scrollBefore = EbDesktopMenuShellState::scrollVisual();
        bool scrolled = false;
        if (wheel != 0.0f && overTab && EbDesktopMenuShellState::tabVisual() >= 0.97f) {
            float const step = ImGui::GetTextLineHeightWithSpacing();
            float next = EbDesktopMenuShellState::scrollTarget() + wheel * step;
            if (next < 0.0f) {
                next = 0.0f;
            }
            if (next > maxY) {
                next = maxY;
            }
            if (std::fabs(next - EbDesktopMenuShellState::scrollTarget()) > 0.01f) {
                scrolled = true;
            }
            EbDesktopMenuShellState::scrollTarget() = next;
        }
        if (EbDesktopMenuShellState::scrollTarget() > maxY) {
            EbDesktopMenuShellState::scrollTarget() = maxY;
        }
        EbDesktopMenuShellState::scrollVisual() = EbDesktopMenuMotion::approach(
            EbDesktopMenuShellState::scrollVisual(), EbDesktopMenuShellState::scrollTarget(), 0.06f,
            ImGui::GetIO().DeltaTime);
        if (std::fabs(EbDesktopMenuShellState::scrollVisual() - scrollBefore) > 0.5f) {
            scrolled = true;
        }
        if (scrolled) {
            EbDesktopMenuShell::requestDismissFloatingPopups();
        }
        ImGui::SetScrollY(EbDesktopMenuShellState::scrollVisual());

        SettingsCategory const active = EbDesktopMenuShellState::active();
        EbDesktopMenuPanels::draw(active);
        // No extra end Dummy — AlwaysUseWindowPadding already mirrors top kContentPadY
        // at the bottom. (Old Dummy(pad+24) made the scrolled-end gutter ~2× the top.)

        ImGui::EndChild();
        ImGui::PopStyleVar(4);
    }

    static void frame() {
        float const deltaTime = (std::max)(0.0f, ImGui::GetIO().DeltaTime);
        EbDesktopMenuMotion::beginFrame();
        EbDesktopMenuShellState::dismissFloatingPopups() = false;
        // Live accent — ImGui style + wave chrome pick up desktop-accent-color edits.
        EbDesktopMenuTheme::apply();
        EbDesktopMenuShellAnim::updateOpen(deltaTime);
        EbDesktopMenuShellAnim::updateTab(deltaTime);

        float const openE = EbDesktopMenuShellState::openVisual();
        if (openE <= 0.001f && !EbDesktopMenuShellState::wantOpen()) {
            EbDesktopMenuShellState::haveAnchor() = false;
            EbDesktopMenuShellState::dragging() = false;
            EbDesktopMenuShellState::closing() = false;
            EbDesktopMenuShellAnim::clearTrailToCenter();
            EbDesktopMenuShellState::prevOpenVisual() = openE;
            return;
        }

        ImGuiIO const& io = ImGui::GetIO();
        ImVec2 const screenCenter(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        ImVec2 const winSize(EbDesktopMenuShellState::kMenuWidth,
                             EbDesktopMenuShellState::kMenuHeight);

        // Fresh open from closed — re-anchor to screen center.
        if (!EbDesktopMenuShellState::haveAnchor() ||
            (EbDesktopMenuShellState::prevOpenVisual() <= 0.01f && openE > 0.01f)) {
            EbDesktopMenuShellState::targetCenter() = screenCenter;
            EbDesktopMenuShellState::visualCenter() = screenCenter;
            for (int index = 0; index < EbDesktopMenuShellState::kTrailCount; ++index) {
                EbDesktopMenuShellState::trail()[index] = screenCenter;
            }
            EbDesktopMenuShellState::haveAnchor() = true;
            EbDesktopMenuShellState::dragging() = false;
        }

        // Drag continues from last frame's empty-space grab.
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            EbDesktopMenuShellState::dragging() = false;
        } else if (EbDesktopMenuShellState::dragging() && EbDesktopMenuShellState::wantOpen() &&
                   openE > 0.98f) {
            EbDesktopMenuShellState::targetCenter().x += io.MouseDelta.x;
            EbDesktopMenuShellState::targetCenter().y += io.MouseDelta.y;
        }

        EbDesktopMenuShellState::visualCenter() = EbDesktopMenuMotion::approach2(
            EbDesktopMenuShellState::visualCenter(), EbDesktopMenuShellState::targetCenter(),
            EbDesktopMenuShellState::kDragFollowTau, deltaTime);

        ImVec2 const drift(
            EbDesktopMenuShellState::targetCenter().x - EbDesktopMenuShellState::visualCenter().x,
            EbDesktopMenuShellState::targetCenter().y - EbDesktopMenuShellState::visualCenter().y);
        float const driftLen = std::sqrt(drift.x * drift.x + drift.y * drift.y);
        // Never draw trail ghosts while closing — they felt like an end hitch.
        bool const showTrail = EbDesktopMenuShellState::wantOpen() &&
                               (EbDesktopMenuShellState::dragging() || driftLen > 2.5f);

        ImVec2* trail = EbDesktopMenuShellState::trail();
        if (showTrail) {
            trail[0] = EbDesktopMenuMotion::approach2(
                trail[0], EbDesktopMenuShellState::visualCenter(), 0.035f, deltaTime);
            for (int index = 1; index < EbDesktopMenuShellState::kTrailCount; ++index) {
                trail[index] = EbDesktopMenuMotion::approach2(trail[index], trail[index - 1], 0.045f,
                                                             deltaTime);
            }
        } else {
            EbDesktopMenuShellAnim::clearTrailToCenter();
        }

        float const slideY = (1.0f - openE) * EbDesktopMenuShellState::kOpenSlidePx;
        ImVec2 const displayCenter(EbDesktopMenuShellState::visualCenter().x,
                                   EbDesktopMenuShellState::visualCenter().y + slideY);
        float const rounding = EbDesktopMenuTheme::windowRounding();

        // Behind the real window only, and only while moving — never overlays the UI.
        if (showTrail) {
            if (ImDrawList* bg = ImGui::GetBackgroundDrawList()) {
                for (int index = EbDesktopMenuShellState::kTrailCount - 1; index >= 0; --index) {
                    float const trailAlpha =
                        openE * 0.14f *
                        (1.0f - static_cast<float>(index) /
                                    static_cast<float>(EbDesktopMenuShellState::kTrailCount));
                    ImVec2 ghost(trail[index].x, trail[index].y + slideY);
                    drawShellGhost(bg, ghost, winSize, rounding, trailAlpha);
                }
            }
        }

        ImGui::SetNextWindowSize(winSize, ImGuiCond_Always);
        ImGui::SetNextWindowPos(displayCenter, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, openE);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground |
                                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoMove;
        // Block input only while still opening / closing — unlock early so clicks work.
        if (openE < 0.85f || !EbDesktopMenuShellState::wantOpen()) {
            flags |= ImGuiWindowFlags_NoInputs;
        }
        if (!ImGui::Begin("##eb-desktop-shell", nullptr, flags)) {
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(4);
            EbDesktopMenuShellState::prevOpenVisual() = openE;
            return;
        }

        // Esc: cancel keybind capture first, otherwise close the menu.
        if (EbDesktopMenuShellState::wantOpen() && openE > 0.9f &&
            ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            if (EbKeybind::isCapturing()) {
                EbKeybind::cancelCapture();
            } else {
                EbDesktopMenuShell::requestOpen(false);
            }
        }

        ImVec2 const winPos = ImGui::GetWindowPos();
        float const sideW = EbDesktopMenuShellState::kSidebarWidth;
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Unified shell — sidebar docked flush left (outer corners only).
        drawList->AddRectFilled(winPos, ImVec2(winPos.x + winSize.x, winPos.y + winSize.y),
                                EbDesktopMenuTheme::toU32(EbDesktopMenuTheme::bg()), rounding);

        ImVec2 const sideMin = winPos;
        ImVec2 const sideMax(winPos.x + sideW, winPos.y + winSize.y);
        EbDesktopMenuSidebarSmoke::get().paint(drawList, sideMin, sideMax, rounding,
                                               static_cast<float>(ImGui::GetTime()),
                                               ImDrawFlags_RoundCornersLeft);

        drawSidebarNav(sideMin, sideW);
        drawSidebarFooter(sideMin, sideW, winSize.y);

        float const tabE = EbDesktopMenuShellState::tabVisual();
        float const tabSlide =
            (1.0f - tabE) * static_cast<float>(EbDesktopMenuShellState::tabDir()) *
            EbDesktopMenuShellState::kTabSlidePx;
        // Soft fade — stays readable on interrupt (dip floors around kTabDip).
        float const tabAlpha = 0.55f + 0.45f * tabE;
        drawContentBody(ImVec2(winPos.x + sideW, winPos.y), winSize.x - sideW, winSize.y, tabAlpha,
                        tabSlide);

        // Drag only after widgets ran this frame — early checks stole presses onto empty gaps.
        bool const overChrome = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        if (EbDesktopMenuShellState::wantOpen() && openE > 0.85f &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) && overChrome &&
            !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive()) {
            EbDesktopMenuShellState::dragging() = true;
        }

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(4);
        EbDesktopMenuShellState::prevOpenVisual() = openE;
    }

private:
    EbDesktopMenuShellDraw() = delete;
};

SettingsCategory EbDesktopMenuShell::activeCategory() {
    return EbDesktopMenuShellState::active();
}

void EbDesktopMenuShell::setActiveCategory(SettingsCategory category) {
    EbDesktopMenuShellAnim::beginTabSwitch(category);
}

void EbDesktopMenuShell::requestOpen(bool open) {
    EbDesktopMenuShellState::wantOpen() = open;
    if (!open) {
        EbKeybind::cancelCapture();
    }
}

bool EbDesktopMenuShell::wantsOpen() {
    return EbDesktopMenuShellState::wantOpen();
}

bool EbDesktopMenuShell::shouldKeepVisible() {
    return EbDesktopMenuShellState::wantOpen() || EbDesktopMenuShellState::openVisual() > 0.001f;
}

void EbDesktopMenuShell::addWheelDelta(float deltaY) {
    EbDesktopMenuShellState::wheelDelta() += deltaY;
}

float EbDesktopMenuShell::takeWheelDelta() {
    float const delta = EbDesktopMenuShellState::wheelDelta();
    EbDesktopMenuShellState::wheelDelta() = 0.0f;
    return delta;
}

void EbDesktopMenuShell::requestDismissFloatingPopups() {
    EbDesktopMenuShellState::dismissFloatingPopups() = true;
}

bool EbDesktopMenuShell::shouldDismissFloatingPopups() {
    return EbDesktopMenuShellState::dismissFloatingPopups();
}

void EbDesktopMenuShell::draw() {
    EbDesktopMenuShellDraw::frame();
}

#else

SettingsCategory EbDesktopMenuShell::activeCategory() {
    return SettingsCategory::Main;
}

void EbDesktopMenuShell::setActiveCategory(SettingsCategory category) {
    (void)category;
}

void EbDesktopMenuShell::requestOpen(bool) {}

bool EbDesktopMenuShell::wantsOpen() {
    return false;
}

bool EbDesktopMenuShell::shouldKeepVisible() {
    return false;
}

void EbDesktopMenuShell::addWheelDelta(float) {}

float EbDesktopMenuShell::takeWheelDelta() {
    return 0.0f;
}

void EbDesktopMenuShell::requestDismissFloatingPopups() {}

bool EbDesktopMenuShell::shouldDismissFloatingPopups() {
    return false;
}

void EbDesktopMenuShell::draw() {}

#endif
