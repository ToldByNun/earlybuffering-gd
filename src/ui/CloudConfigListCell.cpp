#include "CloudConfigListCell.hpp"

#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <cocos-ext.h>

using namespace geode::prelude;

CloudConfigListCell* CloudConfigListCell::create(
    CloudConfigIndex::PackEntry const& entry,
    float width,
    int index,
    bool selected,
    bool active,
    bool cached,
    bool updateAvailable,
    SelectCallback onSelect
) {
    auto* ret = new CloudConfigListCell();
    if (ret->init(
            entry, width, index, selected, active, cached, updateAvailable, std::move(onSelect)
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
    SelectCallback onSelect
) {
    if (!CCNode::init()) return false;

    m_id = entry.id;
    m_onSelect = std::move(onSelect);
    this->setContentSize({ width, kHeight });
    this->ignoreAnchorPointForPosition(false);

    // Classic GD list stripes (same family as comment / level cells).
    GLubyte const a = selected ? 255 : 200;
    ccColor4B stripe = (index % 2 == 0)
        ? ccColor4B{ 161, 88, 44, a }
        : ccColor4B{ 191, 114, 62, a };
    if (selected) {
        stripe = ccColor4B{ 84, 140, 200, 230 };
    }

    auto* bg = CCLayerColor::create(stripe, width, kHeight);
    bg->ignoreAnchorPointForPosition(false);
    bg->setID("cloud-cell-bg"_spr);
    this->addChildAtPosition(bg, Anchor::Center);

    if (selected) {
        auto* rim = CCScale9Sprite::create("square02b_001.png");
        rim->setContentSize({ width - 2.f, kHeight - 2.f });
        rim->setColor({ 255, 255, 255 });
        rim->setOpacity(40);
        rim->setID("cloud-cell-sel"_spr);
        this->addChildAtPosition(rim, Anchor::Center);
    }

    auto* menu = CCMenu::create();
    menu->setContentSize(this->getContentSize());
    menu->ignoreAnchorPointForPosition(false);
    menu->setID("cloud-cell-menu"_spr);
    this->addChildAtPosition(menu, Anchor::Center);

    auto* hitBg = CCLayerColor::create({ 0, 0, 0, 0 }, width, kHeight);
    hitBg->ignoreAnchorPointForPosition(false);
    auto* hit = CCMenuItemSpriteExtra::create(
        hitBg, this, menu_selector(CloudConfigListCell::onClick)
    );
    hit->setContentSize({ width, kHeight });
    hit->setID("cloud-cell-hit"_spr);
    menu->addChildAtPosition(hit, Anchor::Center);

    auto* name = CCLabelBMFont::create(entry.name.c_str(), "bigFont.fnt");
    name->setScale(0.38f);
    name->setAnchorPoint({ 0.f, 0.5f });
    name->setID("cloud-cell-name"_spr);
    // Keep title from eating badges.
    float const nameMax = width * 0.55f;
    if (name->getScaledContentSize().width > nameMax && name->getContentSize().width > 0.f) {
        name->setScale(nameMax / name->getContentSize().width);
    }
    this->addChildAtPosition(name, Anchor::Left, ccp(10.f, 7.f));

    std::string sub = entry.author.empty() ? entry.id : (entry.author + " · " + entry.id);
    auto* author = CCLabelBMFont::create(sub.c_str(), "goldFont.fnt");
    author->setScale(0.30f);
    author->setAnchorPoint({ 0.f, 0.5f });
    author->setOpacity(200);
    author->setID("cloud-cell-author"_spr);
    float const subMax = width - 20.f;
    if (author->getScaledContentSize().width > subMax && author->getContentSize().width > 0.f) {
        author->setScale(subMax / author->getContentSize().width);
    }
    this->addChildAtPosition(author, Anchor::Left, ccp(10.f, -8.f));

    float badgeX = -8.f;
    auto addBadge = [&](char const* text, ccColor3B color) {
        auto* lab = CCLabelBMFont::create(text, "bigFont.fnt");
        lab->setScale(0.25f);
        lab->setColor(color);
        lab->setAnchorPoint({ 1.f, 0.5f });
        this->addChildAtPosition(lab, Anchor::Right, ccp(badgeX, 0.f));
        badgeX -= lab->getScaledContentSize().width + 8.f;
    };

    if (active) addBadge("ACTIVE", { 100, 255, 140 });
    if (updateAvailable) addBadge("UPDATE", { 255, 200, 80 });
    else if (cached) addBadge("SAVED", { 180, 210, 255 });

    return true;
}

void CloudConfigListCell::onClick(CCObject*) {
    if (m_onSelect) m_onSelect(m_id);
}
