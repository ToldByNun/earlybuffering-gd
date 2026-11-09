#include "cloud_browser_list.hpp"
#include "../../../cloud/catalog/cloud_config_index.hpp"
#include "../../../cloud/catalog/cloud_config_service.hpp"
#include "../../../cloud/cloud_config_stats.hpp"
#include "../../../core/platform/platform_limits.hpp"
#include "../cloud_config_list_cell.hpp"
#include "../cloud_config_popup.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/Layout.hpp>
#include <algorithm>
#include <cctype>
#include <cocos-ext.h>
#include <string>
#include <vector>

bool EbCloudBrowserList::packMatchesSearch(EbCloudConfigPopup const& owner,
                                         EbCloudConfigIndex::PackEntry const& e) {
    auto const queryOffset = EbCloudConfigPopupImplementation::toLower(owner.m_searchQuery);
    if (queryOffset.empty())
        return true;
    if (EbCloudConfigPopupImplementation::containsInsensitive(e.name, queryOffset))
        return true;
    if (EbCloudConfigPopupImplementation::containsInsensitive(e.author, queryOffset))
        return true;
    if (EbCloudConfigPopupImplementation::containsInsensitive(e.id, queryOffset))
        return true;
    if (EbCloudConfigPopupImplementation::containsInsensitive(e.description, queryOffset))
        return true;
    for (auto const& t : e.tags) {
        if (EbCloudConfigPopupImplementation::containsInsensitive(t, queryOffset))
            return true;
    }
    return false;
}

char const* EbCloudBrowserList::sortModeLabel(EbCloudConfigPopup const& owner) {
    bool const tight = EbPlatformLimits::isConstrainedDevice();
    switch (owner.m_sortMode) {
    case EbCloudConfigPopup::SortMode::Downloads:
        return tight ? "DLs" : "Downloads";
    case EbCloudConfigPopup::SortMode::Rating:
        return tight ? "Rate" : "Rating";
    case EbCloudConfigPopup::SortMode::Popular:
        return tight ? "Pop" : "Popular";
    case EbCloudConfigPopup::SortMode::Name:
        return "Name";
    case EbCloudConfigPopup::SortMode::Default:
    default:
        return tight ? "Def" : "Default";
    }
}

void EbCloudBrowserList::updateSortLabel(EbCloudConfigPopup& owner) {
    if (!owner.m_sortLabel)
        return;
    owner.m_sortLabel->setString(fmt::format("Sort: {}", owner.sortModeLabel()).c_str());
}

void EbCloudBrowserList::onSortCycle(EbCloudConfigPopup& owner, geode::prelude::CCObject*) {
    int next = static_cast<int>(owner.m_sortMode) + 1;
    if (next >= static_cast<int>(EbCloudConfigPopup::SortMode::Count))
        next = 0;
    owner.m_sortMode = static_cast<EbCloudConfigPopup::SortMode>(next);
    owner.updateSortLabel();
    owner.rebuildList();
}

void EbCloudBrowserList::applySort(EbCloudConfigPopup const& owner,
                                 std::vector<EbCloudConfigIndex::PackEntry const*>& rows) {
    if (rows.size() < 2)
        return;
    if (owner.m_sortMode == EbCloudConfigPopup::SortMode::Default)
        return; // catalog order

    auto statsOf = [](EbCloudConfigIndex::PackEntry const* valuePointer) {
        return EbCloudConfigStats::get().statsFor(valuePointer->id);
    };

    std::stable_sort(rows.begin(), rows.end(),
                     [&](EbCloudConfigIndex::PackEntry const* firstValue,
                         EbCloudConfigIndex::PackEntry const* secondValue) {
                         if (!firstValue || !secondValue)
                             return firstValue != nullptr;
                         auto const sa = statsOf(firstValue);
                         auto const sb = statsOf(secondValue);
                         switch (owner.m_sortMode) {
                         case EbCloudConfigPopup::SortMode::Downloads:
                             if (sa.downloads != sb.downloads)
                                 return sa.downloads > sb.downloads;
                             break;
                         case EbCloudConfigPopup::SortMode::Rating: {
                             float const ra = EbCloudConfigStats::averageRating(sa);
                             float const rb = EbCloudConfigStats::averageRating(sb);
                             if (ra != rb)
                                 return ra > rb;
                             if (sa.ratingCount != sb.ratingCount)
                                 return sa.ratingCount > sb.ratingCount;
                             break;
                         }
                         case EbCloudConfigPopup::SortMode::Popular: {
                             float const pa = EbCloudConfigStats::popularityScore(sa);
                             float const pb = EbCloudConfigStats::popularityScore(sb);
                             if (pa != pb)
                                 return pa > pb;
                             break;
                         }
                         case EbCloudConfigPopup::SortMode::Name: {
                             auto const na =
                                 EbCloudConfigPopupImplementation::toLower(firstValue->name);
                             auto const nb =
                                 EbCloudConfigPopupImplementation::toLower(secondValue->name);
                             if (na != nb)
                                 return na < nb;
                             break;
                         }
                         case EbCloudConfigPopup::SortMode::Default:
                         default:
                             break;
                         }
                         return firstValue->id < secondValue->id;
                     });
}

void EbCloudBrowserList::rebuildList(EbCloudConfigPopup& owner) {
    if (!owner.m_listScroll || !owner.m_listScroll->m_contentLayer)
        return;
    auto* content = owner.m_listScroll->m_contentLayer;
    content->removeAllChildren();

    float const width = owner.m_listScroll->getContentWidth();
    auto const& catalog = EbCloudConfigService::get().catalog();

    std::vector<EbCloudConfigIndex::PackEntry const*> rows;
    rows.reserve(catalog.packs.size());
    for (auto const& p : catalog.packs) {
        if (owner.packMatchesSearch(p))
            rows.push_back(&p);
    }
    owner.applySort(rows);

    content->setLayout(geode::prelude::ColumnLayout::create()
                           ->setGap(2.f)
                           ->setAxisReverse(true)
                           ->setAxisAlignment(geode::prelude::AxisAlignment::End)
                           ->setAutoGrowAxis(owner.m_listScroll->getContentHeight()));

    geode::prelude::Ref<EbCloudConfigPopup> self = (&owner);
    int index = 0;
    for (auto const* text : rows) {
        auto* cell = EbCloudConfigListCell::create(
            *text, width, index++, text->id == owner.m_selectedId, owner.isActive(text->id),
            owner.isCached(text->id), owner.hasUpdate(*text), owner.statsBadgeFor(text->id),
            [self](std::string const& id) {
                if (self)
                    self->selectPack(id);
            });
        if (cell)
            content->addChild(cell);
    }

    if (rows.empty()) {
        auto* emptyNode = geode::prelude::CCNode::create();
        emptyNode->setContentSize({width, owner.m_listScroll->getContentHeight()});
        auto* empty = geode::prelude::CCLabelBMFont::create(
            owner.m_searchQuery.empty() ? "No packs yet" : "No matches", "bigFont.fnt");
        empty->setScale(0.32f);
        empty->setOpacity(160);
        emptyNode->addChildAtPosition(empty, geode::Anchor::Center);
        content->addChild(emptyNode);
    }

    content->updateLayout();
    owner.m_listScroll->scrollToTop();
}
