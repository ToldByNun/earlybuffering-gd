#include "CloudConfigListCell.hpp"

#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <cocos-ext.h>

#include <algorithm>
#include <fmt/format.h>

using namespace geode::prelude;

namespace {

std::string formatDlBadge(int n) {
    if (n >= 1000000) return fmt::format("{}M", n / 1000000);
    if (n >= 10000) return fmt::format("{}k", n / 1000);
    return fmt::format("{}", n);
}

} // namespace

CloudConfigListCell* CloudConfigListCell::create(
    CloudConfigIndex::PackEntry const& entry,
    float width,
    int index,
    bool selected,
    bool active,
    bool cached,
    bool updateAvailable,
    StatsBadge const& stats,
    SelectCallback onSelect
) {
    auto* ret = new CloudConfigListCell();
    if (ret->init(
            entry,
            width,
            index,
            selected,
            active,
            cached,
            updateAvailable,
            stats,
            std::move(onSelect)
        )) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool CloudConfigListCell::init(
    CloudConfigIndex::PackEntry const& entry,
    float width,
    int index,
    bool selected,
    bool active,
    bool cached,
    bool updateAvailable,
    StatsBadge const& stats,
    SelectCallback onSelect
) {
    if (!CCNode::init()) return false;

    m_id = entry.id;
    m_onSelect = std::move(onSelect);
    // Lock size before children so ColumnLayout never measures overflow.
    this->setContentSize({ width, kHeight });
    this->ignoreAnchorPointForPosition(false);
    this->setAnchorPoint({ 0.5f, 0.5f });

    GLubyte const a = selected ? 255 : 210;
    ccColor4B stripe = (index % 2 == 0)
        ? ccColor4B{ 161, 88, 44, a }
        : ccColor4B{ 191, 114, 62, a };
    if (selected) {
        stripe = ccColor4B{ 70, 120, 185, 255 };
    }

    auto* bg = CCLayerColor::create(stripe, width, kHeight);
    bg->setAnchorPoint({ 0.5f, 0.5f });
    bg->ignoreAnchorPointForPosition(false);
    bg->setID("cloud-cell-bg"_spr);
    this->addChildAtPosition(bg, Anchor::Center);

    if (selected) {
        // Thin left accent — no Scale9 (that was bleeding into the neighbor row).
        auto* accent = CCLayerColor::create({ 255, 255, 255, 220 }, 3.f, kHeight - 4.f);
        accent->setAnchorPoint({ 0.f, 0.5f });
        accent->ignoreAnchorPointForPosition(false);
        accent->setID("cloud-cell-accent"_spr);
        this->addChildAtPosition(accent, Anchor::Left, ccp(2.f, 0.f));
    }

    auto* menu = CCMenu::create();
    menu->setContentSize({ width, kHeight });
    menu->setAnchorPoint({ 0.5f, 0.5f });
    menu->ignoreAnchorPointForPosition(false);
    menu->setID("cloud-cell-menu"_spr);
    this->addChildAtPosition(menu, Anchor::Center);

    auto* hitBg = CCLayerColor::create({ 0, 0, 0, 0 }, width, kHeight);
    hitBg->setAnchorPoint({ 0.5f, 0.5f });
    hitBg->ignoreAnchorPointForPosition(false);
    auto* hit = CCMenuItemSpriteExtra::create(
        hitBg, this, menu_selector(CloudConfigListCell::onClick)
    );
    hit->setContentSize({ width, kHeight });
    hit->setID("cloud-cell-hit"_spr);
    menu->addChildAtPosition(hit, Anchor::Center);

    // Right-side status: one badge only (ACTIVE wins).
    float badgeX = -8.f;
    auto addBadge = [&](char const* text, ccColor3B color) {
        auto* lab = CCLabelBMFont::create(text, "bigFont.fnt");
        lab->setScale(0.22f);
        lab->setColor(color);
        lab->setAnchorPoint({ 1.f, 0.5f });
        this->addChildAtPosition(lab, Anchor::Right, ccp(badgeX, 8.f));
        badgeX -= lab->getScaledContentSize().width + 6.f;
    };

    if (active) {
        addBadge("ACTIVE", { 100, 255, 140 });
    }
    else if (updateAvailable) {
        addBadge("UPDATE", { 255, 200, 80 });
    }
    else if (cached) {
        addBadge("SAVED", { 180, 210, 255 });
    }

    // ASCII counters — BMFonts often lack ↓/★.
    if (stats.show && (stats.downloads > 0 || stats.ratingCount > 0)) {
        std::string bits;
        if (stats.downloads > 0) {
            bits += formatDlBadge(stats.downloads);
            bits += "dl";
        }
        if (stats.ratingCount > 0) {
            if (!bits.empty()) bits += " ";
            bits += fmt::format("{:.1f}*", stats.ratingAvg);
        }
        auto* lab = CCLabelBMFont::create(bits.c_str(), "goldFont.fnt");
        lab->setScale(0.26f);
        lab->setColor({ 220, 235, 255 });
        lab->setAnchorPoint({ 1.f, 0.5f });
        lab->setID("cloud-cell-stats"_spr);
        this->addChildAtPosition(lab, Anchor::Right, ccp(-8.f, -8.f));
        badgeX = std::min(badgeX, -8.f - lab->getScaledContentSize().width - 4.f);
    }

    float const nameMax = std::max(48.f, width + badgeX - 16.f);

    auto* name = CCLabelBMFont::create(entry.name.c_str(), "bigFont.fnt");
    name->setScale(0.36f);
    name->setAnchorPoint({ 0.f, 0.5f });
    name->setID("cloud-cell-name"_spr);
    if (name->getScaledContentSize().width > nameMax && name->getContentSize().width > 0.f) {
        name->setScale(nameMax / name->getContentSize().width);
    }
    this->addChildAtPosition(name, Anchor::Left, ccp(10.f, 7.f));

    std::string sub = entry.author.empty() ? entry.id : (entry.author + " · " + entry.id);
    auto* author = CCLabelBMFont::create(sub.c_str(), "goldFont.fnt");
    author->setScale(0.28f);
    author->setAnchorPoint({ 0.f, 0.5f });
    author->setOpacity(200);
    author->setID("cloud-cell-author"_spr);
    float const subMax = std::max(48.f, nameMax);
    if (author->getScaledContentSize().width > subMax && author->getContentSize().width > 0.f) {
        author->setScale(subMax / author->getContentSize().width);
    }
    this->addChildAtPosition(author, Anchor::Left, ccp(10.f, -8.f));

    // Re-lock after children (some nodes nudge measured size).
    this->setContentSize({ width, kHeight });
    return true;
}

void CloudConfigListCell::onClick(CCObject*) {
    if (m_onSelect) m_onSelect(m_id);
}
