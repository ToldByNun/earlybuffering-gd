#include "cloud_config_popup.hpp"
#include "../../cloud/cloud_config_apply.hpp"
#include "../../cloud/cloud_config_stats.hpp"
#include "../../cloud/cloud_config_store.hpp"
#include "../../cloud/sharing/cloud_config_share.hpp"
#include "../../core/platform/platform_limits.hpp"
#include "../pause/pause_action_bridge.hpp"
#include "cloud_config_list_cell.hpp"
#include "cloud_config_share_popup.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/Layout.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cocos-ext.h>

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
    bool const tight = PlatformLimits::isConstrainedDevice();
    float const width = tight ? 310.f : 380.f;
    float const height = tight ? 240.f : 268.f;

    if (!geode::prelude::Popup::init(width, height, "GJ_square01.png")) {
        return false;
    }

    this->setTitle("Cloud Configs", "goldFont.fnt", 0.64f, 12.f);

    {
        CloudConfigStore::ActiveInfo active{};
        std::string err;
        if (CloudConfigStore::get().readActive(active, err)) {
            m_activeId = active.id;
        }
    }

    float const pad = 14.f;
    float const gap = 8.f;

    // Status left; Refresh nudged inward so it clears title/close chrome.
    m_statusLabel = geode::prelude::CCLabelBMFont::create("Loading…", "goldFont.fnt");
    m_statusLabel->setScale(tight ? 0.26f : 0.28f);
    m_statusLabel->setAnchorPoint({0.f, 0.5f});
    m_statusLabel->setID("cloud-status"_spr);
    m_mainLayer->addChildAtPosition(m_statusLabel, geode::Anchor::TopLeft, ccp(pad, -34.f));

    float const refreshNudge = tight ? 28.f : 36.f; // plan: ~28–40px left of TopRight
    auto* topMenu = geode::prelude::CCMenu::create();
    topMenu->setContentSize({70.f, 24.f});
    topMenu->setID("cloud-top-menu"_spr);
    m_mainLayer->addChildAtPosition(topMenu, geode::Anchor::TopRight,
                                    ccp(-(pad + refreshNudge), -34.f));

    auto* refreshSpr = ButtonSprite::create("Refresh", "bigFont.fnt", "GJ_button_05.png", 0.8f);
    refreshSpr->setScale(tight ? 0.30f : 0.34f);
    m_refreshBtn =
        CCMenuItemSpriteExtra::create(refreshSpr, this, menu_selector(CloudConfigPopup::onRefresh));
    m_refreshBtn->setID("cloud-refresh-btn"_spr);
    topMenu->addChildAtPosition(m_refreshBtn, geode::Anchor::Center);

    m_listWidth = tight ? (width - pad * 2.f) : ((width - pad * 2.f - gap) * 0.50f);
    m_detailWidth = tight ? (width - pad * 2.f) : ((width - pad * 2.f - gap) * 0.50f);
    // Extra row for sort cycle under search.
    m_listHeight = tight ? 68.f : (height - 136.f);
    m_detailHeight = tight ? 56.f : (height - 122.f);

    float const sortRowH = 16.f;
    float const searchY = -54.f;
    float const sortY = searchY - 16.f;

    // No setScale on TextInput — scale shrinks the visual while layout uses full width.
    m_search = geode::prelude::TextInput::create(tight ? (m_listWidth - 8.f) : m_listWidth,
                                                 "Search packs…", "bigFont.fnt");
    m_search->setID("cloud-search"_spr);
    m_search->setMaxCharCount(64);
    m_search->setCommonFilter(geode::prelude::CommonFilter::Any);
    m_search->setTextAlign(geode::prelude::TextInputAlign::Left);
    m_search->setCallback([this](std::string const& text) {
        m_searchQuery = text;
        this->rebuildList();
    });
    if (tight) {
        m_mainLayer->addChildAtPosition(m_search, geode::Anchor::Top, ccp(0.f, searchY));
    } else {
        m_mainLayer->addChildAtPosition(m_search, geode::Anchor::TopLeft,
                                        ccp(pad + m_listWidth * 0.5f, searchY));
    }

    auto* sortMenu = geode::prelude::CCMenu::create();
    sortMenu->setContentSize({m_listWidth, sortRowH});
    sortMenu->ignoreAnchorPointForPosition(false);
    sortMenu->setAnchorPoint({0.f, 0.5f});
    sortMenu->setID("cloud-sort-menu"_spr);
    m_sortLabel = geode::prelude::CCLabelBMFont::create("Sort: Default", "goldFont.fnt");
    m_sortLabel->setScale(tight ? 0.24f : 0.28f);
    m_sortLabel->setAnchorPoint({0.f, 0.5f});
    m_sortLabel->setOpacity(220);
    m_sortLabel->setID("cloud-sort-label"_spr);
    m_sortBtn = CCMenuItemSpriteExtra::create(m_sortLabel, this,
                                              menu_selector(CloudConfigPopup::onSortCycle));
    m_sortBtn->setAnchorPoint({0.f, 0.5f});
    m_sortBtn->setID("cloud-sort-btn"_spr);
    sortMenu->addChildAtPosition(m_sortBtn, geode::Anchor::Left, ccp(0.f, 0.f));
    if (tight) {
        m_mainLayer->addChildAtPosition(sortMenu, geode::Anchor::Top,
                                        ccp(-m_listWidth * 0.5f, sortY));
    } else {
        m_mainLayer->addChildAtPosition(sortMenu, geode::Anchor::TopLeft, ccp(pad, sortY));
    }
    this->updateSortLabel();

    m_listScroll = geode::prelude::ScrollLayer::create(
        geode::prelude::CCSize{m_listWidth, m_listHeight}, true, true);
    m_listScroll->setID("cloud-list-scroll"_spr);
    m_listScroll->ignoreAnchorPointForPosition(false);
    m_listScroll->setAnchorPoint({0.f, 0.5f});

    m_listBorders = geode::prelude::ListBorders::create();
    m_listBorders->setContentSize({m_listWidth, m_listHeight});
    m_listBorders->setID("cloud-list-borders"_spr);
    m_listBorders->ignoreAnchorPointForPosition(false);
    m_listBorders->setAnchorPoint({0.f, 0.5f});

    if (tight) {
        m_mainLayer->addChildAtPosition(m_listScroll, geode::Anchor::Center,
                                        ccp(-m_listWidth * 0.5f, 18.f));
        m_mainLayer->addChildAtPosition(m_listBorders, geode::Anchor::Center,
                                        ccp(-m_listWidth * 0.5f, 18.f));
    } else {
        float const listY = -2.f;
        m_mainLayer->addChildAtPosition(m_listScroll, geode::Anchor::Left, ccp(pad, listY));
        m_mainLayer->addChildAtPosition(m_listBorders, geode::Anchor::Left, ccp(pad, listY));
    }

    m_detailRoot = geode::prelude::CCNode::create();
    m_detailRoot->setContentSize({m_detailWidth, m_detailHeight});
    m_detailRoot->setID("cloud-detail-root"_spr);
    m_detailRoot->ignoreAnchorPointForPosition(false);
    m_detailRoot->setAnchorPoint({1.f, 0.5f});

    m_detailBg = geode::prelude::CCLayerColor::create({0, 0, 0, 80}, m_detailWidth, m_detailHeight);
    m_detailBg->ignoreAnchorPointForPosition(false);
    m_detailBg->setAnchorPoint({0.5f, 0.5f});
    m_detailBg->setID("cloud-detail-bg"_spr);
    m_detailRoot->addChildAtPosition(m_detailBg, geode::Anchor::Center);

    m_detailTitle = geode::prelude::CCLabelBMFont::create("Select a pack", "bigFont.fnt");
    m_detailTitle->setScale(0.34f);
    m_detailTitle->setAnchorPoint({0.f, 1.f});
    m_detailTitle->setID("cloud-detail-title"_spr);
    m_detailRoot->addChildAtPosition(m_detailTitle, geode::Anchor::TopLeft, ccp(6.f, -6.f));

    m_detailMeta = geode::prelude::CCLabelBMFont::create(" ", "goldFont.fnt");
    m_detailMeta->setScale(0.26f);
    m_detailMeta->setAnchorPoint({0.f, 1.f});
    m_detailMeta->setOpacity(210);
    m_detailMeta->setID("cloud-detail-meta"_spr);
    m_detailRoot->addChildAtPosition(m_detailMeta, geode::Anchor::TopLeft, ccp(6.f, -22.f));

    m_detailBody = geode::prelude::SimpleTextArea::create(
        "Pick a pack, then Install.\nShare… sets name & description.", "chatFont.fnt", 0.38f,
        m_detailWidth - 12.f);
    m_detailBody->setWrappingMode(geode::prelude::WORD_WRAP);
    m_detailBody->setMaxLines(tight ? 2 : 3);
    m_detailBody->setAlignment(geode::prelude::kCCTextAlignmentLeft);
    m_detailBody->setAnchorPoint({0.f, 1.f});
    m_detailBody->setID("cloud-detail-body"_spr);
    m_detailRoot->addChildAtPosition(m_detailBody, geode::Anchor::TopLeft, ccp(6.f, -36.f));

    // Gray star sprite (resources/ui/eb_star.png) — tinted gold/gray in updateRateUi.
    m_rateHint = geode::prelude::CCLabelBMFont::create("Rate", "goldFont.fnt");
    m_rateHint->setScale(0.26f);
    m_rateHint->setAnchorPoint({0.f, 0.5f});
    m_rateHint->setOpacity(210);
    m_rateHint->setID("cloud-rate-hint"_spr);
    m_detailRoot->addChildAtPosition(m_rateHint, geode::Anchor::BottomLeft, ccp(6.f, 34.f));

    m_rateMenu = geode::prelude::CCMenu::create();
    m_rateMenu->setContentSize({m_detailWidth - 12.f, 22.f});
    m_rateMenu->ignoreAnchorPointForPosition(false);
    m_rateMenu->setAnchorPoint({0.f, 0.5f});
    m_rateMenu->setID("cloud-rate-menu"_spr);
    m_rateMenu->setLayout(geode::prelude::RowLayout::create()
                              ->setGap(2.f)
                              ->setAxisAlignment(geode::prelude::AxisAlignment::Start)
                              ->setCrossAxisAlignment(geode::prelude::AxisAlignment::Center)
                              ->setAutoScale(false));
    for (int index = 0; index < 5; ++index) {
        auto* sprite = geode::prelude::CCSprite::create("eb_star.png"_spr);
        if (!sprite) {
            // Fallback if resource missing from package.
            sprite = geode::prelude::CCSprite::createWithSpriteFrameName("GJ_starsIcon_001.png");
        }
        if (!sprite) {
            auto* lab = geode::prelude::CCLabelBMFont::create("*", "goldFont.fnt");
            lab->setScale(0.45f);
            m_rateStarSpr[index] = nullptr;
            m_rateBtns[index] =
                CCMenuItemSpriteExtra::create(lab, this, menu_selector(CloudConfigPopup::onRate));
        } else {
            sprite->setScale(tight ? 0.34f : 0.42f);
            sprite->setColor({170, 170, 170});
            m_rateStarSpr[index] = sprite;
            m_rateBtns[index] = CCMenuItemSpriteExtra::create(
                sprite, this, menu_selector(CloudConfigPopup::onRate));
        }
        m_rateBtns[index]->setTag(index + 1);
        m_rateBtns[index]->setID(fmt::format("cloud-rate-{}", index + 1));
        m_rateMenu->addChild(m_rateBtns[index]);
    }
    m_rateMenu->updateLayout();
    m_detailRoot->addChildAtPosition(m_rateMenu, geode::Anchor::BottomLeft,
                                     ccp(tight ? 32.f : 38.f, tight ? 30.f : 34.f));

    m_detailFoot = geode::prelude::CCLabelBMFont::create(" ", "chatFont.fnt");
    m_detailFoot->setScale(tight ? 0.30f : 0.34f);
    m_detailFoot->setAnchorPoint({0.f, 0.f});
    m_detailFoot->setOpacity(200);
    m_detailFoot->setID("cloud-detail-foot"_spr);
    m_detailRoot->addChildAtPosition(m_detailFoot, geode::Anchor::BottomLeft, ccp(6.f, 3.f));

    if (tight) {
        m_mainLayer->addChildAtPosition(m_detailRoot, geode::Anchor::Bottom,
                                        ccp(m_detailWidth * 0.5f, 48.f));
        m_detailRoot->setAnchorPoint({0.5f, 0.f});
    } else {
        m_mainLayer->addChildAtPosition(m_detailRoot, geode::Anchor::Right, ccp(-pad, -2.f));
    }

    auto* bottom = geode::prelude::CCMenu::create();
    bottom->setContentSize({width - pad * 2.f, 28.f});
    bottom->setID("cloud-bottom-menu"_spr);
    m_mainLayer->addChildAtPosition(bottom, geode::Anchor::Bottom, ccp(0.f, 18.f));
    bottom->setLayout(geode::prelude::RowLayout::create()
                          ->setGap(10.f)
                          ->setAxisAlignment(geode::prelude::AxisAlignment::Center)
                          ->setCrossAxisAlignment(geode::prelude::AxisAlignment::Center));

    auto* installSpr = ButtonSprite::create("Install", "bigFont.fnt", "GJ_button_01.png", 0.8f);
    installSpr->setScale(0.44f);
    m_installBtn =
        CCMenuItemSpriteExtra::create(installSpr, this, menu_selector(CloudConfigPopup::onInstall));
    m_installBtn->setID("cloud-install-btn"_spr);
    bottom->addChild(m_installBtn);

    auto* shareSpr = ButtonSprite::create("Share…", "bigFont.fnt", "GJ_button_05.png", 0.8f);
    shareSpr->setScale(0.44f);
    auto* shareBtn =
        CCMenuItemSpriteExtra::create(shareSpr, this, menu_selector(CloudConfigPopup::onShare));
    shareBtn->setID("cloud-share-btn"_spr);
    bottom->addChild(shareBtn);
    bottom->updateLayout();

    this->loadInitial();
    return true;
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
    auto const queryOffset = CloudConfigPopupImplementation::toLower(m_searchQuery);
    if (queryOffset.empty())
        return true;
    if (CloudConfigPopupImplementation::containsInsensitive(e.name, queryOffset))
        return true;
    if (CloudConfigPopupImplementation::containsInsensitive(e.author, queryOffset))
        return true;
    if (CloudConfigPopupImplementation::containsInsensitive(e.id, queryOffset))
        return true;
    if (CloudConfigPopupImplementation::containsInsensitive(e.description, queryOffset))
        return true;
    for (auto const& t : e.tags) {
        if (CloudConfigPopupImplementation::containsInsensitive(t, queryOffset))
            return true;
    }
    return false;
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
    bool const tight = PlatformLimits::isConstrainedDevice();
    switch (m_sortMode) {
    case SortMode::Downloads:
        return tight ? "DLs" : "Downloads";
    case SortMode::Rating:
        return tight ? "Rate" : "Rating";
    case SortMode::Popular:
        return tight ? "Pop" : "Popular";
    case SortMode::Name:
        return "Name";
    case SortMode::Default:
    default:
        return tight ? "Def" : "Default";
    }
}

void CloudConfigPopup::updateSortLabel() {
    if (!m_sortLabel)
        return;
    m_sortLabel->setString(fmt::format("Sort: {}", this->sortModeLabel()).c_str());
}

void CloudConfigPopup::onSortCycle(geode::prelude::CCObject*) {
    int next = static_cast<int>(m_sortMode) + 1;
    if (next >= static_cast<int>(SortMode::Count))
        next = 0;
    m_sortMode = static_cast<SortMode>(next);
    this->updateSortLabel();
    this->rebuildList();
}

void CloudConfigPopup::applySort(std::vector<CloudConfigIndex::PackEntry const*>& rows) const {
    if (rows.size() < 2)
        return;
    if (m_sortMode == SortMode::Default)
        return; // catalog order

    auto statsOf = [](CloudConfigIndex::PackEntry const* valuePointer) {
        return CloudConfigStats::get().statsFor(valuePointer->id);
    };

    std::stable_sort(rows.begin(), rows.end(),
                     [&](CloudConfigIndex::PackEntry const* firstValue,
                         CloudConfigIndex::PackEntry const* secondValue) {
                         if (!firstValue || !secondValue)
                             return firstValue != nullptr;
                         auto const sa = statsOf(firstValue);
                         auto const sb = statsOf(secondValue);
                         switch (m_sortMode) {
                         case SortMode::Downloads:
                             if (sa.downloads != sb.downloads)
                                 return sa.downloads > sb.downloads;
                             break;
                         case SortMode::Rating: {
                             float const ra = CloudConfigStats::averageRating(sa);
                             float const rb = CloudConfigStats::averageRating(sb);
                             if (ra != rb)
                                 return ra > rb;
                             if (sa.ratingCount != sb.ratingCount)
                                 return sa.ratingCount > sb.ratingCount;
                             break;
                         }
                         case SortMode::Popular: {
                             float const pa = CloudConfigStats::popularityScore(sa);
                             float const pb = CloudConfigStats::popularityScore(sb);
                             if (pa != pb)
                                 return pa > pb;
                             break;
                         }
                         case SortMode::Name: {
                             auto const na =
                                 CloudConfigPopupImplementation::toLower(firstValue->name);
                             auto const nb =
                                 CloudConfigPopupImplementation::toLower(secondValue->name);
                             if (na != nb)
                                 return na < nb;
                             break;
                         }
                         case SortMode::Default:
                         default:
                             break;
                         }
                         return firstValue->id < secondValue->id;
                     });
}

void CloudConfigPopup::rebuildList() {
    if (!m_listScroll || !m_listScroll->m_contentLayer)
        return;
    auto* content = m_listScroll->m_contentLayer;
    content->removeAllChildren();

    float const width = m_listScroll->getContentWidth();
    auto const& catalog = CloudConfigService::get().catalog();

    std::vector<CloudConfigIndex::PackEntry const*> rows;
    rows.reserve(catalog.packs.size());
    for (auto const& p : catalog.packs) {
        if (this->packMatchesSearch(p))
            rows.push_back(&p);
    }
    this->applySort(rows);

    content->setLayout(geode::prelude::ColumnLayout::create()
                           ->setGap(2.f)
                           ->setAxisReverse(true)
                           ->setAxisAlignment(geode::prelude::AxisAlignment::End)
                           ->setAutoGrowAxis(m_listScroll->getContentHeight()));

    geode::prelude::Ref<CloudConfigPopup> self = this;
    int index = 0;
    for (auto const* text : rows) {
        auto* cell = CloudConfigListCell::create(
            *text, width, index++, text->id == m_selectedId, this->isActive(text->id),
            this->isCached(text->id), this->hasUpdate(*text), this->statsBadgeFor(text->id),
            [self](std::string const& id) {
                if (self)
                    self->selectPack(id);
            });
        if (cell)
            content->addChild(cell);
    }

    if (rows.empty()) {
        auto* emptyNode = geode::prelude::CCNode::create();
        emptyNode->setContentSize({width, m_listScroll->getContentHeight()});
        auto* empty = geode::prelude::CCLabelBMFont::create(
            m_searchQuery.empty() ? "No packs yet" : "No matches", "bigFont.fnt");
        empty->setScale(0.32f);
        empty->setOpacity(160);
        emptyNode->addChildAtPosition(empty, geode::Anchor::Center);
        content->addChild(emptyNode);
    }

    content->updateLayout();
    m_listScroll->scrollToTop();
}

void CloudConfigPopup::selectPack(std::string const& id) {
    m_selectedId = id;
    this->rebuildList();
    this->updateDetail();
}

void CloudConfigPopup::updateRateUi() {
    auto const* entry = this->selectedEntry();
    bool const hasPack = entry != nullptr;
    bool const enable = hasPack && !m_busyUi && !m_rateBusy;
    int const mine = hasPack ? CloudConfigStats::get().myStarsFor(entry->id) : 0;

    // Paint: own vote if set, else community average (display only).
    int filled = 0;
    if (mine >= 1 && mine <= 5) {
        filled = mine;
    } else if (hasPack) {
        auto const state = CloudConfigStats::get().statsFor(entry->id);
        if (state.ratingCount > 0) {
            filled = static_cast<int>(
                std::lround(static_cast<double>(CloudConfigStats::averageRating(state))));
            if (filled < 1)
                filled = 1;
            if (filled > 5)
                filled = 5;
        }
    }

    if (m_rateHint) {
        if (!hasPack) {
            m_rateHint->setString(" ");
            m_rateHint->setOpacity(0);
        } else if (m_rateBusy) {
            m_rateHint->setString("…");
            m_rateHint->setOpacity(210);
        } else if (mine > 0) {
            m_rateHint->setString("Yours");
            m_rateHint->setOpacity(210);
        } else {
            m_rateHint->setString("Rate");
            m_rateHint->setOpacity(210);
        }
    }

    geode::prelude::ccColor3B const colOn = {255, 210, 70};
    geode::prelude::ccColor3B const colOff = {150, 150, 150};

    for (int index = 0; index < 5; ++index) {
        auto* button = m_rateBtns[index];
        if (!button)
            continue;
        button->setVisible(hasPack);
        button->setEnabled(enable);

        bool const on = filled > 0 && (index < filled);
        geode::prelude::ccColor3B const col = on ? colOn : colOff;
        GLubyte const op = !hasPack ? 0 : (enable ? 255 : 140);

        if (auto* spr = m_rateStarSpr[index]) {
            spr->setColor(col);
            spr->setOpacity(op);
        } else {
            button->setColor(col);
            button->setOpacity(op);
        }
    }
    if (m_rateMenu)
        m_rateMenu->setVisible(hasPack);
}

void CloudConfigPopup::updateDetail() {
    auto const* entry = this->selectedEntry();
    if (!entry) {
        if (m_detailTitle)
            m_detailTitle->setString("Select a pack");
        if (m_detailMeta)
            m_detailMeta->setString(" ");
        if (m_detailBody) {
            m_detailBody->setText("Pick a pack from the list.\nShare… sets name & description.");
        }
        if (m_detailFoot)
            m_detailFoot->setString(" ");
        if (m_installBtn)
            m_installBtn->setEnabled(false);
        this->updateRateUi();
        return;
    }

    if (m_detailTitle) {
        auto title = CloudConfigPopupImplementation::truncate(entry->name, 22);
        m_detailTitle->setString(title.c_str());
        float const maxW = m_detailWidth - 14.f;
        if (m_detailTitle->getScaledContentSize().width > maxW &&
            m_detailTitle->getContentSize().width > 0.f) {
            m_detailTitle->setScale(maxW / m_detailTitle->getContentSize().width);
        } else {
            m_detailTitle->setScale(0.34f);
        }
    }

    std::string tags;
    for (size_t index = 0; index < entry->tags.size() && index < 3; ++index) {
        if (index)
            tags += ", ";
        tags += entry->tags[index];
    }
    if (tags.empty())
        tags = "—";

    bool const tight = PlatformLimits::isConstrainedDevice();
    if (m_detailMeta) {
        m_detailMeta->setString(
            CloudConfigPopupImplementation::truncate(
                fmt::format("by {} · {}", entry->author.empty() ? "?" : entry->author, tags),
                tight ? 32 : 42)
                .c_str());
    }

    bool const verOk = CloudConfigIndex::meetsMinModVersion(*entry, this->liveModVersion());
    if (m_detailBody) {
        m_detailBody->setText(entry->description.empty() ? "(no description)" : entry->description);
    }
    if (m_detailFoot) {
        auto const state = CloudConfigStats::get().statsFor(entry->id);
        std::string statsBit;
        if (state.downloads > 0 || state.ratingCount > 0) {
            if (state.ratingCount > 0) {
                statsBit =
                    tight ? fmt::format(
                                " · {}dl · {:.1f}*",
                                CloudConfigPopupImplementation::formatDownloads(state.downloads),
                                CloudConfigStats::averageRating(state))
                          : fmt::format(
                                " · {}dl · {:.1f}* ({}r)",
                                CloudConfigPopupImplementation::formatDownloads(state.downloads),
                                CloudConfigStats::averageRating(state), state.ratingCount);
            } else {
                statsBit = fmt::format(
                    " · {}dl", CloudConfigPopupImplementation::formatDownloads(state.downloads));
            }
        }
        std::string const line1 =
            fmt::format("{}{} · min {}", CloudConfigPopupImplementation::formatBytes(entry->size),
                        statsBit, entry->minModVersion.empty() ? "any" : entry->minModVersion);
        std::string const line2 =
            verOk ? (this->isCached(entry->id) ? "Ready to install." : "Downloads on Install.")
                  : "Needs newer EarlyBuffer.";
        m_detailFoot->setString(
            fmt::format("{}\n{}", CloudConfigPopupImplementation::truncate(line1, tight ? 36 : 48),
                        line2)
                .c_str());
        float const footMax = m_detailWidth - 12.f;
        float const baseScale = tight ? 0.30f : 0.34f;
        m_detailFoot->setScale(baseScale);
        if (m_detailFoot->getContentSize().width > 0.f &&
            m_detailFoot->getScaledContentSize().width > footMax) {
            m_detailFoot->setScale(footMax / m_detailFoot->getContentSize().width);
        }
    }
    if (m_installBtn)
        m_installBtn->setEnabled(verOk && !m_busyUi);
    this->updateRateUi();
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
