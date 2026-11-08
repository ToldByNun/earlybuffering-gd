#include "cloud_config_popup.hpp"
#include "../../cloud/catalog/cloud_config_index.hpp"
#include "../../cloud/catalog/cloud_config_service.hpp"
#include "../../cloud/cloud_config_apply.hpp"
#include "../../cloud/cloud_config_stats.hpp"
#include "../../cloud/cloud_config_store.hpp"
#include "../../core/platform/platform_limits.hpp"
#include "../pause/pause_action_bridge.hpp"
#include "browser/cloud_browser_details.hpp"
#include "browser/cloud_browser_layout.hpp"
#include "browser/cloud_browser_list.hpp"
#include "cloud_config_list_cell.hpp"
#include "cloud_config_share_popup.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/Layout.hpp>
#include <cctype>
#include <cocos-ext.h>
#include <string>
#include <vector>

std::string CloudConfigPopupImplementation::toLower(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

bool CloudConfigPopupImplementation::containsInsensitive(std::string const& hay,
                                                         std::string const& needleLower) {
    if (!(needleLower.empty())) {
        return CloudConfigPopupImplementation::toLower(hay).find(needleLower) != std::string::npos;
    }
    return true;
}

std::string CloudConfigPopupImplementation::truncate(std::string text, size_t maxChars) {
    if (text.size() <= maxChars)
        return text;
    if (!(maxChars <= 3)) {
        return text.substr(0, maxChars - 3) + "...";
    }
    return text.substr(0, maxChars);
}

std::string CloudConfigPopupImplementation::formatBytes(size_t count) {
    if (count < 1024)
        return fmt::format("{} B", count);
    if (!(count < 1024 * 1024)) {
        return fmt::format("{:.1f} MB", count / (1024.0 * 1024.0));
    }
    return fmt::format("{:.1f} KB", count / 1024.0);
}

std::string CloudConfigPopupImplementation::formatDownloads(int count) {
    if (count >= 1000000)
        return fmt::format("{:.1f}M", count / 1000000.0);
    if (!(count >= 10000)) {
        return fmt::format("{}", count);
    }
    return fmt::format("{}k", count / 1000);
}

CloudConfigPopup* CloudConfigPopup::create() {
    auto* createdObject = new CloudConfigPopup();
    if (createdObject->init()) {
        createdObject->autorelease();
        return createdObject;
    }
    delete createdObject;
    return nullptr;
}

void CloudConfigPopup::open() {
    if (auto* p = create()) {
        p->show();
    }
}

bool CloudConfigPopup::init() {
    return CloudBrowserLayout::init(*this);
}

void CloudConfigPopup::setShareOpen(bool open) {
    m_shareOpen = open;
    if (m_closeBtn) {
        m_closeBtn->setVisible(!open);
        m_closeBtn->setEnabled(!open);
    }
}

std::string CloudConfigPopup::liveModVersion() const {
    if (auto* mod = geode::prelude::Mod::get()) {
        return mod->getVersion().toVString();
    }
    return "0.0.0";
}

bool CloudConfigPopup::isCached(std::string const& id) const {
    return CloudConfigStore::get().hasPack(id);
}

bool CloudConfigPopup::isActive(std::string const& id) const {
    return !id.empty() && id == m_activeId;
}

bool CloudConfigPopup::hasUpdate(CloudConfigIndex::PackEntry const& e) const {
    if (!this->isCached(e.id))
        return false;
    std::string body;
    std::string err;
    if (!CloudConfigStore::get().readPack(e.id, body, err))
        return false;
    CloudConfigStore::ActiveInfo active{};
    if (CloudConfigStore::get().readActive(active, err) && active.id == e.id &&
        !active.sha256.empty()) {
        return active.sha256 != e.sha256;
    }
    return body.size() != e.size;
}

CloudConfigIndex::PackEntry const* CloudConfigPopup::selectedEntry() const {
    return CloudConfigIndex::findById(CloudConfigService::get().catalog(), m_selectedId);
}

bool CloudConfigPopup::packMatchesSearch(CloudConfigIndex::PackEntry const& e) const {
    return CloudBrowserList::packMatchesSearch(*this, e);
}

void CloudConfigPopup::setStatus(std::string const& text, bool isError) {
    if (!m_statusLabel)
        return;
    // Leave room for the nudged Refresh control.
    size_t const maxChars = PlatformLimits::isConstrainedDevice() ? 26 : 36;
    m_statusLabel->setString(CloudConfigPopupImplementation::truncate(text, maxChars).c_str());
    m_statusLabel->setColor(isError ? geode::prelude::ccColor3B{255, 90, 90}
                                    : geode::prelude::ccColor3B{255, 255, 255});
}

void CloudConfigPopup::ensureLocalCatalog() {
    if (!CloudConfigService::get().catalog().packs.empty())
        return;
    CloudConfigService::RefreshResult local{};
    (void)CloudConfigService::get().loadLocalCatalog(local);
}

CloudConfigListCell::StatsBadge CloudConfigPopup::statsBadgeFor(std::string const& id) const {
    CloudConfigListCell::StatsBadge secondValue{};
    auto const state = CloudConfigStats::get().statsFor(id);
    if (state.downloads <= 0 && state.ratingCount <= 0)
        return secondValue;
    secondValue.show = true;
    secondValue.downloads = state.downloads;
    secondValue.ratingCount = state.ratingCount;
    secondValue.ratingAvg = CloudConfigStats::averageRating(state);
    return secondValue;
}

void CloudConfigPopup::startStatsFetch() {
    uint64_t const generation = ++m_statsGen;
    geode::prelude::Ref<CloudConfigPopup> self = this;
    CloudConfigStats::get().fetchStats([self, generation](bool ok, std::string const&) {
        if (!self || generation != self->m_statsGen)
            return;
        if (!ok)
            return;
        self->rebuildList();
        self->updateDetail();
    });
}

void CloudConfigPopup::loadInitial() {
    this->ensureLocalCatalog();
    auto const& cat = CloudConfigService::get().catalog();
    char const* sourceValue =
        CloudConfigService::get().source() == CloudConfigService::CatalogSource::Seed ? "Seed"
        : CloudConfigService::get().source() == CloudConfigService::CatalogSource::DiskCache
            ? "Cache"
            : "Local";
    this->setStatus(fmt::format("{} · {} packs", sourceValue, cat.packs.size()), cat.packs.empty());
    if (!cat.packs.empty() && m_selectedId.empty()) {
        m_selectedId = cat.packs.front().id;
    }
    this->rebuildList();
    this->updateDetail();

    this->startStatsFetch();
    this->startRefresh(false);
}

void CloudConfigPopup::startRefresh(bool force) {
    if (m_busyUi || CloudConfigService::get().isBusy()) {
        this->setStatus("Busy…", false);
        return;
    }
    m_busyUi = true;
    uint64_t const generation = ++m_opGen;
    this->setStatus(force ? "Refreshing (force)…" : "Refreshing…", false);
    if (m_refreshBtn)
        m_refreshBtn->setEnabled(false);

    std::string const custom = CloudConfigService::readCatalogUrlSetting();
    geode::prelude::Ref<CloudConfigPopup> self = this;
    CloudConfigService::get().refreshCatalog(
        custom,
        [self, generation](CloudConfigService::RefreshResult const& r) {
            if (!self || generation != self->m_opGen)
                return;
            self->onRefreshDone(r);
        },
        force);
}

void CloudConfigPopup::onRefreshDone(CloudConfigService::RefreshResult const& r) {
    m_busyUi = false;
    if (m_refreshBtn)
        m_refreshBtn->setEnabled(true);

    if (r.busy) {
        this->setStatus("Busy…", false);
        return;
    }
    if (!r.ok) {
        this->ensureLocalCatalog();
        auto const count = CloudConfigService::get().catalog().packs.size();
        this->setStatus(fmt::format("Offline · {} packs", count), count == 0);
        if (m_selectedId.empty() && count > 0) {
            m_selectedId = CloudConfigService::get().catalog().packs.front().id;
        }
        this->rebuildList();
        this->updateDetail();
        this->startStatsFetch();
        return;
    }

    char const* source = r.usedFallback ? "Online*" : "Online";
    std::string stamp = r.catalog.updatedAt;
    if (stamp.size() > 16)
        stamp = stamp.substr(0, 16); // 2026-11-04T15:16
    if (!stamp.empty()) {
        this->setStatus(fmt::format("{} · {} packs · {}", source, r.catalog.packs.size(), stamp),
                        false);
    } else {
        this->setStatus(fmt::format("{} · {} packs", source, r.catalog.packs.size()), false);
    }
    if (r.catalog.packs.empty()) {
        this->ensureLocalCatalog();
    }
    if (m_selectedId.empty() && !CloudConfigService::get().catalog().packs.empty()) {
        m_selectedId = CloudConfigService::get().catalog().packs.front().id;
    } else if (!CloudConfigIndex::findById(CloudConfigService::get().catalog(), m_selectedId) &&
               !CloudConfigService::get().catalog().packs.empty()) {
        m_selectedId = CloudConfigService::get().catalog().packs.front().id;
    }
    this->rebuildList();
    this->updateDetail();
    this->startStatsFetch();
}

char const* CloudConfigPopup::sortModeLabel() const {
    return CloudBrowserList::sortModeLabel(*this);
}

void CloudConfigPopup::updateSortLabel() {
    return CloudBrowserList::updateSortLabel(*this);
}

void CloudConfigPopup::onSortCycle(geode::prelude::CCObject* argument0) {
    return CloudBrowserList::onSortCycle(*this, argument0);
}

void CloudConfigPopup::applySort(std::vector<CloudConfigIndex::PackEntry const*>& rows) const {
    return CloudBrowserList::applySort(*this, rows);
}

void CloudConfigPopup::rebuildList() {
    return CloudBrowserList::rebuildList(*this);
}

void CloudConfigPopup::selectPack(std::string const& id) {
    m_selectedId = id;
    this->rebuildList();
    this->updateDetail();
}

void CloudConfigPopup::updateRateUi() {
    return CloudBrowserDetails::updateRateUi(*this);
}

void CloudConfigPopup::updateDetail() {
    return CloudBrowserDetails::updateDetail(*this);
}

void CloudConfigPopup::onRefresh(geode::prelude::CCObject*) {
    this->startRefresh(true);
}

void CloudConfigPopup::onInstall(geode::prelude::CCObject*) {
    auto const* entry = this->selectedEntry();
    if (!entry || m_busyUi)
        return;
    if (!CloudConfigIndex::meetsMinModVersion(*entry, this->liveModVersion())) {
        FLAlertLayer::create("Cloud Configs", "This pack needs a newer EarlyBuffer version.", "OK")
            ->show();
        return;
    }

    std::string const id = entry->id;
    geode::prelude::createQuickPopup("Install config?",
                                     "Replace current EarlyBuffer settings?\n"
                                     "<cy>Keybinds</c> and <cy>Dev Lab</c> keys stay unchanged.",
                                     "Cancel", "Install", [this, id](auto*, bool btn2) {
                                         if (btn2)
                                             this->doInstall(id);
                                     });
}

void CloudConfigPopup::doInstall(std::string const& id) {
    auto const* entryValue = CloudConfigIndex::findById(CloudConfigService::get().catalog(), id);
    if (!entryValue)
        return;

    m_busyUi = true;
    uint64_t const generation = ++m_opGen;
    this->setStatus("Downloading…", false);
    if (m_installBtn)
        m_installBtn->setEnabled(false);

    CloudConfigIndex::PackEntry entry = *entryValue;
    geode::prelude::Ref<CloudConfigPopup> self = this;
    CloudConfigService::get().fetchPack(
        id, this->hasUpdate(entry),
        [self, generation, entry](CloudConfigService::PackFetchResult const& fr) {
            if (!self || generation != self->m_opGen)
                return;
            self->onPackFetched(fr, entry);
        });
}

void CloudConfigPopup::onPackFetched(CloudConfigService::PackFetchResult const& fr,
                                     CloudConfigIndex::PackEntry entry) {
    m_busyUi = false;
    if (m_installBtn)
        m_installBtn->setEnabled(true);

    if (fr.busy) {
        this->setStatus("Busy…", false);
        return;
    }
    if (fr.cancelled) {
        this->setStatus("Cancelled", true);
        return;
    }
    if (!fr.ok) {
        this->setStatus("Download failed", true);
        FLAlertLayer::create("Cloud Configs",
                             fr.error.empty() ? "Could not download pack." : fr.error.c_str(), "OK")
            ->show();
        return;
    }

    auto const applied =
        CloudConfigApply::applyPackBody(entry.id, entry.name, entry.sha256, entry.size, fr.body);
    if (!applied.ok) {
        this->setStatus("Install failed", true);
        FLAlertLayer::create("Cloud Configs", applied.error.c_str(), "OK")->show();
        return;
    }

    m_activeId = entry.id;
    this->rebuildList();
    this->updateDetail();
    this->setStatus(fmt::format("Installed · {}", entry.name), false);

    // Fire-and-forget; never fails install UX. Local/server dedupe = 1 dl / actor.
    this->trackInstallDownload(entry.id);

    PauseActionBridge::afterConfigApplied(
        fmt::format("Installed cloud config \"{}\".\nApplied {} settings ({} skipped).", entry.name,
                    applied.stats.applied, applied.stats.skipped));
}

void CloudConfigPopup::trackInstallDownload(std::string const& packId) {
    geode::prelude::Ref<CloudConfigPopup> self = this;
    CloudConfigStats::get().trackDownload(packId, [self](CloudConfigStats::TrackResult const& r) {
        if (!self)
            return;
        // Refresh badges when counters moved; silent on network errors.
        if (r.ok || r.duplicate || r.skippedLocal) {
            self->rebuildList();
            self->updateDetail();
        }
    });
}

void CloudConfigPopup::onRate(geode::prelude::CCObject* sender) {
    auto const* entry = this->selectedEntry();
    if (!entry || m_busyUi || m_rateBusy)
        return;

    auto* item = geode::prelude::typeinfo_cast<geode::prelude::CCMenuItem*>(sender);
    if (!item)
        return;
    int const stars = item->getTag();
    if (stars < 1 || stars > 5)
        return;

    // Same as current vote — just refresh paint.
    if (CloudConfigStats::get().myStarsFor(entry->id) == stars) {
        this->updateRateUi();
        return;
    }

    m_rateBusy = true;
    uint64_t const generation = ++m_rateGen;
    this->updateRateUi();

    std::string const id = entry->id;
    geode::prelude::Ref<CloudConfigPopup> self = this;
    CloudConfigStats::get().trackRate(
        id, stars, [self, generation, stars](CloudConfigStats::TrackResult const& r) {
            if (!self || generation != self->m_rateGen)
                return;
            self->m_rateBusy = false;

            if (r.ok || r.duplicate || r.skippedLocal) {
                self->rebuildList();
                self->updateDetail();
                if (r.updated) {
                    self->setStatus(fmt::format("Updated {}/5", stars), false);
                } else if (r.ok && !r.duplicate && !r.skippedLocal) {
                    self->setStatus(fmt::format("Rated {}/5", stars), false);
                }
                return;
            }

            self->updateRateUi();
            FLAlertLayer::create("Cloud Configs",
                                 r.error.empty() ? "Could not submit rating." : r.error.c_str(),
                                 "OK")
                ->show();
        });
}

void CloudConfigPopup::onShare(geode::prelude::CCObject*) {
    if (m_busyUi || m_shareOpen)
        return;
    this->setShareOpen(true);
    geode::prelude::Ref<CloudConfigPopup> self = this;
    CloudConfigSharePopup::open([self]() {
        if (self)
            self->setShareOpen(false);
    });
}
