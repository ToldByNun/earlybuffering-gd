#include "desktop_menu_cloud_overlay.hpp"
#include "desktop_menu_alert.hpp"
#include "desktop_menu_fonts.hpp"
#include "desktop_menu_modal.hpp"
#include "desktop_menu_motion.hpp"
#include "desktop_menu_shell.hpp"
#include "desktop_menu_sidebar_smoke.hpp"
#include "desktop_menu_theme.hpp"
#include "widgets/eb_button.hpp"
#include "widgets/eb_combo_box.hpp"
#include "widgets/eb_keybind.hpp"
#include "widgets/eb_text_field.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include "../../cloud/catalog/cloud_config_index.hpp"
#include "../../cloud/catalog/cloud_config_service.hpp"
#include "../../cloud/cloud_config_apply.hpp"
#include "../../cloud/cloud_config_stats.hpp"
#include "../../cloud/cloud_config_store.hpp"
#include "../pause/pause_action_bridge.hpp"
#include <Geode/Geode.hpp>
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

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

    static std::string& searchQuery() {
        static std::string value;
        return value;
    }

    // 0 Default, 1 Downloads, 2 Rating, 3 Popular, 4 Name
    static int& sortMode() {
        static int value = 0;
        return value;
    }

    // Focus the sheet once on open — every-frame focus kills combo popups.
    static bool& needWindowFocus() {
        static bool value = false;
        return value;
    }

    static float& wheelDelta() {
        static float value = 0.0f;
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

    static float& detailScrollTarget() {
        static float value = 0.0f;
        return value;
    }

    static float& detailScrollVisual() {
        static float value = 0.0f;
        return value;
    }

    static constexpr float kOpenTau = 0.09f;
    static constexpr float kCloseDuration = 0.20f;
    static constexpr float kCardW = 780.0f;
    static constexpr float kCardH = 520.0f;
    static constexpr float kSlidePx = 14.0f;
    static constexpr float kRailW = 68.0f;
    static constexpr float kContentPad = 20.0f;
    static constexpr float kBlockH = 74.0f;
    static constexpr float kBlockGap = 8.0f;
    static constexpr int kSortCount = 5;

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

class EbDesktopCloudOverlayInstall {
public:
    static std::string liveModVersion() {
        if (auto* mod = geode::prelude::Mod::get()) {
            return mod->getVersion().toVString();
        }
        return "0.0.0";
    }

    static void requestInstall() {
        auto const* entry = EbCloudConfigIndex::findById(
            EbCloudConfigService::get().catalog(), EbDesktopCloudOverlayState::selectedId());
        if (!entry || EbDesktopCloudOverlayState::busyUi()) {
            return;
        }
        if (!EbCloudConfigIndex::meetsMinModVersion(*entry, liveModVersion())) {
            EbDesktopAlert::info("Cloud Configs",
                                 "This pack needs a newer EarlyBuffer version.");
            return;
        }

        std::string const id = entry->id;
        EbDesktopAlert::confirm(
            "Install config?",
            "Replace current EarlyBuffer settings?\nKeybinds and Dev Lab keys stay unchanged.",
            [id] { doInstall(id); }, {}, "Install", "Cancel");
    }

    static void doInstall(std::string const& id) {
        auto const* entryValue =
            EbCloudConfigIndex::findById(EbCloudConfigService::get().catalog(), id);
        if (!entryValue) {
            return;
        }
        if (EbDesktopCloudOverlayState::busyUi() || EbCloudConfigService::get().isBusy()) {
            EbDesktopCloudOverlayCatalog::setStatus("Busy…", false);
            return;
        }

        EbDesktopCloudOverlayState::busyUi() = true;
        uint64_t const session = EbDesktopCloudOverlayState::sessionGen();
        uint64_t const generation = ++EbDesktopCloudOverlayState::opGen();
        EbDesktopCloudOverlayCatalog::setStatus("Downloading…", false);

        EbCloudConfigIndex::PackEntry entry = *entryValue;
        // Force network when the cached body/active sha no longer matches the catalog.
        bool forceNetwork = false;
        if (EbCloudConfigStore::get().hasPack(entry.id)) {
            std::string body;
            std::string err;
            if (EbCloudConfigStore::get().readPack(entry.id, body, err)) {
                EbCloudConfigStore::ActiveInfo active{};
                if (EbCloudConfigStore::get().readActive(active, err) && active.id == entry.id &&
                    !active.sha256.empty()) {
                    forceNetwork = active.sha256 != entry.sha256;
                } else {
                    forceNetwork = body.size() != entry.size;
                }
            }
        }
        EbCloudConfigService::get().fetchPack(
            id, forceNetwork,
            [session, generation, entry](EbCloudConfigService::PackFetchResult const& fr) {
                if (!EbDesktopCloudOverlayCatalog::sessionLive(session) ||
                    generation != EbDesktopCloudOverlayState::opGen()) {
                    return;
                }
                onPackFetched(fr, entry);
            });
    }

    static void onPackFetched(EbCloudConfigService::PackFetchResult const& fr,
                              EbCloudConfigIndex::PackEntry entry) {
        EbDesktopCloudOverlayState::busyUi() = false;

        if (fr.busy) {
            EbDesktopCloudOverlayCatalog::setStatus("Busy…", false);
            return;
        }
        if (fr.cancelled) {
            EbDesktopCloudOverlayCatalog::setStatus("Cancelled", true);
            return;
        }
        if (!fr.ok) {
            EbDesktopCloudOverlayCatalog::setStatus("Download failed", true);
            EbDesktopAlert::info(
                "Cloud Configs",
                fr.error.empty() ? "Could not download pack." : fr.error.c_str());
            return;
        }

        auto const applied = EbCloudConfigApply::applyPackBody(entry.id, entry.name, entry.sha256,
                                                               entry.size, fr.body);
        if (!applied.ok) {
            EbDesktopCloudOverlayCatalog::setStatus("Install failed", true);
            EbDesktopAlert::info("Cloud Configs", applied.error.c_str());
            return;
        }

        EbDesktopCloudOverlayState::activeId() = entry.id;
        EbDesktopCloudOverlayCatalog::setStatus(fmt::format("Installed · {}", entry.name), false);
        trackInstallDownload(entry.id);

        EbPauseActionBridge::afterConfigApplied(fmt::format(
            "Installed cloud config \"{}\".\nApplied {} settings ({} skipped).", entry.name,
            applied.stats.applied, applied.stats.skipped));
    }

    static void trackInstallDownload(std::string const& packId) {
        uint64_t const session = EbDesktopCloudOverlayState::sessionGen();
        EbCloudConfigStats::get().trackDownload(
            packId, [session](EbCloudConfigStats::TrackResult const& result) {
                if (!EbDesktopCloudOverlayCatalog::sessionLive(session)) {
                    return;
                }
                // Badge refresh is frame-driven from stats cache; bump gens so list
                // rebuilds stay coherent if we later cache filtered rows.
                if (result.ok || result.duplicate || result.skippedLocal) {
                    ++EbDesktopCloudOverlayState::statsGen();
                }
            });
    }

private:
    EbDesktopCloudOverlayInstall() = delete;
};

class EbDesktopCloudOverlayList {
public:
    static std::string toLower(std::string text) {
        for (char& c : text) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return text;
    }

    static bool containsInsensitive(std::string const& hay, std::string const& needleLower) {
        if (needleLower.empty()) {
            return true;
        }
        return toLower(hay).find(needleLower) != std::string::npos;
    }

    static bool packMatchesSearch(EbCloudConfigIndex::PackEntry const& entry) {
        auto const query = toLower(EbDesktopCloudOverlayState::searchQuery());
        if (query.empty()) {
            return true;
        }
        if (containsInsensitive(entry.name, query) || containsInsensitive(entry.author, query) ||
            containsInsensitive(entry.id, query) || containsInsensitive(entry.description, query)) {
            return true;
        }
        for (auto const& tag : entry.tags) {
            if (containsInsensitive(tag, query)) {
                return true;
            }
        }
        return false;
    }

    static bool isCached(std::string const& id) {
        return EbCloudConfigStore::get().hasPack(id);
    }

    static bool isActive(std::string const& id) {
        return !id.empty() && id == EbDesktopCloudOverlayState::activeId();
    }

    static bool hasUpdate(EbCloudConfigIndex::PackEntry const& entry) {
        if (!isCached(entry.id)) {
            return false;
        }
        std::string body;
        std::string err;
        if (!EbCloudConfigStore::get().readPack(entry.id, body, err)) {
            return false;
        }
        EbCloudConfigStore::ActiveInfo active{};
        if (EbCloudConfigStore::get().readActive(active, err) && active.id == entry.id &&
            !active.sha256.empty()) {
            return active.sha256 != entry.sha256;
        }
        return body.size() != entry.size;
    }

    static std::string formatDlBadge(int count) {
        if (count >= 1000000) {
            return fmt::format("{}M", count / 1000000);
        }
        if (count >= 10000) {
            return fmt::format("{}k", count / 1000);
        }
        return fmt::format("{}", count);
    }

    static void collectRows(std::vector<EbCloudConfigIndex::PackEntry const*>& rows) {
        rows.clear();
        auto const& catalog = EbCloudConfigService::get().catalog();
        rows.reserve(catalog.packs.size());
        for (auto const& pack : catalog.packs) {
            if (packMatchesSearch(pack)) {
                rows.push_back(&pack);
            }
        }
        applySort(rows);
    }

    static void applySort(std::vector<EbCloudConfigIndex::PackEntry const*>& rows) {
        if (rows.size() < 2) {
            return;
        }
        int const mode = EbDesktopCloudOverlayState::sortMode();
        if (mode <= 0) {
            return;
        }
        std::stable_sort(
            rows.begin(), rows.end(),
            [mode](EbCloudConfigIndex::PackEntry const* a, EbCloudConfigIndex::PackEntry const* b) {
                if (!a || !b) {
                    return a != nullptr;
                }
                auto const sa = EbCloudConfigStats::get().statsFor(a->id);
                auto const sb = EbCloudConfigStats::get().statsFor(b->id);
                switch (mode) {
                case 1:
                    if (sa.downloads != sb.downloads) {
                        return sa.downloads > sb.downloads;
                    }
                    break;
                case 2: {
                    float const ra = EbCloudConfigStats::averageRating(sa);
                    float const rb = EbCloudConfigStats::averageRating(sb);
                    if (ra != rb) {
                        return ra > rb;
                    }
                    if (sa.ratingCount != sb.ratingCount) {
                        return sa.ratingCount > sb.ratingCount;
                    }
                    break;
                }
                case 3: {
                    float const pa = EbCloudConfigStats::popularityScore(sa);
                    float const pb = EbCloudConfigStats::popularityScore(sb);
                    if (pa != pb) {
                        return pa > pb;
                    }
                    break;
                }
                case 4: {
                    auto const na = toLower(a->name);
                    auto const nb = toLower(b->name);
                    if (na != nb) {
                        return na < nb;
                    }
                    break;
                }
                default:
                    break;
                }
                return a->id < b->id;
            });
    }

    // Saved/offline checkmark — no pill background (download glyph reserved for stats).
    static void drawSavedIcon(ImDrawList* drawList, ImVec2 const& topRight) {
        float const size = 11.0f;
        ImVec2 const origin(topRight.x - size - 2.0f, topRight.y + 2.0f);
        ImVec2 const p0(origin.x + size * 0.10f, origin.y + size * 0.52f);
        ImVec2 const p1(origin.x + size * 0.38f, origin.y + size * 0.82f);
        ImVec2 const p2(origin.x + size * 0.92f, origin.y + size * 0.18f);
        ImVec4 color = EbDesktopMenuTheme::accentLight();
        color.w = 0.95f;
        ImU32 const col = EbDesktopMenuTheme::toU32(color);
        drawList->AddLine(p0, p1, col, 2.0f);
        drawList->AddLine(p1, p2, col, 2.0f);
    }

    // Download tray + arrow (trackDownload / catalog download counts).
    static void drawDownloadIcon(ImDrawList* drawList, ImVec2 const& topLeft, float size,
                                 ImU32 color) {
        if (!drawList || size < 4.0f) {
            return;
        }
        float const midX = topLeft.x + size * 0.5f;
        float const thick = (std::max)(1.4f, size * 0.14f);
        drawList->AddLine(ImVec2(midX, topLeft.y + size * 0.05f),
                          ImVec2(midX, topLeft.y + size * 0.52f), color, thick);
        drawList->AddLine(ImVec2(midX - size * 0.28f, topLeft.y + size * 0.34f),
                          ImVec2(midX, topLeft.y + size * 0.56f), color, thick);
        drawList->AddLine(ImVec2(midX + size * 0.28f, topLeft.y + size * 0.34f),
                          ImVec2(midX, topLeft.y + size * 0.56f), color, thick);
        drawList->AddLine(ImVec2(topLeft.x + size * 0.12f, topLeft.y + size * 0.70f),
                          ImVec2(topLeft.x + size * 0.12f, topLeft.y + size * 0.92f), color, thick);
        drawList->AddLine(ImVec2(topLeft.x + size * 0.12f, topLeft.y + size * 0.92f),
                          ImVec2(topLeft.x + size * 0.88f, topLeft.y + size * 0.92f), color, thick);
        drawList->AddLine(ImVec2(topLeft.x + size * 0.88f, topLeft.y + size * 0.92f),
                          ImVec2(topLeft.x + size * 0.88f, topLeft.y + size * 0.70f), color, thick);
    }

    static void drawPackCard(EbCloudConfigIndex::PackEntry const& entry, int index, float cellW,
                             float cellH) {
        bool const selected = entry.id == EbDesktopCloudOverlayState::selectedId();
        bool const active = isActive(entry.id);
        bool const cached = isCached(entry.id);
        bool const update = hasUpdate(entry);
        auto const stats = EbCloudConfigStats::get().statsFor(entry.id);

        float const rounding = EbDesktopMenuTheme::frameRounding();
        ImVec2 const min = ImGui::GetCursorScreenPos();
        ImVec2 const max(min.x + cellW, min.y + cellH);

        ImGui::PushID(entry.id.c_str());
        bool pressed = ImGui::InvisibleButton("##pack-card", ImVec2(cellW, cellH));
        bool const hovered = ImGui::IsItemHovered();
        float const heat =
            EbDesktopMenuMotion::smoothToward(ImGui::GetItemID(), hovered || selected, 0.05f);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec4 fill = EbDesktopMenuTheme::panel();
        fill.w = 0.78f;
        if (selected) {
            EbDesktopMenuTheme::drawAccentGlow(drawList, min, max, rounding, 0.34f + 0.16f * heat);
            ImVec4 accentWash = EbDesktopMenuTheme::accent();
            accentWash.w = 0.24f;
            fill = EbDesktopMenuMotion::lerp4(EbDesktopMenuTheme::panel(), accentWash, 0.95f);
            fill.w = 1.0f;
        } else if (heat > 0.01f) {
            ImVec4 lift = EbDesktopMenuTheme::surface();
            lift.w = 1.0f;
            fill = EbDesktopMenuMotion::lerp4(fill, lift, heat);
            fill.w = 0.94f;
        }
        drawList->AddRectFilled(min, max, EbDesktopMenuTheme::toU32(fill), rounding);

        ImVec4 stroke = EbDesktopMenuTheme::outline();
        stroke.w = selected ? 0.75f : (0.32f + 0.40f * heat);
        if (selected) {
            ImVec4 accentRim = EbDesktopMenuTheme::accentLight();
            accentRim.w = 0.60f + 0.30f * heat;
            stroke = EbDesktopMenuMotion::lerp4(stroke, accentRim, 0.90f);
        }
        drawList->AddRect(min, max, EbDesktopMenuTheme::toU32(stroke), rounding, 0,
                          EbDesktopMenuTheme::outlineThickness());

        if (selected || active) {
            drawList->AddRectFilled(ImVec2(min.x + 3.0f, min.y + 10.0f),
                                    ImVec2(min.x + 6.0f, max.y - 10.0f),
                                    EbDesktopMenuTheme::toU32(active && !selected
                                                                  ? ImVec4(0.40f, 1.0f, 0.55f, 0.90f)
                                                                  : EbDesktopMenuTheme::accentLight()),
                                    2.0f);
        }

        // Top-right status marks (no backgrounds).
        if (cached) {
            drawSavedIcon(drawList, ImVec2(max.x - 6.0f, min.y + 6.0f));
        }
        if (update) {
            ImVec2 const tip(max.x - 14.0f, min.y + 8.0f);
            ImVec4 warn(1.0f, 0.82f, 0.35f, 0.95f);
            drawList->AddTriangleFilled(ImVec2(tip.x, tip.y + 11.0f), ImVec2(tip.x + 10.0f, tip.y + 11.0f),
                                        ImVec2(tip.x + 5.0f, tip.y),
                                        EbDesktopMenuTheme::toU32(warn));
        }

        float const textLeft = min.x + ((selected || active) ? 14.0f : 12.0f);
        float const textRight = max.x - (cached || update ? 26.0f : 12.0f);
        ImVec4 const clip(textLeft, min.y + 2.0f, textRight, max.y - 2.0f);

        ImVec4 nameColor = EbDesktopMenuTheme::text();
        if (!selected) {
            nameColor = EbDesktopMenuMotion::lerp4(EbDesktopMenuTheme::muted(), nameColor,
                                                   0.78f + 0.22f * heat);
        }
        float const nameSize = ImGui::GetFontSize();
        drawList->AddText(ImGui::GetFont(), nameSize, ImVec2(textLeft, min.y + 12.0f),
                          EbDesktopMenuTheme::toU32(nameColor), entry.name.c_str(), nullptr, 0.0f,
                          &clip);

        std::string meta =
            entry.author.empty() ? entry.id : fmt::format("{} · {}", entry.author, entry.id);
        ImVec4 muted = EbDesktopMenuTheme::muted();
        muted.w = 0.88f;
        drawList->AddText(ImGui::GetFont(), nameSize * 0.90f, ImVec2(textLeft, min.y + 34.0f),
                          EbDesktopMenuTheme::toU32(muted), meta.c_str(), nullptr, 0.0f, &clip);

        if (stats.downloads > 0 || stats.ratingCount > 0) {
            ImVec4 statsColor = EbDesktopMenuTheme::muted();
            statsColor.w = 0.92f;
            ImU32 const statsCol = EbDesktopMenuTheme::toU32(statsColor);
            float const statsSize = nameSize * 0.86f;
            float const rowY = max.y - 22.0f;
            float cursorX = textLeft;
            if (stats.downloads > 0) {
                float const iconSize = 11.0f;
                drawDownloadIcon(drawList, ImVec2(cursorX, rowY + 1.0f), iconSize, statsCol);
                cursorX += iconSize + 4.0f;
                std::string const dlText = formatDlBadge(stats.downloads);
                drawList->AddText(ImGui::GetFont(), statsSize, ImVec2(cursorX, rowY), statsCol,
                                  dlText.c_str(), nullptr, 0.0f, &clip);
                cursorX +=
                    ImGui::GetFont()->CalcTextSizeA(statsSize, FLT_MAX, 0.0f, dlText.c_str()).x +
                    10.0f;
            }
            if (stats.ratingCount > 0) {
                std::string const rateText =
                    fmt::format("{:.1f}★", EbCloudConfigStats::averageRating(stats));
                drawList->AddText(ImGui::GetFont(), statsSize, ImVec2(cursorX, rowY), statsCol,
                                  rateText.c_str(), nullptr, 0.0f, &clip);
            }
        }

        if (pressed) {
            EbDesktopCloudOverlayState::selectedId() = entry.id;
        }
        ImGui::PopID();
        (void)index;
    }

    static void drawToolbar(float width) {
        // Full-width search, then full-width sort row — combo needs real avail width
        // so "Downloads" fits (label left + field right), not a cramped SameLine stub.
        EbTextField::draw("##eb-cloud-search", EbDesktopCloudOverlayState::searchQuery(),
                          "Search packs…", true, 64, width);
        ImGui::Dummy(ImVec2(0.0f, 2.0f));

        static char const* sortOptions[] = {"Default", "Downloads", "Rating", "Popular", "Name"};
        int sortIndex = EbDesktopCloudOverlayState::sortMode();
        if (sortIndex < 0 || sortIndex >= EbDesktopCloudOverlayState::kSortCount) {
            sortIndex = 0;
        }
        if (EbComboBox::draw("##eb-cloud-sort", "Sort", sortOptions,
                             EbDesktopCloudOverlayState::kSortCount, &sortIndex)) {
            EbDesktopCloudOverlayState::sortMode() = sortIndex;
        }
    }

    static void drawListPane(float width, float height) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::BeginChild("##eb-cloud-list", ImVec2(width, height), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);

        EbDesktopMenuFonts::pushCaps();
        ImGui::PushStyleColor(ImGuiCol_Text, EbDesktopMenuTheme::muted());
        ImGui::TextUnformatted("PACKS");
        ImGui::PopStyleColor();
        EbDesktopMenuFonts::pop();
        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        float const listH = (std::max)(40.0f, ImGui::GetContentRegionAvail().y);
        ImVec2 const listMin = ImGui::GetCursorScreenPos();
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0.0f);
        ImGui::BeginChild("##eb-cloud-rows", ImVec2(0.0f, listH), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);

        std::vector<EbCloudConfigIndex::PackEntry const*> rows;
        collectRows(rows);
        if (rows.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, EbDesktopMenuTheme::muted());
            ImGui::TextUnformatted(EbDesktopCloudOverlayState::searchQuery().empty()
                                       ? "No packs yet"
                                       : "No matches");
            ImGui::PopStyleColor();
        } else {
            // Single column of blocky rows (not a multi-col grid).
            float const gap = EbDesktopCloudOverlayState::kBlockGap;
            float const cellW = ImGui::GetContentRegionAvail().x;
            float const cellH = EbDesktopCloudOverlayState::kBlockH;
            for (size_t index = 0; index < rows.size(); ++index) {
                if (!rows[index]) {
                    continue;
                }
                drawPackCard(*rows[index], static_cast<int>(index), cellW, cellH);
                ImGui::Dummy(ImVec2(0.0f, gap));
            }
        }

        // Smooth wheel scroll (Cocos → addWheelDelta); dismiss sort popup on move.
        float& wheel = EbDesktopCloudOverlayState::wheelDelta();
        ImVec2 const mouse = ImGui::GetIO().MousePos;
        bool const overList = mouse.x >= listMin.x && mouse.x < listMin.x + width &&
                              mouse.y >= listMin.y && mouse.y < listMin.y + listH;
        float const maxY = ImGui::GetScrollMaxY();
        float& scrollTarget = EbDesktopCloudOverlayState::scrollTarget();
        float& scrollVisual = EbDesktopCloudOverlayState::scrollVisual();
        bool scrolled = false;
        if (wheel != 0.0f && overList) {
            float const step = ImGui::GetTextLineHeightWithSpacing() * 1.35f;
            float next = scrollTarget + wheel * step;
            if (next < 0.0f) {
                next = 0.0f;
            }
            if (next > maxY) {
                next = maxY;
            }
            if (std::fabs(next - scrollTarget) > 0.01f) {
                scrolled = true;
            }
            scrollTarget = next;
            wheel = 0.0f;
        }
        if (scrollTarget > maxY) {
            scrollTarget = maxY;
        }
        if (scrollTarget < 0.0f) {
            scrollTarget = 0.0f;
        }
        float const before = scrollVisual;
        scrollVisual = EbDesktopMenuMotion::approach(scrollVisual, scrollTarget, 0.06f,
                                                     ImGui::GetIO().DeltaTime);
        if (std::fabs(scrollVisual - before) > 0.5f) {
            scrolled = true;
        }
        ImGui::SetScrollY(scrollVisual);
        if (scrolled) {
            EbDesktopMenuShell::requestDismissFloatingPopups();
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    static void drawDetailPane(float width, float height) {
        ImVec2 const min = ImGui::GetCursorScreenPos();
        ImVec2 const max(min.x + width, min.y + height);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float const rounding = EbDesktopMenuTheme::frameRounding();

        ImVec4 border = EbDesktopMenuTheme::outline();
        border.w = 0.45f;
        drawList->AddRectFilled(min, max, EbDesktopMenuTheme::toU32(EbDesktopMenuTheme::panel()),
                                rounding);
        drawList->AddRect(min, max, EbDesktopMenuTheme::toU32(border), rounding, 0,
                          EbDesktopMenuTheme::outlineThickness());

        float const footerH = 40.0f;
        float const bodyH = (std::max)(48.0f, height - footerH - 10.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0.0f);
        ImGui::BeginChild("##eb-cloud-detail", ImVec2(width, bodyH),
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);

        auto const* entry = EbCloudConfigIndex::findById(EbCloudConfigService::get().catalog(),
                                                         EbDesktopCloudOverlayState::selectedId());
        if (!entry) {
            ImGui::PushStyleColor(ImGuiCol_Text, EbDesktopMenuTheme::muted());
            ImGui::TextUnformatted("Select a pack from the list.");
            ImGui::PopStyleColor();
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
            ImGui::SetCursorScreenPos(ImVec2(min.x + 16.0f, max.y - footerH));
            ImGui::Dummy(ImVec2(width - 32.0f, footerH - 8.0f));
            return;
        }

        // Saved mark top-right of the detail card (no background / no status sentence).
        if (isCached(entry->id)) {
            drawSavedIcon(drawList, ImVec2(max.x - 14.0f, min.y + 12.0f));
        }

        // Wrap at content edge (0) — absolute screen wrap was clipping to one line.
        ImGui::PushTextWrapPos(0.0f);

        EbDesktopMenuFonts::pushHeader();
        ImGui::PushStyleColor(ImGuiCol_Text, EbDesktopMenuTheme::text());
        ImGui::TextWrapped("%s", entry->name.c_str());
        ImGui::PopStyleColor();
        EbDesktopMenuFonts::pop();

        ImGui::PushStyleColor(ImGuiCol_Text, EbDesktopMenuTheme::muted());
        if (!entry->author.empty()) {
            ImGui::TextWrapped("%s", entry->author.c_str());
        }
        ImGui::TextWrapped("%s", entry->id.c_str());
        ImGui::PopStyleColor();

        auto const stats = EbCloudConfigStats::get().statsFor(entry->id);
        if (stats.downloads > 0 || stats.ratingCount > 0) {
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImVec2 const rowMin = ImGui::GetCursorScreenPos();
            ImVec4 statsColor = EbDesktopMenuTheme::muted();
            statsColor.w = 0.92f;
            ImU32 const statsCol = EbDesktopMenuTheme::toU32(statsColor);
            float const statsSize = ImGui::GetFontSize() * 0.92f;
            float cursorX = rowMin.x;
            float rowW = 0.0f;
            if (stats.downloads > 0) {
                float const iconSize = 12.0f;
                drawDownloadIcon(drawList, ImVec2(cursorX, rowMin.y + 1.0f), iconSize, statsCol);
                cursorX += iconSize + 5.0f;
                std::string const dlText = formatDlBadge(stats.downloads);
                drawList->AddText(ImGui::GetFont(), statsSize, ImVec2(cursorX, rowMin.y), statsCol,
                                  dlText.c_str());
                cursorX +=
                    ImGui::GetFont()->CalcTextSizeA(statsSize, FLT_MAX, 0.0f, dlText.c_str()).x +
                    12.0f;
            }
            if (stats.ratingCount > 0) {
                std::string const rateText =
                    fmt::format("{:.1f}★", EbCloudConfigStats::averageRating(stats));
                drawList->AddText(ImGui::GetFont(), statsSize, ImVec2(cursorX, rowMin.y), statsCol,
                                  rateText.c_str());
                cursorX +=
                    ImGui::GetFont()->CalcTextSizeA(statsSize, FLT_MAX, 0.0f, rateText.c_str()).x;
            }
            rowW = cursorX - rowMin.x;
            ImGui::Dummy(ImVec2(rowW, statsSize + 2.0f));
        }

        if (!entry->description.empty()) {
            ImGui::Dummy(ImVec2(0.0f, 10.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, EbDesktopMenuTheme::text());
            ImGui::TextWrapped("%s", entry->description.c_str());
            ImGui::PopStyleColor();
        }

        if (isActive(entry->id) || hasUpdate(*entry)) {
            ImGui::Dummy(ImVec2(0.0f, 12.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, EbDesktopMenuTheme::muted());
            if (isActive(entry->id)) {
                ImGui::TextUnformatted("Currently installed");
            } else if (hasUpdate(*entry)) {
                ImGui::TextUnformatted("Update available");
            }
            ImGui::PopStyleColor();
        }

        bool const verOk =
            EbCloudConfigIndex::meetsMinModVersion(*entry,
                                                   EbDesktopCloudOverlayInstall::liveModVersion());
        if (!verOk) {
            ImGui::Dummy(ImVec2(0.0f, 10.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, EbDesktopMenuTheme::danger());
            ImGui::TextWrapped("Needs newer EarlyBuffer (%s).",
                               entry->minModVersion.empty() ? "?" : entry->minModVersion.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::PopTextWrapPos();

        // Detail uses leftover wheel when the pointer is over this pane.
        float& wheel = EbDesktopCloudOverlayState::wheelDelta();
        bool const overDetail = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        float const maxY = ImGui::GetScrollMaxY();
        float& scrollTarget = EbDesktopCloudOverlayState::detailScrollTarget();
        float& scrollVisual = EbDesktopCloudOverlayState::detailScrollVisual();
        if (wheel != 0.0f && overDetail) {
            float const step = ImGui::GetTextLineHeightWithSpacing();
            scrollTarget += wheel * step;
            wheel = 0.0f;
        }
        if (scrollTarget < 0.0f) {
            scrollTarget = 0.0f;
        }
        if (scrollTarget > maxY) {
            scrollTarget = maxY;
        }
        scrollVisual = EbDesktopMenuMotion::approach(scrollVisual, scrollTarget, 0.06f,
                                                     ImGui::GetIO().DeltaTime);
        ImGui::SetScrollY(scrollVisual);

        ImGui::EndChild();
        ImGui::PopStyleVar(2);

        ImGui::SetCursorScreenPos(ImVec2(min.x + 16.0f, max.y - footerH));
        ImGui::PushItemWidth(width - 32.0f);
        bool const canInstall = verOk && !EbDesktopCloudOverlayState::busyUi();
        char const* installLabel = hasUpdate(*entry) ? "Update" : "Install";
        if (EbButton::draw("##eb-cloud-install", installLabel, canInstall, nullptr, true)) {
            EbDesktopCloudOverlayInstall::requestInstall();
        }
        ImGui::PopItemWidth();
    }

private:
    EbDesktopCloudOverlayList() = delete;
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

    static void drawHeader(float contentW) {
        float const actionsW = 188.0f;
        float const titleW = (std::max)(120.0f, contentW - actionsW - 12.0f);

        ImGui::BeginGroup();
        EbDesktopMenuFonts::pushHeader();
        ImGui::PushStyleColor(ImGuiCol_Text, EbDesktopMenuTheme::text());
        ImGui::TextUnformatted("Cloud Configs");
        ImGui::PopStyleColor();
        EbDesktopMenuFonts::pop();

        ImGui::PushStyleColor(ImGuiCol_Text, EbDesktopCloudOverlayState::statusError()
                                                 ? EbDesktopMenuTheme::danger()
                                                 : EbDesktopMenuTheme::muted());
        ImGui::PushTextWrapPos(ImGui::GetCursorScreenPos().x + titleW);
        ImGui::TextUnformatted(EbDesktopCloudOverlayState::statusText().c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::EndGroup();

        ImGui::SameLine(0.0f, 12.0f);
        float const rightX = ImGui::GetWindowContentRegionMax().x - actionsW;
        if (ImGui::GetCursorPosX() < rightX) {
            ImGui::SetCursorPosX(rightX);
        }
        bool const canRefresh = !EbDesktopCloudOverlayState::busyUi();
        EbButton::beginRow(2, 8.0f);
        if (EbButton::draw("##eb-cloud-refresh", "Refresh", canRefresh, nullptr, true)) {
            EbDesktopCloudOverlayCatalog::startRefresh(true);
        }
        if (EbButton::draw("##eb-cloud-close", "Close")) {
            requestDismiss();
        }
        EbButton::endRow();
    }

    static void drawBody(float contentW) {
        drawHeader(contentW);
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        EbDesktopCloudOverlayList::drawToolbar(contentW);
        ImGui::Dummy(ImVec2(0.0f, 8.0f));

        float const gap = 14.0f;
        // List is a single column of full-width block rows — give it real width.
        float const listW = contentW * 0.48f;
        float const detailW = contentW - listW - gap;
        float const paneH = (std::max)(160.0f, ImGui::GetContentRegionAvail().y);

        EbDesktopCloudOverlayList::drawListPane(listW, paneH);
        ImGui::SameLine(0.0f, gap);
        EbDesktopCloudOverlayList::drawDetailPane(detailW, paneH);
    }

    static void drawShellChrome(ImVec2 const& winPos, float cardW, float cardH) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float const rounding = EbDesktopMenuTheme::windowRounding();
        ImVec2 const winMax(winPos.x + cardW, winPos.y + cardH);

        // Soft drop shadow under the sheet (matches shell presence).
        for (int index = 0; index < 3; ++index) {
            float const grow = 6.0f + static_cast<float>(index) * 5.0f;
            float const alpha = 0.16f - static_cast<float>(index) * 0.045f;
            drawList->AddRectFilled(ImVec2(winPos.x - grow * 0.2f, winPos.y + grow * 0.35f),
                                    ImVec2(winMax.x + grow * 0.2f, winMax.y + grow),
                                    IM_COL32(0, 0, 0, static_cast<int>(alpha * 255.0f *
                                                                      EbDesktopMenuTheme::styleAlpha() +
                                                                      0.5f)),
                                    rounding + grow * 0.25f);
        }

        drawList->AddRectFilled(winPos, winMax, EbDesktopMenuTheme::toU32(EbDesktopMenuTheme::bg()),
                                rounding);

        float const railW = EbDesktopCloudOverlayState::kRailW;
        ImVec2 const railMin = winPos;
        ImVec2 const railMax(winPos.x + railW, winPos.y + cardH);
        // Always paint the wave — GPU AddImage already multiplies Style.Alpha (no solid pop).
        EbDesktopMenuSidebarSmoke::get().paint(drawList, railMin, railMax, rounding,
                                               static_cast<float>(ImGui::GetTime()),
                                               ImDrawFlags_RoundCornersLeft);

        // Soft divider between wave rail and content.
        drawList->AddRectFilled(ImVec2(railMax.x - 1.0f, railMin.y + 18.0f),
                                ImVec2(railMax.x, railMax.y - 18.0f),
                                EbDesktopMenuTheme::toU32(ImVec4(1.0f, 1.0f, 1.0f, 0.08f)));
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
        float const railW = EbDesktopCloudOverlayState::kRailW;
        float const pad = EbDesktopCloudOverlayState::kContentPad;

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
        // Shell is hidden while cloud is up — light dim over the game only.
        float const scrimA = 0.45f * visual;
        hostDraw->AddRectFilled(
            displayPos, ImVec2(displayPos.x + display.x, displayPos.y + display.y),
            IM_COL32(0, 0, 0, static_cast<int>(scrimA * 255.0f + 0.5f)));

        ImGui::SetCursorScreenPos(displayPos);
        ImGui::InvisibleButton("##eb-cloud-scrim", display);
        bool const scrimClicked = ImGui::IsItemClicked();
        bool const anyPopup = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);

        if (!modalUp && !anyPopup && EbDesktopCloudOverlayState::wantOpen() && visual > 0.85f &&
            !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            requestDismiss();
        }

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        ImVec2 const center(displayPos.x + display.x * 0.5f,
                            displayPos.y + display.y * 0.5f + slide);
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(cardW, cardH));
        // One-shot focus on open — continuous focus steals clicks from Sort popup.
        if (!modalUp && !anyPopup && !ImGui::GetIO().WantTextInput &&
            EbDesktopCloudOverlayState::needWindowFocus()) {
            ImGui::SetNextWindowFocus();
            EbDesktopCloudOverlayState::needWindowFocus() = false;
        }

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, visual);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 8.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

        ImGuiWindowFlags const cardFlags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoBackground;
        ImGui::Begin("##eb-cloud-card", nullptr, cardFlags);

        ImVec2 const winPos = ImGui::GetWindowPos();
        drawShellChrome(winPos, cardW, cardH);

        ImGui::SetCursorScreenPos(ImVec2(winPos.x + railW + pad, winPos.y + pad));
        ImGui::BeginChild("##eb-cloud-content", ImVec2(cardW - railW - pad * 2.0f, cardH - pad * 2.0f),
                          ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        drawBody(cardW - railW - pad * 2.0f);
        ImGui::EndChild();

        bool const cardHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(5);

        bool const popupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
        if (!modalUp && !popupOpen && scrimClicked && !cardHovered &&
            EbDesktopCloudOverlayState::wantOpen() && visual > 0.85f) {
            requestDismiss();
        }
    }

private:
    EbDesktopCloudOverlayDraw() = delete;
};

void EbDesktopCloudOverlay::addWheelDelta(float deltaY) {
    EbDesktopCloudOverlayState::wheelDelta() += deltaY;
}

void EbDesktopCloudOverlay::open() {
    EbKeybind::cancelCapture();
    EbDesktopCloudOverlayState::shareOpen() = false;
    ++EbDesktopCloudOverlayState::sessionGen();
    EbDesktopCloudOverlayState::busyUi() = false;
    EbDesktopCloudOverlayState::searchQuery().clear();
    EbDesktopCloudOverlayState::wheelDelta() = 0.0f;
    EbDesktopCloudOverlayState::scrollTarget() = 0.0f;
    EbDesktopCloudOverlayState::scrollVisual() = 0.0f;
    EbDesktopCloudOverlayState::detailScrollTarget() = 0.0f;
    EbDesktopCloudOverlayState::detailScrollVisual() = 0.0f;
    EbDesktopCloudOverlayState::needWindowFocus() = true;
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

void EbDesktopCloudOverlay::addWheelDelta(float) {}

void EbDesktopCloudOverlay::draw() {}

#endif
