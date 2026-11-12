#include "desktop_menu_cloud_overlay.hpp"
#include "desktop_menu_fonts.hpp"
#include "desktop_menu_modal.hpp"
#include "desktop_menu_motion.hpp"
#include "desktop_menu_theme.hpp"
#include "widgets/eb_button.hpp"
#include "widgets/eb_keybind.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include "../../cloud/catalog/cloud_config_index.hpp"
#include "../../cloud/catalog/cloud_config_service.hpp"
#include "../../cloud/cloud_config_stats.hpp"
#include "../../cloud/cloud_config_store.hpp"
#include <Geode/Geode.hpp>
#include <imgui.h>
#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <string>

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

    static uint64_t& sessionGen() {
        static uint64_t value = 0;
        return value;
    }

    static uint64_t& opGen() {
        static uint64_t value = 0;
        return value;
    }

    static uint64_t& statsGen() {
        static uint64_t value = 0;
        return value;
    }

    static bool& busyUi() {
        static bool value = false;
        return value;
    }

    static bool& statusError() {
        static bool value = false;
        return value;
    }

    static std::string& statusText() {
        static std::string value;
        return value;
    }

    static std::string& selectedId() {
        static std::string value;
        return value;
    }

    static std::string& activeId() {
        static std::string value;
        return value;
    }

    static size_t& packCount() {
        static size_t value = 0;
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

class EbDesktopCloudOverlayCatalog {
public:
    static bool sessionLive(uint64_t generation) {
        return generation == EbDesktopCloudOverlayState::sessionGen() &&
               EbDesktopCloudOverlayState::wantOpen();
    }

    static void setStatus(std::string text, bool isError) {
        EbDesktopCloudOverlayState::statusText() = std::move(text);
        EbDesktopCloudOverlayState::statusError() = isError;
    }

    static void syncPackCount() {
        EbDesktopCloudOverlayState::packCount() =
            EbCloudConfigService::get().catalog().packs.size();
    }

    static void ensureSelection() {
        auto const& packs = EbCloudConfigService::get().catalog().packs;
        if (packs.empty()) {
            return;
        }
        std::string& selected = EbDesktopCloudOverlayState::selectedId();
        if (selected.empty() || !EbCloudConfigIndex::findById(EbCloudConfigService::get().catalog(),
                                                              selected)) {
            selected = packs.front().id;
        }
    }

    static void ensureLocalCatalog() {
        if (!EbCloudConfigService::get().catalog().packs.empty()) {
            return;
        }
        EbCloudConfigService::RefreshResult local{};
        (void)EbCloudConfigService::get().loadLocalCatalog(local);
    }

    static void startStatsFetch() {
        uint64_t const session = EbDesktopCloudOverlayState::sessionGen();
        uint64_t const generation = ++EbDesktopCloudOverlayState::statsGen();
        EbCloudConfigStats::get().fetchStats(
            [session, generation](bool ok, std::string const&) {
                if (!sessionLive(session) ||
                    generation != EbDesktopCloudOverlayState::statsGen()) {
                    return;
                }
                if (!ok) {
                    return;
                }
                // AP4 will rebuild list badges from cached stats.
                syncPackCount();
            });
    }

    static void onRefreshDone(EbCloudConfigService::RefreshResult const& result) {
        EbDesktopCloudOverlayState::busyUi() = false;

        if (result.busy) {
            setStatus("Busy…", false);
            return;
        }
        if (!result.ok) {
            ensureLocalCatalog();
            syncPackCount();
            size_t const count = EbDesktopCloudOverlayState::packCount();
            setStatus(fmt::format("Offline · {} packs", count), count == 0);
            ensureSelection();
            startStatsFetch();
            return;
        }

        char const* source = result.usedFallback ? "Online*" : "Online";
        std::string stamp = result.catalog.updatedAt;
        if (stamp.size() > 16) {
            stamp = stamp.substr(0, 16);
        }
        if (!stamp.empty()) {
            setStatus(fmt::format("{} · {} packs · {}", source, result.catalog.packs.size(), stamp),
                      false);
        } else {
            setStatus(fmt::format("{} · {} packs", source, result.catalog.packs.size()), false);
        }
        if (result.catalog.packs.empty()) {
            ensureLocalCatalog();
        }
        syncPackCount();
        ensureSelection();
        startStatsFetch();
    }

    static void startRefresh(bool force) {
        if (EbDesktopCloudOverlayState::busyUi() || EbCloudConfigService::get().isBusy()) {
            setStatus("Busy…", false);
            return;
        }
        EbDesktopCloudOverlayState::busyUi() = true;
        uint64_t const session = EbDesktopCloudOverlayState::sessionGen();
        uint64_t const generation = ++EbDesktopCloudOverlayState::opGen();
        setStatus(force ? "Refreshing (force)…" : "Refreshing…", false);

        std::string const custom = EbCloudConfigService::readCatalogUrlSetting();
        EbCloudConfigService::get().refreshCatalog(
            custom,
            [session, generation](EbCloudConfigService::RefreshResult const& result) {
                if (!sessionLive(session) || generation != EbDesktopCloudOverlayState::opGen()) {
                    return;
                }
                onRefreshDone(result);
            },
            force);
    }

    static void loadInitial() {
        EbCloudConfigStore::ActiveInfo active{};
        std::string err;
        if (EbCloudConfigStore::get().readActive(active, err)) {
            EbDesktopCloudOverlayState::activeId() = active.id;
        } else {
            EbDesktopCloudOverlayState::activeId().clear();
        }

        ensureLocalCatalog();
        syncPackCount();
        auto const& cat = EbCloudConfigService::get().catalog();
        char const* source =
            EbCloudConfigService::get().source() == EbCloudConfigService::CatalogSource::Seed
                ? "Seed"
            : EbCloudConfigService::get().source() == EbCloudConfigService::CatalogSource::DiskCache
                ? "Cache"
                : "Local";
        setStatus(fmt::format("{} · {} packs", source, cat.packs.size()), cat.packs.empty());
        ensureSelection();
        startStatsFetch();
        startRefresh(false);
    }

private:
    EbDesktopCloudOverlayCatalog() = delete;
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

    static void drawHeader(float innerW) {
        EbDesktopMenuFonts::pushUi();
        EbDesktopMenuFonts::pushCaps();
        ImGui::PushStyleColor(ImGuiCol_Text, EbDesktopMenuTheme::text());
        ImGui::TextUnformatted("Cloud Configs");
        ImGui::PopStyleColor();
        EbDesktopMenuFonts::pop();

        ImGui::PushStyleColor(ImGuiCol_Text, EbDesktopCloudOverlayState::statusError()
                                                 ? EbDesktopMenuTheme::danger()
                                                 : EbDesktopMenuTheme::muted());
        ImGui::PushTextWrapPos(ImGui::GetCursorScreenPos().x + innerW);
        ImGui::TextUnformatted(EbDesktopCloudOverlayState::statusText().c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();

        bool const canRefresh = !EbDesktopCloudOverlayState::busyUi();
        if (EbButton::draw("##eb-cloud-refresh", "Refresh", canRefresh)) {
            EbDesktopCloudOverlayCatalog::startRefresh(true);
        }

        EbDesktopMenuFonts::pop();
    }

    static void drawBody(float innerW) {
        drawHeader(innerW);

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, EbDesktopMenuTheme::muted());
        ImGui::PushTextWrapPos(ImGui::GetCursorScreenPos().x + innerW);
        ImGui::TextUnformatted(
            "Browse, install, rate, and share community packs. List UI lands in AP4.");
        if (!EbDesktopCloudOverlayState::selectedId().empty()) {
            ImGui::Text("Selected: %s", EbDesktopCloudOverlayState::selectedId().c_str());
        }
        if (!EbDesktopCloudOverlayState::activeId().empty()) {
            ImGui::Text("Active: %s", EbDesktopCloudOverlayState::activeId().c_str());
        }
        ImGui::Text("Packs in catalog: %zu", EbDesktopCloudOverlayState::packCount());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();

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

        drawBody(cardW - pad * 2.0f);

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
    ++EbDesktopCloudOverlayState::sessionGen();
    EbDesktopCloudOverlayState::busyUi() = false;
    EbDesktopCloudOverlayState::wantOpen() = true;
    EbDesktopCloudOverlayCatalog::loadInitial();
}

void EbDesktopCloudOverlay::dismiss() {
    if (!EbDesktopCloudOverlayState::wantOpen() &&
        EbDesktopCloudOverlayState::visual() <= 0.001f) {
        return;
    }
    EbDesktopCloudOverlayState::shareOpen() = false;
    EbDesktopCloudOverlayState::wantOpen() = false;
    ++EbDesktopCloudOverlayState::sessionGen();
    EbDesktopCloudOverlayState::busyUi() = false;
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
