#include "CloudConfigPopup.hpp"
#include "CloudConfigListCell.hpp"
#include "CloudConfigSharePopup.hpp"
#include "PauseActionBridge.hpp"

#include "../cloud/CloudConfigApply.hpp"
#include "../cloud/CloudConfigShare.hpp"
#include "../cloud/CloudConfigStats.hpp"
#include "../cloud/CloudConfigStore.hpp"
#include "../core/PlatformLimits.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/Layout.hpp>
#include <cocos-ext.h>

#include <algorithm>
#include <cmath>
#include <cctype>

using namespace geode::prelude;

std::string toLower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool containsInsensitive(std::string const& hay, std::string const& needleLower) {
    if (needleLower.empty()) return true;
    return toLower(hay).find(needleLower) != std::string::npos;
}

std::string truncate(std::string s, size_t maxChars) {
    if (s.size() <= maxChars) return s;
    if (maxChars <= 3) return s.substr(0, maxChars);
    return s.substr(0, maxChars - 3) + "...";
}

std::string formatBytes(size_t n) {
    if (n < 1024) return fmt::format("{} B", n);
    if (n < 1024 * 1024) return fmt::format("{:.1f} KB", n / 1024.0);
    return fmt::format("{:.1f} MB", n / (1024.0 * 1024.0));
}

std::string formatDownloads(int n) {
    if (n >= 1000000) return fmt::format("{:.1f}M", n / 1000000.0);
    if (n >= 10000) return fmt::format("{}k", n / 1000);
    return fmt::format("{}", n);
}

CloudConfigPopup* CloudConfigPopup::create() {
    auto* ret = new CloudConfigPopup();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

void CloudConfigPopup::open() {
    if (auto* p = create()) {
        p->show();
    }
}

bool CloudConfigPopup::init() {
    bool const tight = PlatformLimits::isConstrainedDevice();
    float const w = tight ? 310.f : 380.f;
    float const h = tight ? 240.f : 268.f;

    if (!Popup::init(w, h, "GJ_square01.png")) {
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

    // Status left, Refresh right — keeps the control off the close-button side.
    m_statusLabel = CCLabelBMFont::create("Loading…", "goldFont.fnt");
    m_statusLabel->setScale(0.28f);
    m_statusLabel->setAnchorPoint({ 0.f, 0.5f });
    m_statusLabel->setID("cloud-status"_spr);
    m_mainLayer->addChildAtPosition(m_statusLabel, Anchor::TopLeft, ccp(pad, -34.f));

    auto* topMenu = CCMenu::create();
    topMenu->setContentSize({ 70.f, 24.f });
    topMenu->setID("cloud-top-menu"_spr);
    m_mainLayer->addChildAtPosition(topMenu, Anchor::TopRight, ccp(-pad, -34.f));

    auto* refreshSpr = ButtonSprite::create(
        "Refresh", "bigFont.fnt", "GJ_button_05.png", 0.8f
    );
    refreshSpr->setScale(0.34f);
    m_refreshBtn = CCMenuItemSpriteExtra::create(
        refreshSpr, this, menu_selector(CloudConfigPopup::onRefresh)
    );
    m_refreshBtn->setID("cloud-refresh-btn"_spr);
    topMenu->addChildAtPosition(m_refreshBtn, Anchor::Center);

    m_listWidth = tight ? (w - pad * 2.f) : ((w - pad * 2.f - gap) * 0.50f);
    m_detailWidth = tight ? (w - pad * 2.f) : ((w - pad * 2.f - gap) * 0.50f);
    // Extra row for sort cycle under search.
    m_listHeight = tight ? 68.f : (h - 136.f);
    m_detailHeight = tight ? 56.f : (h - 122.f);

    float const sortRowH = 16.f;
    float const searchY = -54.f;
    float const sortY = searchY - 16.f;

    m_search = TextInput::create(m_listWidth, "Search packs…", "bigFont.fnt");
    m_search->setID("cloud-search"_spr);
    m_search->setMaxCharCount(64);
    m_search->setCommonFilter(CommonFilter::Any);
    m_search->setTextAlign(TextInputAlign::Left);
    m_search->setScale(0.86f);
    m_search->setCallback([this](std::string const& text) {
        m_searchQuery = text;
        this->rebuildList();
    });
    if (tight) {
        m_mainLayer->addChildAtPosition(m_search, Anchor::Top, ccp(0.f, searchY));
    }
    else {
        m_mainLayer->addChildAtPosition(
            m_search,
            Anchor::TopLeft,
            ccp(pad + m_listWidth * 0.5f, searchY)
        );
    }

    auto* sortMenu = CCMenu::create();
    sortMenu->setContentSize({ m_listWidth, sortRowH });
    sortMenu->ignoreAnchorPointForPosition(false);
    sortMenu->setAnchorPoint({ 0.f, 0.5f });
    sortMenu->setID("cloud-sort-menu"_spr);
    m_sortLabel = CCLabelBMFont::create("Sort: Default", "goldFont.fnt");
    m_sortLabel->setScale(0.28f);
    m_sortLabel->setAnchorPoint({ 0.f, 0.5f });
    m_sortLabel->setOpacity(220);
    m_sortLabel->setID("cloud-sort-label"_spr);
    m_sortBtn = CCMenuItemSpriteExtra::create(
        m_sortLabel, this, menu_selector(CloudConfigPopup::onSortCycle)
    );
    m_sortBtn->setAnchorPoint({ 0.f, 0.5f });
    m_sortBtn->setID("cloud-sort-btn"_spr);
    sortMenu->addChildAtPosition(m_sortBtn, Anchor::Left, ccp(0.f, 0.f));
    if (tight) {
        m_mainLayer->addChildAtPosition(
            sortMenu, Anchor::Top, ccp(-m_listWidth * 0.5f, sortY)
        );
    }
    else {
        m_mainLayer->addChildAtPosition(sortMenu, Anchor::TopLeft, ccp(pad, sortY));
    }
    this->updateSortLabel();

    m_listScroll = ScrollLayer::create(
        CCSize{ m_listWidth, m_listHeight }, true, true
    );
    m_listScroll->setID("cloud-list-scroll"_spr);
    m_listScroll->ignoreAnchorPointForPosition(false);
    m_listScroll->setAnchorPoint({ 0.f, 0.5f });

    m_listBorders = ListBorders::create();
    m_listBorders->setContentSize({ m_listWidth, m_listHeight });
    m_listBorders->setID("cloud-list-borders"_spr);
    m_listBorders->ignoreAnchorPointForPosition(false);
    m_listBorders->setAnchorPoint({ 0.f, 0.5f });

    if (tight) {
        m_mainLayer->addChildAtPosition(
            m_listScroll, Anchor::Center, ccp(-m_listWidth * 0.5f, 18.f)
        );
        m_mainLayer->addChildAtPosition(
            m_listBorders, Anchor::Center, ccp(-m_listWidth * 0.5f, 18.f)
        );
    }
    else {
        float const listY = -2.f;
        m_mainLayer->addChildAtPosition(
            m_listScroll, Anchor::Left, ccp(pad, listY)
        );
        m_mainLayer->addChildAtPosition(
            m_listBorders, Anchor::Left, ccp(pad, listY)
        );
    }

    m_detailRoot = CCNode::create();
    m_detailRoot->setContentSize({ m_detailWidth, m_detailHeight });
    m_detailRoot->setID("cloud-detail-root"_spr);
    m_detailRoot->ignoreAnchorPointForPosition(false);
    m_detailRoot->setAnchorPoint({ 1.f, 0.5f });

    m_detailBg = CCLayerColor::create(
        { 0, 0, 0, 80 },
        m_detailWidth,
        m_detailHeight
    );
    m_detailBg->ignoreAnchorPointForPosition(false);
    m_detailBg->setAnchorPoint({ 0.5f, 0.5f });
    m_detailBg->setID("cloud-detail-bg"_spr);
    m_detailRoot->addChildAtPosition(m_detailBg, Anchor::Center);

    m_detailTitle = CCLabelBMFont::create("Select a pack", "bigFont.fnt");
    m_detailTitle->setScale(0.34f);
    m_detailTitle->setAnchorPoint({ 0.f, 1.f });
    m_detailTitle->setID("cloud-detail-title"_spr);
    m_detailRoot->addChildAtPosition(
        m_detailTitle, Anchor::TopLeft, ccp(6.f, -6.f)
    );

    m_detailMeta = CCLabelBMFont::create(" ", "goldFont.fnt");
    m_detailMeta->setScale(0.26f);
    m_detailMeta->setAnchorPoint({ 0.f, 1.f });
    m_detailMeta->setOpacity(210);
    m_detailMeta->setID("cloud-detail-meta"_spr);
    m_detailRoot->addChildAtPosition(
        m_detailMeta, Anchor::TopLeft, ccp(6.f, -22.f)
    );

    m_detailBody = SimpleTextArea::create(
        "Pick a pack, then Install.\nShare… sets name & description.",
        "chatFont.fnt",
        0.38f,
        m_detailWidth - 12.f
    );
    m_detailBody->setWrappingMode(WORD_WRAP);
    m_detailBody->setMaxLines(tight ? 2 : 3);
    m_detailBody->setAlignment(kCCTextAlignmentLeft);
    m_detailBody->setAnchorPoint({ 0.f, 1.f });
    m_detailBody->setID("cloud-detail-body"_spr);
    m_detailRoot->addChildAtPosition(
        m_detailBody, Anchor::TopLeft, ccp(6.f, -36.f)
    );

    // Gray star sprite (resources/ui/eb_star.png) — tinted gold/gray in updateRateUi.
    m_rateHint = CCLabelBMFont::create("Rate", "goldFont.fnt");
    m_rateHint->setScale(0.26f);
    m_rateHint->setAnchorPoint({ 0.f, 0.5f });
    m_rateHint->setOpacity(210);
    m_rateHint->setID("cloud-rate-hint"_spr);
    m_detailRoot->addChildAtPosition(
        m_rateHint, Anchor::BottomLeft, ccp(6.f, 34.f)
    );

    m_rateMenu = CCMenu::create();
    m_rateMenu->setContentSize({ m_detailWidth - 12.f, 22.f });
    m_rateMenu->ignoreAnchorPointForPosition(false);
    m_rateMenu->setAnchorPoint({ 0.f, 0.5f });
    m_rateMenu->setID("cloud-rate-menu"_spr);
    m_rateMenu->setLayout(
        RowLayout::create()
            ->setGap(2.f)
            ->setAxisAlignment(AxisAlignment::Start)
            ->setCrossAxisAlignment(AxisAlignment::Center)
            ->setAutoScale(false)
    );
    for (int i = 0; i < 5; ++i) {
        auto* spr = CCSprite::create("eb_star.png"_spr);
        if (!spr) {
            // Fallback if resource missing from package.
            spr = CCSprite::createWithSpriteFrameName("GJ_starsIcon_001.png");
        }
        if (!spr) {
            auto* lab = CCLabelBMFont::create("*", "goldFont.fnt");
            lab->setScale(0.45f);
            m_rateStarSpr[i] = nullptr;
            m_rateBtns[i] = CCMenuItemSpriteExtra::create(
                lab, this, menu_selector(CloudConfigPopup::onRate)
            );
        }
        else {
            spr->setScale(0.42f);
            spr->setColor({ 170, 170, 170 });
            m_rateStarSpr[i] = spr;
            m_rateBtns[i] = CCMenuItemSpriteExtra::create(
                spr, this, menu_selector(CloudConfigPopup::onRate)
            );
        }
        m_rateBtns[i]->setTag(i + 1);
        m_rateBtns[i]->setID(fmt::format("cloud-rate-{}", i + 1));
        m_rateMenu->addChild(m_rateBtns[i]);
    }
    m_rateMenu->updateLayout();
    m_detailRoot->addChildAtPosition(
        m_rateMenu, Anchor::BottomLeft, ccp(38.f, 34.f)
    );

    m_detailFoot = CCLabelBMFont::create(" ", "chatFont.fnt");
    m_detailFoot->setScale(0.34f);
    m_detailFoot->setAnchorPoint({ 0.f, 0.f });
    m_detailFoot->setOpacity(200);
    m_detailFoot->setID("cloud-detail-foot"_spr);
    m_detailRoot->addChildAtPosition(
        m_detailFoot, Anchor::BottomLeft, ccp(6.f, 4.f)
    );

    if (tight) {
        m_mainLayer->addChildAtPosition(
            m_detailRoot, Anchor::Bottom, ccp(m_detailWidth * 0.5f, 48.f)
        );
        m_detailRoot->setAnchorPoint({ 0.5f, 0.f });
    }
    else {
        m_mainLayer->addChildAtPosition(
            m_detailRoot, Anchor::Right, ccp(-pad, -2.f)
        );
    }

    auto* bottom = CCMenu::create();
    bottom->setContentSize({ w - pad * 2.f, 28.f });
    bottom->setID("cloud-bottom-menu"_spr);
    m_mainLayer->addChildAtPosition(bottom, Anchor::Bottom, ccp(0.f, 18.f));
    bottom->setLayout(
        RowLayout::create()
            ->setGap(10.f)
            ->setAxisAlignment(AxisAlignment::Center)
            ->setCrossAxisAlignment(AxisAlignment::Center)
    );

    auto* installSpr = ButtonSprite::create(
        "Install", "bigFont.fnt", "GJ_button_01.png", 0.8f
    );
    installSpr->setScale(0.44f);
    m_installBtn = CCMenuItemSpriteExtra::create(
        installSpr, this, menu_selector(CloudConfigPopup::onInstall)
    );
    m_installBtn->setID("cloud-install-btn"_spr);
    bottom->addChild(m_installBtn);

    auto* shareSpr = ButtonSprite::create(
        "Share…", "bigFont.fnt", "GJ_button_05.png", 0.8f
    );
    shareSpr->setScale(0.44f);
    auto* shareBtn = CCMenuItemSpriteExtra::create(
        shareSpr, this, menu_selector(CloudConfigPopup::onShare)
    );
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
    if (auto* mod = Mod::get()) {
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
    if (!this->isCached(e.id)) return false;
    std::string body;
    std::string err;
    if (!CloudConfigStore::get().readPack(e.id, body, err)) return false;
    CloudConfigStore::ActiveInfo active{};
    if (CloudConfigStore::get().readActive(active, err)
        && active.id == e.id
        && !active.sha256.empty()) {
        return active.sha256 != e.sha256;
    }
    return body.size() != e.size;
}

CloudConfigIndex::PackEntry const* CloudConfigPopup::selectedEntry() const {
    return CloudConfigIndex::findById(
        CloudConfigService::get().catalog(), m_selectedId
    );
}

bool CloudConfigPopup::packMatchesSearch(CloudConfigIndex::PackEntry const& e) const {
    auto const q = toLower(m_searchQuery);
    if (q.empty()) return true;
    if (containsInsensitive(e.name, q)) return true;
    if (containsInsensitive(e.author, q)) return true;
    if (containsInsensitive(e.id, q)) return true;
    if (containsInsensitive(e.description, q)) return true;
    for (auto const& t : e.tags) {
        if (containsInsensitive(t, q)) return true;
    }
    return false;
}

void CloudConfigPopup::setStatus(std::string const& text, bool isError) {
    if (!m_statusLabel) return;
    m_statusLabel->setString(truncate(text, 48).c_str());
    m_statusLabel->setColor(isError ? ccColor3B{ 255, 90, 90 } : ccColor3B{ 255, 255, 255 });
}

void CloudConfigPopup::ensureLocalCatalog() {
    if (!CloudConfigService::get().catalog().packs.empty()) return;
    CloudConfigService::RefreshResult local{};
    (void)CloudConfigService::get().loadLocalCatalog(local);
}

CloudConfigListCell::StatsBadge CloudConfigPopup::statsBadgeFor(std::string const& id) const {
    CloudConfigListCell::StatsBadge b{};
    auto const s = CloudConfigStats::get().statsFor(id);
    if (s.downloads <= 0 && s.ratingCount <= 0) return b;
    b.show = true;
    b.downloads = s.downloads;
    b.ratingCount = s.ratingCount;
    b.ratingAvg = CloudConfigStats::averageRating(s);
    return b;
}

void CloudConfigPopup::startStatsFetch() {
    uint64_t const gen = ++m_statsGen;
    Ref<CloudConfigPopup> self = this;
    CloudConfigStats::get().fetchStats([self, gen](bool ok, std::string const&) {
        if (!self || gen != self->m_statsGen) return;
        if (!ok) return;
        self->rebuildList();
        self->updateDetail();
    });
}

void CloudConfigPopup::loadInitial() {
    this->ensureLocalCatalog();
    auto const& cat = CloudConfigService::get().catalog();
    char const* src =
        CloudConfigService::get().source() == CloudConfigService::CatalogSource::Seed
            ? "Seed"
        : CloudConfigService::get().source() == CloudConfigService::CatalogSource::DiskCache
            ? "Cache"
            : "Local";
    this->setStatus(fmt::format("{} · {} packs", src, cat.packs.size()), cat.packs.empty());
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
    uint64_t const gen = ++m_opGen;
    this->setStatus(force ? "Refreshing (force)…" : "Refreshing…", false);
    if (m_refreshBtn) m_refreshBtn->setEnabled(false);

    std::string const custom = CloudConfigService::readCatalogUrlSetting();
    Ref<CloudConfigPopup> self = this;
    CloudConfigService::get().refreshCatalog(
        custom,
        [self, gen](CloudConfigService::RefreshResult const& r) {
            if (!self || gen != self->m_opGen) return;
            self->onRefreshDone(r);
        },
        force
    );
}

void CloudConfigPopup::onRefreshDone(CloudConfigService::RefreshResult const& r) {
    m_busyUi = false;
    if (m_refreshBtn) m_refreshBtn->setEnabled(true);

    if (r.busy) {
        this->setStatus("Busy…", false);
        return;
    }
    if (!r.ok) {
        this->ensureLocalCatalog();
        auto const n = CloudConfigService::get().catalog().packs.size();
        this->setStatus(fmt::format("Offline · {} packs", n), n == 0);
        if (m_selectedId.empty() && n > 0) {
            m_selectedId = CloudConfigService::get().catalog().packs.front().id;
        }
        this->rebuildList();
        this->updateDetail();
        this->startStatsFetch();
        return;
    }

    char const* src = r.usedFallback ? "Online*" : "Online";
    std::string stamp = r.catalog.updatedAt;
    if (stamp.size() > 16) stamp = stamp.substr(0, 16); // 2026-11-04T15:16
    if (!stamp.empty()) {
        this->setStatus(
            fmt::format("{} · {} packs · {}", src, r.catalog.packs.size(), stamp),
            false
        );
    }
    else {
        this->setStatus(fmt::format("{} · {} packs", src, r.catalog.packs.size()), false);
    }
    if (r.catalog.packs.empty()) {
        this->ensureLocalCatalog();
    }
    if (m_selectedId.empty() && !CloudConfigService::get().catalog().packs.empty()) {
        m_selectedId = CloudConfigService::get().catalog().packs.front().id;
    }
    else if (!CloudConfigIndex::findById(CloudConfigService::get().catalog(), m_selectedId)
             && !CloudConfigService::get().catalog().packs.empty()) {
        m_selectedId = CloudConfigService::get().catalog().packs.front().id;
    }
    this->rebuildList();
    this->updateDetail();
    this->startStatsFetch();
}

char const* CloudConfigPopup::sortModeLabel() const {
    switch (m_sortMode) {
    case SortMode::Downloads: return "Downloads";
    case SortMode::Rating: return "Rating";
    case SortMode::Popular: return "Popular";
    case SortMode::Name: return "Name";
    case SortMode::Default:
    default: return "Default";
    }
}

void CloudConfigPopup::updateSortLabel() {
    if (!m_sortLabel) return;
    m_sortLabel->setString(fmt::format("Sort: {}", this->sortModeLabel()).c_str());
}

void CloudConfigPopup::onSortCycle(CCObject*) {
    int next = static_cast<int>(m_sortMode) + 1;
    if (next >= static_cast<int>(SortMode::Count)) next = 0;
    m_sortMode = static_cast<SortMode>(next);
    this->updateSortLabel();
    this->rebuildList();
}

void CloudConfigPopup::applySort(std::vector<CloudConfigIndex::PackEntry const*>& rows) const {
    if (rows.size() < 2) return;
    if (m_sortMode == SortMode::Default) return; // catalog order

    auto statsOf = [](CloudConfigIndex::PackEntry const* p) {
        return CloudConfigStats::get().statsFor(p->id);
    };

    std::stable_sort(
        rows.begin(),
        rows.end(),
        [&](CloudConfigIndex::PackEntry const* a, CloudConfigIndex::PackEntry const* b) {
            if (!a || !b) return a != nullptr;
            auto const sa = statsOf(a);
            auto const sb = statsOf(b);
            switch (m_sortMode) {
            case SortMode::Downloads:
                if (sa.downloads != sb.downloads) return sa.downloads > sb.downloads;
                break;
            case SortMode::Rating: {
                float const ra = CloudConfigStats::averageRating(sa);
                float const rb = CloudConfigStats::averageRating(sb);
                if (ra != rb) return ra > rb;
                if (sa.ratingCount != sb.ratingCount) return sa.ratingCount > sb.ratingCount;
                break;
            }
            case SortMode::Popular: {
                float const pa = CloudConfigStats::popularityScore(sa);
                float const pb = CloudConfigStats::popularityScore(sb);
                if (pa != pb) return pa > pb;
                break;
            }
            case SortMode::Name: {
                auto const na = toLower(a->name);
                auto const nb = toLower(b->name);
                if (na != nb) return na < nb;
                break;
            }
            case SortMode::Default:
            default:
                break;
            }
            return a->id < b->id;
        }
    );
}

void CloudConfigPopup::rebuildList() {
    if (!m_listScroll || !m_listScroll->m_contentLayer) return;
    auto* content = m_listScroll->m_contentLayer;
    content->removeAllChildren();

    float const width = m_listScroll->getContentWidth();
    auto const& catalog = CloudConfigService::get().catalog();

    std::vector<CloudConfigIndex::PackEntry const*> rows;
    rows.reserve(catalog.packs.size());
    for (auto const& p : catalog.packs) {
        if (this->packMatchesSearch(p)) rows.push_back(&p);
    }
    this->applySort(rows);

    content->setLayout(
        ColumnLayout::create()
            ->setGap(2.f)
            ->setAxisReverse(true)
            ->setAxisAlignment(AxisAlignment::End)
            ->setAutoGrowAxis(m_listScroll->getContentHeight())
    );

    Ref<CloudConfigPopup> self = this;
    int index = 0;
    for (auto const* p : rows) {
        auto* cell = CloudConfigListCell::create(
            *p,
            width,
            index++,
            p->id == m_selectedId,
            this->isActive(p->id),
            this->isCached(p->id),
            this->hasUpdate(*p),
            this->statsBadgeFor(p->id),
            [self](std::string const& id) {
                if (self) self->selectPack(id);
            }
        );
        if (cell) content->addChild(cell);
    }

    if (rows.empty()) {
        auto* emptyNode = CCNode::create();
        emptyNode->setContentSize({ width, m_listScroll->getContentHeight() });
        auto* empty = CCLabelBMFont::create(
            m_searchQuery.empty() ? "No packs yet" : "No matches",
            "bigFont.fnt"
        );
        empty->setScale(0.32f);
        empty->setOpacity(160);
        emptyNode->addChildAtPosition(empty, Anchor::Center);
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
    auto const* e = this->selectedEntry();
    bool const hasPack = e != nullptr;
    bool const enable = hasPack && !m_busyUi && !m_rateBusy;
    int const mine = hasPack ? CloudConfigStats::get().myStarsFor(e->id) : 0;

    // Paint: own vote if set, else community average (display only).
    int filled = 0;
    if (mine >= 1 && mine <= 5) {
        filled = mine;
    }
    else if (hasPack) {
        auto const s = CloudConfigStats::get().statsFor(e->id);
        if (s.ratingCount > 0) {
            filled = static_cast<int>(std::lround(
                static_cast<double>(CloudConfigStats::averageRating(s))
            ));
            if (filled < 1) filled = 1;
            if (filled > 5) filled = 5;
        }
    }

    if (m_rateHint) {
        if (!hasPack) {
            m_rateHint->setString(" ");
            m_rateHint->setOpacity(0);
        }
        else if (m_rateBusy) {
            m_rateHint->setString("…");
            m_rateHint->setOpacity(210);
        }
        else if (mine > 0) {
            m_rateHint->setString("Yours");
            m_rateHint->setOpacity(210);
        }
        else {
            m_rateHint->setString("Rate");
            m_rateHint->setOpacity(210);
        }
    }

    ccColor3B const colOn = { 255, 210, 70 };
    ccColor3B const colOff = { 150, 150, 150 };

    for (int i = 0; i < 5; ++i) {
        auto* btn = m_rateBtns[i];
        if (!btn) continue;
        btn->setVisible(hasPack);
        btn->setEnabled(enable);

        bool const on = filled > 0 && (i < filled);
        ccColor3B const col = on ? colOn : colOff;
        GLubyte const op = !hasPack ? 0 : (enable ? 255 : 140);

        if (auto* spr = m_rateStarSpr[i]) {
            spr->setColor(col);
            spr->setOpacity(op);
        }
        else {
            btn->setColor(col);
            btn->setOpacity(op);
        }
    }
    if (m_rateMenu) m_rateMenu->setVisible(hasPack);
}

void CloudConfigPopup::updateDetail() {
    auto const* e = this->selectedEntry();
    if (!e) {
        if (m_detailTitle) m_detailTitle->setString("Select a pack");
        if (m_detailMeta) m_detailMeta->setString(" ");
        if (m_detailBody) {
            m_detailBody->setText(
                "Pick a pack from the list.\nShare… sets name & description."
            );
        }
        if (m_detailFoot) m_detailFoot->setString(" ");
        if (m_installBtn) m_installBtn->setEnabled(false);
        this->updateRateUi();
        return;
    }

    if (m_detailTitle) {
        auto title = truncate(e->name, 22);
        m_detailTitle->setString(title.c_str());
        float const maxW = m_detailWidth - 14.f;
        if (m_detailTitle->getScaledContentSize().width > maxW
            && m_detailTitle->getContentSize().width > 0.f) {
            m_detailTitle->setScale(maxW / m_detailTitle->getContentSize().width);
        }
        else {
            m_detailTitle->setScale(0.34f);
        }
    }

    std::string tags;
    for (size_t i = 0; i < e->tags.size() && i < 3; ++i) {
        if (i) tags += ", ";
        tags += e->tags[i];
    }
    if (tags.empty()) tags = "—";

    if (m_detailMeta) {
        m_detailMeta->setString(truncate(fmt::format(
            "by {} · {}",
            e->author.empty() ? "?" : e->author,
            tags
        ), 42).c_str());
    }

    bool const verOk = CloudConfigIndex::meetsMinModVersion(*e, this->liveModVersion());
    if (m_detailBody) {
        m_detailBody->setText(
            e->description.empty() ? "(no description)" : e->description
        );
    }
    if (m_detailFoot) {
        auto const s = CloudConfigStats::get().statsFor(e->id);
        std::string statsBit;
        if (s.downloads > 0 || s.ratingCount > 0) {
            if (s.ratingCount > 0) {
                statsBit = fmt::format(
                    " · {}dl · {:.1f}* ({} rating{})",
                    formatDownloads(s.downloads),
                    CloudConfigStats::averageRating(s),
                    s.ratingCount,
                    s.ratingCount == 1 ? "" : "s"
                );
            }
            else {
                statsBit = fmt::format(
                    " · {} dl",
                    formatDownloads(s.downloads)
                );
            }
        }
        m_detailFoot->setString(fmt::format(
            "{}{} · min {}\n{}",
            formatBytes(e->size),
            statsBit,
            e->minModVersion.empty() ? "any" : e->minModVersion,
            verOk
                ? (this->isCached(e->id) ? "Ready to install." : "Downloads on Install.")
                : "Needs newer EarlyBuffer."
        ).c_str());
    }
    if (m_installBtn) m_installBtn->setEnabled(verOk && !m_busyUi);
    this->updateRateUi();
}

void CloudConfigPopup::onRefresh(CCObject*) {
    this->startRefresh(true);
}

void CloudConfigPopup::onInstall(CCObject*) {
    auto const* e = this->selectedEntry();
    if (!e || m_busyUi) return;
    if (!CloudConfigIndex::meetsMinModVersion(*e, this->liveModVersion())) {
        FLAlertLayer::create(
            "Cloud Configs",
            "This pack needs a newer EarlyBuffer version.",
            "OK"
        )->show();
        return;
    }

    std::string const id = e->id;
    createQuickPopup(
        "Install config?",
        "Replace current EarlyBuffer settings?\n"
        "<cy>Keybinds</c> and <cy>Dev Lab</c> keys stay unchanged.",
        "Cancel",
        "Install",
        [this, id](auto*, bool btn2) {
            if (btn2) this->doInstall(id);
        }
    );
}

void CloudConfigPopup::doInstall(std::string const& id) {
    auto const* e = CloudConfigIndex::findById(CloudConfigService::get().catalog(), id);
    if (!e) return;

    m_busyUi = true;
    uint64_t const gen = ++m_opGen;
    this->setStatus("Downloading…", false);
    if (m_installBtn) m_installBtn->setEnabled(false);

    CloudConfigIndex::PackEntry entry = *e;
    Ref<CloudConfigPopup> self = this;
    CloudConfigService::get().fetchPack(
        id,
        this->hasUpdate(entry),
        [self, gen, entry](CloudConfigService::PackFetchResult const& fr) {
            if (!self || gen != self->m_opGen) return;
            self->onPackFetched(fr, entry);
        }
    );
}

void CloudConfigPopup::onPackFetched(
    CloudConfigService::PackFetchResult const& fr,
    CloudConfigIndex::PackEntry entry
) {
    m_busyUi = false;
    if (m_installBtn) m_installBtn->setEnabled(true);

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
        FLAlertLayer::create(
            "Cloud Configs",
            fr.error.empty() ? "Could not download pack." : fr.error.c_str(),
            "OK"
        )->show();
        return;
    }

    auto const applied = CloudConfigApply::applyPackBody(
        entry.id,
        entry.name,
        entry.sha256,
        entry.size,
        fr.body
    );
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

    PauseActionBridge::afterConfigApplied(fmt::format(
        "Installed cloud config \"{}\".\nApplied {} settings ({} skipped).",
        entry.name,
        applied.stats.applied,
        applied.stats.skipped
    ));
}

void CloudConfigPopup::trackInstallDownload(std::string const& packId) {
    Ref<CloudConfigPopup> self = this;
    CloudConfigStats::get().trackDownload(
        packId,
        [self](CloudConfigStats::TrackResult const& r) {
            if (!self) return;
            // Refresh badges when counters moved; silent on network errors.
            if (r.ok || r.duplicate || r.skippedLocal) {
                self->rebuildList();
                self->updateDetail();
            }
        }
    );
}

void CloudConfigPopup::onRate(CCObject* sender) {
    auto const* e = this->selectedEntry();
    if (!e || m_busyUi || m_rateBusy) return;

    auto* item = typeinfo_cast<CCMenuItem*>(sender);
    if (!item) return;
    int const stars = item->getTag();
    if (stars < 1 || stars > 5) return;

    // Same as current vote — just refresh paint.
    if (CloudConfigStats::get().myStarsFor(e->id) == stars) {
        this->updateRateUi();
        return;
    }

    m_rateBusy = true;
    uint64_t const gen = ++m_rateGen;
    this->updateRateUi();

    std::string const id = e->id;
    Ref<CloudConfigPopup> self = this;
    CloudConfigStats::get().trackRate(
        id,
        stars,
        [self, gen, stars](CloudConfigStats::TrackResult const& r) {
            if (!self || gen != self->m_rateGen) return;
            self->m_rateBusy = false;

            if (r.ok || r.duplicate || r.skippedLocal) {
                self->rebuildList();
                self->updateDetail();
                if (r.updated) {
                    self->setStatus(fmt::format("Updated {}/5", stars), false);
                }
                else if (r.ok && !r.duplicate && !r.skippedLocal) {
                    self->setStatus(fmt::format("Rated {}/5", stars), false);
                }
                return;
            }

            self->updateRateUi();
            FLAlertLayer::create(
                "Cloud Configs",
                r.error.empty() ? "Could not submit rating." : r.error.c_str(),
                "OK"
            )->show();
        }
    );
}

void CloudConfigPopup::onShare(CCObject*) {
    if (m_busyUi || m_shareOpen) return;
    this->setShareOpen(true);
    Ref<CloudConfigPopup> self = this;
    CloudConfigSharePopup::open([self]() {
        if (self) self->setShareOpen(false);
    });
}
