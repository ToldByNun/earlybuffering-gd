#include "cloud_config_list_cell.hpp"
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <algorithm>
#include <cocos-ext.h>
#include <fmt/format.h>

std::string CloudConfigListCellImplementation::formatDlBadge(int count) {
    if (count >= 1000000)
        return fmt::format("{}M", count / 1000000);
    if (!(count >= 10000)) {
        return fmt::format("{}", count);
    }
    return fmt::format("{}k", count / 1000);
}

CloudConfigListCell* CloudConfigListCell::create(CloudConfigIndex::PackEntry const& entry,
                                                 float width, int index, bool selected, bool active,
                                                 bool cached, bool updateAvailable,
                                                 StatsBadge const& stats, SelectCallback onSelect) {
    auto* createdObject = new CloudConfigListCell();
    if (createdObject->init(entry, width, index, selected, active, cached, updateAvailable, stats,
                            std::move(onSelect))) {
        createdObject->autorelease();
        return createdObject;
    }
    delete createdObject;
    return nullptr;
}

bool CloudConfigListCell::init(CloudConfigIndex::PackEntry const& entry, float width, int index,
                               bool selected, bool active, bool cached, bool updateAvailable,
                               StatsBadge const& stats, SelectCallback onSelect) {
    if (!geode::prelude::CCNode::init())
        return false;

    m_id = entry.id;
    m_onSelect = std::move(onSelect);
    this->setContentSize({width, kHeight});
    this->ignoreAnchorPointForPosition(false);
    this->setAnchorPoint({0.5f, 0.5f});

    bool const narrow = width < 170.f;

    GLubyte const firstValue = selected ? 255 : 210;
    geode::prelude::ccColor4B stripe = (index % 2 == 0)
                                           ? geode::prelude::ccColor4B{161, 88, 44, firstValue}
                                           : geode::prelude::ccColor4B{191, 114, 62, firstValue};
    if (selected) {
        stripe = geode::prelude::ccColor4B{70, 120, 185, 255};
    }

    auto* bg = geode::prelude::CCLayerColor::create(stripe, width, kHeight);
    bg->setAnchorPoint({0.5f, 0.5f});
    bg->ignoreAnchorPointForPosition(false);
    bg->setID("cloud-cell-bg"_spr);
    this->addChildAtPosition(bg, geode::Anchor::Center);

    if (selected) {
        auto* accent =
            geode::prelude::CCLayerColor::create({255, 255, 255, 220}, 3.f, kHeight - 4.f);
        accent->setAnchorPoint({0.f, 0.5f});
        accent->ignoreAnchorPointForPosition(false);
        accent->setID("cloud-cell-accent"_spr);
        this->addChildAtPosition(accent, geode::Anchor::Left, ccp(2.f, 0.f));
    }

    auto* menu = geode::prelude::CCMenu::create();
    menu->setContentSize({width, kHeight});
    menu->setAnchorPoint({0.5f, 0.5f});
    menu->ignoreAnchorPointForPosition(false);
    menu->setID("cloud-cell-menu"_spr);
    this->addChildAtPosition(menu, geode::Anchor::Center);

    auto* hitBg = geode::prelude::CCLayerColor::create({0, 0, 0, 0}, width, kHeight);
    hitBg->setAnchorPoint({0.5f, 0.5f});
    hitBg->ignoreAnchorPointForPosition(false);
    auto* hit =
        CCMenuItemSpriteExtra::create(hitBg, this, menu_selector(CloudConfigListCell::onClick));
    hit->setContentSize({width, kHeight});
    hit->setID("cloud-cell-hit"_spr);
    menu->addChildAtPosition(hit, geode::Anchor::Center);

    // Right badges first so title can shrink to leftover width.
    float badgeX = -6.f;
    auto addBadge = [&](char const* text, geode::prelude::ccColor3B color) {
        auto* lab = geode::prelude::CCLabelBMFont::create(text, "bigFont.fnt");
        lab->setScale(narrow ? 0.18f : 0.20f);
        lab->setColor(color);
        lab->setAnchorPoint({1.f, 0.5f});
        this->addChildAtPosition(lab, geode::Anchor::Right, ccp(badgeX, 8.f));
        badgeX -= lab->getScaledContentSize().width + 5.f;
    };

    if (active) {
        addBadge(narrow ? "ON" : "ACTIVE", {100, 255, 140});
    } else if (updateAvailable) {
        addBadge(narrow ? "UP" : "UPDATE", {255, 200, 80});
    } else if (cached) {
        addBadge(narrow ? "OK" : "SAVED", {180, 210, 255});
    }

    if (stats.show && (stats.downloads > 0 || stats.ratingCount > 0)) {
        std::string bits;
        if (stats.downloads > 0) {
            bits += CloudConfigListCellImplementation::formatDlBadge(stats.downloads);
            bits += "dl";
        }
        if (stats.ratingCount > 0) {
            if (!bits.empty())
                bits += " ";
            bits += fmt::format("{:.1f}*", stats.ratingAvg);
        }
        if (bits.size() > 12)
            bits = bits.substr(0, 12);
        auto* lab = geode::prelude::CCLabelBMFont::create(bits.c_str(), "goldFont.fnt");
        lab->setScale(narrow ? 0.22f : 0.24f);
        lab->setColor({220, 235, 255});
        lab->setAnchorPoint({1.f, 0.5f});
        lab->setID("cloud-cell-stats"_spr);
        this->addChildAtPosition(lab, geode::Anchor::Right, ccp(-6.f, -8.f));
        badgeX = std::min(badgeX, -6.f - lab->getScaledContentSize().width - 4.f);
    }

    float const nameMax = std::max(40.f, width + badgeX - 14.f);

    auto* name = geode::prelude::CCLabelBMFont::create(entry.name.c_str(), "bigFont.fnt");
    name->setScale(narrow ? 0.32f : 0.36f);
    name->setAnchorPoint({0.f, 0.5f});
    name->setID("cloud-cell-name"_spr);
    if (name->getScaledContentSize().width > nameMax && name->getContentSize().width > 0.f) {
        name->setScale(nameMax / name->getContentSize().width);
    }
    this->addChildAtPosition(name, geode::Anchor::Left, ccp(8.f, 7.f));

    std::string sub = entry.author.empty() ? entry.id : (entry.author + " · " + entry.id);
    auto* author = geode::prelude::CCLabelBMFont::create(sub.c_str(), "goldFont.fnt");
    author->setScale(narrow ? 0.24f : 0.28f);
    author->setAnchorPoint({0.f, 0.5f});
    author->setOpacity(200);
    author->setID("cloud-cell-author"_spr);
    float const subMax = std::max(40.f, nameMax);
    if (author->getScaledContentSize().width > subMax && author->getContentSize().width > 0.f) {
        author->setScale(subMax / author->getContentSize().width);
    }
    this->addChildAtPosition(author, geode::Anchor::Left, ccp(8.f, -8.f));

    this->setContentSize({width, kHeight});
    return true;
}

void CloudConfigListCell::onClick(geode::prelude::CCObject*) {
    if (m_onSelect)
        m_onSelect(m_id);
}
