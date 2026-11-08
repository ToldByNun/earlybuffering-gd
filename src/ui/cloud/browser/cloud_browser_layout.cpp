#include "cloud_browser_layout.hpp"
#include "../../../cloud/cloud_config_store.hpp"
#include "../../../core/platform/platform_limits.hpp"
#include "../cloud_config_popup.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/Layout.hpp>
#include <cctype>
#include <cocos-ext.h>
#include <string>

bool CloudBrowserLayout::init(CloudConfigPopup& owner) {
    bool const tight = PlatformLimits::isConstrainedDevice();
    float const width = tight ? 310.f : 380.f;
    float const height = tight ? 240.f : 268.f;

    if (!owner.geode::prelude::Popup::init(width, height, "GJ_square01.png")) {
        return false;
    }

    owner.setTitle("Cloud Configs", "goldFont.fnt", 0.64f, 12.f);

    {
        CloudConfigStore::ActiveInfo active{};
        std::string err;
        if (CloudConfigStore::get().readActive(active, err)) {
            owner.m_activeId = active.id;
        }
    }

    float const pad = 14.f;
    float const gap = 8.f;

    // Status left; Refresh nudged inward so it clears title/close chrome.
    owner.m_statusLabel = geode::prelude::CCLabelBMFont::create("Loading…", "goldFont.fnt");
    owner.m_statusLabel->setScale(tight ? 0.26f : 0.28f);
    owner.m_statusLabel->setAnchorPoint({0.f, 0.5f});
    owner.m_statusLabel->setID("cloud-status"_spr);
    owner.m_mainLayer->addChildAtPosition(owner.m_statusLabel, geode::Anchor::TopLeft,
                                          ccp(pad, -34.f));

    float const refreshNudge = tight ? 28.f : 36.f; // plan: ~28–40px left of TopRight
    auto* topMenu = geode::prelude::CCMenu::create();
    topMenu->setContentSize({70.f, 24.f});
    topMenu->setID("cloud-top-menu"_spr);
    owner.m_mainLayer->addChildAtPosition(topMenu, geode::Anchor::TopRight,
                                          ccp(-(pad + refreshNudge), -34.f));

    auto* refreshSpr = ButtonSprite::create("Refresh", "bigFont.fnt", "GJ_button_05.png", 0.8f);
    refreshSpr->setScale(tight ? 0.30f : 0.34f);
    owner.m_refreshBtn = CCMenuItemSpriteExtra::create(refreshSpr, (&owner),
                                                       menu_selector(CloudConfigPopup::onRefresh));
    owner.m_refreshBtn->setID("cloud-refresh-btn"_spr);
    topMenu->addChildAtPosition(owner.m_refreshBtn, geode::Anchor::Center);

    owner.m_listWidth = tight ? (width - pad * 2.f) : ((width - pad * 2.f - gap) * 0.50f);
    owner.m_detailWidth = tight ? (width - pad * 2.f) : ((width - pad * 2.f - gap) * 0.50f);
    // Extra row for sort cycle under search.
    owner.m_listHeight = tight ? 68.f : (height - 136.f);
    owner.m_detailHeight = tight ? 56.f : (height - 122.f);

    float const sortRowH = 16.f;
    float const searchY = -54.f;
    float const sortY = searchY - 16.f;

    // No setScale on TextInput — scale shrinks the visual while layout uses full width.
    owner.m_search = geode::prelude::TextInput::create(
        tight ? (owner.m_listWidth - 8.f) : owner.m_listWidth, "Search packs…", "bigFont.fnt");
    owner.m_search->setID("cloud-search"_spr);
    owner.m_search->setMaxCharCount(64);
    owner.m_search->setCommonFilter(geode::prelude::CommonFilter::Any);
    owner.m_search->setTextAlign(geode::prelude::TextInputAlign::Left);
    owner.m_search->setCallback([&owner](std::string const& text) {
        owner.m_searchQuery = text;
        owner.rebuildList();
    });
    if (tight) {
        owner.m_mainLayer->addChildAtPosition(owner.m_search, geode::Anchor::Top,
                                              ccp(0.f, searchY));
    } else {
        owner.m_mainLayer->addChildAtPosition(owner.m_search, geode::Anchor::TopLeft,
                                              ccp(pad + owner.m_listWidth * 0.5f, searchY));
    }

    auto* sortMenu = geode::prelude::CCMenu::create();
    sortMenu->setContentSize({owner.m_listWidth, sortRowH});
    sortMenu->ignoreAnchorPointForPosition(false);
    sortMenu->setAnchorPoint({0.f, 0.5f});
    sortMenu->setID("cloud-sort-menu"_spr);
    owner.m_sortLabel = geode::prelude::CCLabelBMFont::create("Sort: Default", "goldFont.fnt");
    owner.m_sortLabel->setScale(tight ? 0.24f : 0.28f);
    owner.m_sortLabel->setAnchorPoint({0.f, 0.5f});
    owner.m_sortLabel->setOpacity(220);
    owner.m_sortLabel->setID("cloud-sort-label"_spr);
    owner.m_sortBtn = CCMenuItemSpriteExtra::create(owner.m_sortLabel, (&owner),
                                                    menu_selector(CloudConfigPopup::onSortCycle));
    owner.m_sortBtn->setAnchorPoint({0.f, 0.5f});
    owner.m_sortBtn->setID("cloud-sort-btn"_spr);
    sortMenu->addChildAtPosition(owner.m_sortBtn, geode::Anchor::Left, ccp(0.f, 0.f));
    if (tight) {
        owner.m_mainLayer->addChildAtPosition(sortMenu, geode::Anchor::Top,
                                              ccp(-owner.m_listWidth * 0.5f, sortY));
    } else {
        owner.m_mainLayer->addChildAtPosition(sortMenu, geode::Anchor::TopLeft, ccp(pad, sortY));
    }
    owner.updateSortLabel();

    owner.m_listScroll = geode::prelude::ScrollLayer::create(
        geode::prelude::CCSize{owner.m_listWidth, owner.m_listHeight}, true, true);
    owner.m_listScroll->setID("cloud-list-scroll"_spr);
    owner.m_listScroll->ignoreAnchorPointForPosition(false);
    owner.m_listScroll->setAnchorPoint({0.f, 0.5f});

    owner.m_listBorders = geode::prelude::ListBorders::create();
    owner.m_listBorders->setContentSize({owner.m_listWidth, owner.m_listHeight});
    owner.m_listBorders->setID("cloud-list-borders"_spr);
    owner.m_listBorders->ignoreAnchorPointForPosition(false);
    owner.m_listBorders->setAnchorPoint({0.f, 0.5f});

    if (tight) {
        owner.m_mainLayer->addChildAtPosition(owner.m_listScroll, geode::Anchor::Center,
                                              ccp(-owner.m_listWidth * 0.5f, 18.f));
        owner.m_mainLayer->addChildAtPosition(owner.m_listBorders, geode::Anchor::Center,
                                              ccp(-owner.m_listWidth * 0.5f, 18.f));
    } else {
        float const listY = -2.f;
        owner.m_mainLayer->addChildAtPosition(owner.m_listScroll, geode::Anchor::Left,
                                              ccp(pad, listY));
        owner.m_mainLayer->addChildAtPosition(owner.m_listBorders, geode::Anchor::Left,
                                              ccp(pad, listY));
    }

    owner.m_detailRoot = geode::prelude::CCNode::create();
    owner.m_detailRoot->setContentSize({owner.m_detailWidth, owner.m_detailHeight});
    owner.m_detailRoot->setID("cloud-detail-root"_spr);
    owner.m_detailRoot->ignoreAnchorPointForPosition(false);
    owner.m_detailRoot->setAnchorPoint({1.f, 0.5f});

    owner.m_detailBg = geode::prelude::CCLayerColor::create({0, 0, 0, 80}, owner.m_detailWidth,
                                                            owner.m_detailHeight);
    owner.m_detailBg->ignoreAnchorPointForPosition(false);
    owner.m_detailBg->setAnchorPoint({0.5f, 0.5f});
    owner.m_detailBg->setID("cloud-detail-bg"_spr);
    owner.m_detailRoot->addChildAtPosition(owner.m_detailBg, geode::Anchor::Center);

    owner.m_detailTitle = geode::prelude::CCLabelBMFont::create("Select a pack", "bigFont.fnt");
    owner.m_detailTitle->setScale(0.34f);
    owner.m_detailTitle->setAnchorPoint({0.f, 1.f});
    owner.m_detailTitle->setID("cloud-detail-title"_spr);
    owner.m_detailRoot->addChildAtPosition(owner.m_detailTitle, geode::Anchor::TopLeft,
                                           ccp(6.f, -6.f));

    owner.m_detailMeta = geode::prelude::CCLabelBMFont::create(" ", "goldFont.fnt");
    owner.m_detailMeta->setScale(0.26f);
    owner.m_detailMeta->setAnchorPoint({0.f, 1.f});
    owner.m_detailMeta->setOpacity(210);
    owner.m_detailMeta->setID("cloud-detail-meta"_spr);
    owner.m_detailRoot->addChildAtPosition(owner.m_detailMeta, geode::Anchor::TopLeft,
                                           ccp(6.f, -22.f));

    owner.m_detailBody = geode::prelude::SimpleTextArea::create(
        "Pick a pack, then Install.\nShare… sets name & description.", "chatFont.fnt", 0.38f,
        owner.m_detailWidth - 12.f);
    owner.m_detailBody->setWrappingMode(geode::prelude::WORD_WRAP);
    owner.m_detailBody->setMaxLines(tight ? 2 : 3);
    owner.m_detailBody->setAlignment(geode::prelude::kCCTextAlignmentLeft);
    owner.m_detailBody->setAnchorPoint({0.f, 1.f});
    owner.m_detailBody->setID("cloud-detail-body"_spr);
    owner.m_detailRoot->addChildAtPosition(owner.m_detailBody, geode::Anchor::TopLeft,
                                           ccp(6.f, -36.f));

    // Gray star sprite (resources/ui/eb_star.png) — tinted gold/gray in updateRateUi.
    owner.m_rateHint = geode::prelude::CCLabelBMFont::create("Rate", "goldFont.fnt");
    owner.m_rateHint->setScale(0.26f);
    owner.m_rateHint->setAnchorPoint({0.f, 0.5f});
    owner.m_rateHint->setOpacity(210);
    owner.m_rateHint->setID("cloud-rate-hint"_spr);
    owner.m_detailRoot->addChildAtPosition(owner.m_rateHint, geode::Anchor::BottomLeft,
                                           ccp(6.f, 34.f));

    owner.m_rateMenu = geode::prelude::CCMenu::create();
    owner.m_rateMenu->setContentSize({owner.m_detailWidth - 12.f, 22.f});
    owner.m_rateMenu->ignoreAnchorPointForPosition(false);
    owner.m_rateMenu->setAnchorPoint({0.f, 0.5f});
    owner.m_rateMenu->setID("cloud-rate-menu"_spr);
    owner.m_rateMenu->setLayout(geode::prelude::RowLayout::create()
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
            owner.m_rateStarSpr[index] = nullptr;
            owner.m_rateBtns[index] = CCMenuItemSpriteExtra::create(
                lab, (&owner), menu_selector(CloudConfigPopup::onRate));
        } else {
            sprite->setScale(tight ? 0.34f : 0.42f);
            sprite->setColor({170, 170, 170});
            owner.m_rateStarSpr[index] = sprite;
            owner.m_rateBtns[index] = CCMenuItemSpriteExtra::create(
                sprite, (&owner), menu_selector(CloudConfigPopup::onRate));
        }
        owner.m_rateBtns[index]->setTag(index + 1);
        owner.m_rateBtns[index]->setID(fmt::format("cloud-rate-{}", index + 1));
        owner.m_rateMenu->addChild(owner.m_rateBtns[index]);
    }
    owner.m_rateMenu->updateLayout();
    owner.m_detailRoot->addChildAtPosition(owner.m_rateMenu, geode::Anchor::BottomLeft,
                                           ccp(tight ? 32.f : 38.f, tight ? 30.f : 34.f));

    owner.m_detailFoot = geode::prelude::CCLabelBMFont::create(" ", "chatFont.fnt");
    owner.m_detailFoot->setScale(tight ? 0.30f : 0.34f);
    owner.m_detailFoot->setAnchorPoint({0.f, 0.f});
    owner.m_detailFoot->setOpacity(200);
    owner.m_detailFoot->setID("cloud-detail-foot"_spr);
    owner.m_detailRoot->addChildAtPosition(owner.m_detailFoot, geode::Anchor::BottomLeft,
                                           ccp(6.f, 3.f));

    if (tight) {
        owner.m_mainLayer->addChildAtPosition(owner.m_detailRoot, geode::Anchor::Bottom,
                                              ccp(owner.m_detailWidth * 0.5f, 48.f));
        owner.m_detailRoot->setAnchorPoint({0.5f, 0.f});
    } else {
        owner.m_mainLayer->addChildAtPosition(owner.m_detailRoot, geode::Anchor::Right,
                                              ccp(-pad, -2.f));
    }

    auto* bottom = geode::prelude::CCMenu::create();
    bottom->setContentSize({width - pad * 2.f, 28.f});
    bottom->setID("cloud-bottom-menu"_spr);
    owner.m_mainLayer->addChildAtPosition(bottom, geode::Anchor::Bottom, ccp(0.f, 18.f));
    bottom->setLayout(geode::prelude::RowLayout::create()
                          ->setGap(10.f)
                          ->setAxisAlignment(geode::prelude::AxisAlignment::Center)
                          ->setCrossAxisAlignment(geode::prelude::AxisAlignment::Center));

    auto* installSpr = ButtonSprite::create("Install", "bigFont.fnt", "GJ_button_01.png", 0.8f);
    installSpr->setScale(0.44f);
    owner.m_installBtn = CCMenuItemSpriteExtra::create(installSpr, (&owner),
                                                       menu_selector(CloudConfigPopup::onInstall));
    owner.m_installBtn->setID("cloud-install-btn"_spr);
    bottom->addChild(owner.m_installBtn);

    auto* shareSpr = ButtonSprite::create("Share…", "bigFont.fnt", "GJ_button_05.png", 0.8f);
    shareSpr->setScale(0.44f);
    auto* shareBtn =
        CCMenuItemSpriteExtra::create(shareSpr, (&owner), menu_selector(CloudConfigPopup::onShare));
    shareBtn->setID("cloud-share-btn"_spr);
    bottom->addChild(shareBtn);
    bottom->updateLayout();

    owner.loadInitial();
    return true;
}
