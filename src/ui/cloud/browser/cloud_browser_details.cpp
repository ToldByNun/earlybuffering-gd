#include "cloud_browser_details.hpp"
#include "../../../cloud/catalog/cloud_config_index.hpp"
#include "../../../cloud/cloud_config_stats.hpp"
#include "../../../core/platform/platform_limits.hpp"
#include "../cloud_config_popup.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/Layout.hpp>
#include <cctype>
#include <cmath>
#include <cocos-ext.h>
#include <string>

void CloudBrowserDetails::updateRateUi(CloudConfigPopup& owner) {
    auto const* entry = owner.selectedEntry();
    bool const hasPack = entry != nullptr;
    bool const enable = hasPack && !owner.m_busyUi && !owner.m_rateBusy;
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

    if (owner.m_rateHint) {
        if (!hasPack) {
            owner.m_rateHint->setString(" ");
            owner.m_rateHint->setOpacity(0);
        } else if (owner.m_rateBusy) {
            owner.m_rateHint->setString("…");
            owner.m_rateHint->setOpacity(210);
        } else if (mine > 0) {
            owner.m_rateHint->setString("Yours");
            owner.m_rateHint->setOpacity(210);
        } else {
            owner.m_rateHint->setString("Rate");
            owner.m_rateHint->setOpacity(210);
        }
    }

    geode::prelude::ccColor3B const colOn = {255, 210, 70};
    geode::prelude::ccColor3B const colOff = {150, 150, 150};

    for (int index = 0; index < 5; ++index) {
        auto* button = owner.m_rateBtns[index];
        if (!button)
            continue;
        button->setVisible(hasPack);
        button->setEnabled(enable);

        bool const on = filled > 0 && (index < filled);
        geode::prelude::ccColor3B const col = on ? colOn : colOff;
        GLubyte const op = !hasPack ? 0 : (enable ? 255 : 140);

        if (auto* spr = owner.m_rateStarSpr[index]) {
            spr->setColor(col);
            spr->setOpacity(op);
        } else {
            button->setColor(col);
            button->setOpacity(op);
        }
    }
    if (owner.m_rateMenu)
        owner.m_rateMenu->setVisible(hasPack);
}

void CloudBrowserDetails::updateDetail(CloudConfigPopup& owner) {
    auto const* entry = owner.selectedEntry();
    if (!entry) {
        if (owner.m_detailTitle)
            owner.m_detailTitle->setString("Select a pack");
        if (owner.m_detailMeta)
            owner.m_detailMeta->setString(" ");
        if (owner.m_detailBody) {
            owner.m_detailBody->setText(
                "Pick a pack from the list.\nShare… sets name & description.");
        }
        if (owner.m_detailFoot)
            owner.m_detailFoot->setString(" ");
        if (owner.m_installBtn)
            owner.m_installBtn->setEnabled(false);
        owner.updateRateUi();
        return;
    }

    if (owner.m_detailTitle) {
        auto title = CloudConfigPopupImplementation::truncate(entry->name, 22);
        owner.m_detailTitle->setString(title.c_str());
        float const maxW = owner.m_detailWidth - 14.f;
        if (owner.m_detailTitle->getScaledContentSize().width > maxW &&
            owner.m_detailTitle->getContentSize().width > 0.f) {
            owner.m_detailTitle->setScale(maxW / owner.m_detailTitle->getContentSize().width);
        } else {
            owner.m_detailTitle->setScale(0.34f);
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
    if (owner.m_detailMeta) {
        owner.m_detailMeta->setString(
            CloudConfigPopupImplementation::truncate(
                fmt::format("by {} · {}", entry->author.empty() ? "?" : entry->author, tags),
                tight ? 32 : 42)
                .c_str());
    }

    bool const verOk = CloudConfigIndex::meetsMinModVersion(*entry, owner.liveModVersion());
    if (owner.m_detailBody) {
        owner.m_detailBody->setText(entry->description.empty() ? "(no description)"
                                                               : entry->description);
    }
    if (owner.m_detailFoot) {
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
            verOk ? (owner.isCached(entry->id) ? "Ready to install." : "Downloads on Install.")
                  : "Needs newer EarlyBuffer.";
        owner.m_detailFoot->setString(
            fmt::format("{}\n{}", CloudConfigPopupImplementation::truncate(line1, tight ? 36 : 48),
                        line2)
                .c_str());
        float const footMax = owner.m_detailWidth - 12.f;
        float const baseScale = tight ? 0.30f : 0.34f;
        owner.m_detailFoot->setScale(baseScale);
        if (owner.m_detailFoot->getContentSize().width > 0.f &&
            owner.m_detailFoot->getScaledContentSize().width > footMax) {
            owner.m_detailFoot->setScale(footMax / owner.m_detailFoot->getContentSize().width);
        }
    }
    if (owner.m_installBtn)
        owner.m_installBtn->setEnabled(verOk && !owner.m_busyUi);
    owner.updateRateUi();
}
