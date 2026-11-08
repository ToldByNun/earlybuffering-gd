#include "in_level_settings_filter.hpp"
#include "../../core/bundle/config_bundle_keys.hpp"
#include "categories/settings_category_bar.hpp"
#include "categories/settings_category_map.hpp"
#include "filtering/settings_filter_keeper_node.hpp"
#include "filtering/settings_filter_walk.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <cocos-ext.h>
#include <cstring>
#include <fmt/format.h>
#include <string>
#include <vector>

// Exhaustive key → tab table (mod.json order). Unknown keys → Count (no Assist dump).

char const* InLevelSettingsFilter::categoryName(SettingsCategory category) {
    switch (category) {
    case SettingsCategory::Main:
        return "Main";
    case SettingsCategory::Scope:
        return "Scope";
    case SettingsCategory::Assist:
        return "Assist";
    case SettingsCategory::Bake:
        return "Bake";
    case SettingsCategory::Modes:
        return "Modes";
    case SettingsCategory::Visual:
        return "Visual";
    case SettingsCategory::Keys:
        return "Keys";
    case SettingsCategory::Dev:
        return "Dev";
    case SettingsCategory::Count:
        break;
    }
    return "?";
}

SettingsCategory InLevelSettingsFilter::categoryForKey(std::string const& key) {
    return SettingsCategoryMap::lookup(key);
}

bool InLevelSettingsFilter::keepKey(std::string const& key, SettingsCategory category) {
    return categoryForKey(key) == category;
}

bool InLevelSettingsFilter::keepKey(std::string const& key) {
    auto const candidate = categoryForKey(key);
    return candidate == SettingsCategory::Main || candidate == SettingsCategory::Scope ||
           candidate == SettingsCategory::Assist || candidate == SettingsCategory::Visual ||
           candidate == SettingsCategory::Bake;
}

float InLevelSettingsFilter::preservedContentY(float posBefore, float heightBefore,
                                               float heightAfter) {
    return posBefore + (heightBefore - heightAfter);
}

class SettingsFilterKeeperNode;

// Text-only tab strip: selected = white, idle = grey. No chrome / fills.
// Invisible CCMenu hit targets (opacity 1) — Geode ScrollLayer was eating bar touches.

void InLevelSettingsFilter::apply(geode::prelude::CCNode* popup, SettingsCategory category) {
    if (!popup)
        return;
    std::vector<geode::prelude::SettingNodeV3*> settings;
    geode::prelude::ScrollLayer* list = nullptr;
    SettingsFilterWalk::collect(popup, settings, list);
    if (!list || !list->m_contentLayer || settings.empty())
        return;

    auto* content = list->m_contentLayer;
    float const yBefore = content->getPositionY();
    float const hBefore = content->getContentHeight();

    for (auto* sett : settings) {
        if (!sett)
            continue;
        bool keep = false;
        if (auto setting = sett->getSetting()) {
            keep = keepKey(setting->getKey(), category);
        }
        if (!keep)
            sett->removeFromParent();
        else
            sett->setVisible(true);
    }
    content->updateLayout();
    content->setPositionY(preservedContentY(yBefore, hBefore, content->getContentHeight()));
}

bool InLevelSettingsFilter::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    expect(static_cast<int>(SettingsCategory::Count) == 8, "cat-count-8");
    expect(std::strcmp(categoryName(SettingsCategory::Main), "Main") == 0, "cat-name-main");
    expect(std::strcmp(categoryName(SettingsCategory::Scope), "Scope") == 0, "cat-name-scope");
    expect(std::strcmp(categoryName(SettingsCategory::Assist), "Assist") == 0, "cat-name-assist");
    expect(std::strcmp(categoryName(SettingsCategory::Bake), "Bake") == 0, "cat-name-bake");
    expect(std::strcmp(categoryName(SettingsCategory::Modes), "Modes") == 0, "cat-name-modes");
    expect(std::strcmp(categoryName(SettingsCategory::Visual), "Visual") == 0, "cat-name-visual");
    expect(std::strcmp(categoryName(SettingsCategory::Keys), "Keys") == 0, "cat-name-keys");
    expect(std::strcmp(categoryName(SettingsCategory::Dev), "Dev") == 0, "cat-name-dev");
    expect(std::strcmp(categoryName(SettingsCategory::Count), "?") == 0, "cat-name-count");

    // Spot checks for the new IA (no Ingame; no Assist catch-all).
    expect(categoryForKey("enabled") == SettingsCategory::Main, "cat-enabled-main");
    expect(categoryForKey("assist-mode") == SettingsCategory::Main, "cat-assist-mode-main");
    expect(categoryForKey("title-window") == SettingsCategory::Main, "cat-title-window-main");
    expect(categoryForKey("window-unit") == SettingsCategory::Main, "cat-window-main");
    expect(categoryForKey("ticks-all") == SettingsCategory::Main, "cat-ticks-all-main");
    expect(categoryForKey("ticks-wave") == SettingsCategory::Main, "cat-ticks-wave-main");
    expect(categoryForKey("lag-probe") == SettingsCategory::Main, "cat-lag-main");
    expect(categoryForKey("show-pause-hub") == SettingsCategory::Main, "cat-hub-main");
    expect(categoryForKey("show-overlay") == SettingsCategory::Main, "cat-overlay-main");
    expect(categoryForKey("title-scope") == SettingsCategory::Scope, "cat-title-scope");
    expect(categoryForKey("assist-only-practice") == SettingsCategory::Scope, "cat-practice-scope");
    expect(categoryForKey("assist-off-1-from") == SettingsCategory::Scope, "cat-off-scope");
    expect(categoryForKey("import-bake-replay") == SettingsCategory::Bake, "cat-import-bake");
    expect(categoryForKey("bake-radius") == SettingsCategory::Bake, "cat-bake-radius");
    expect(categoryForKey("reset-learn-action") == SettingsCategory::Assist, "cat-reset-learn");
    expect(categoryForKey("assist-pick") == SettingsCategory::Assist, "cat-assist-pick");
    expect(categoryForKey("assist-engine") == SettingsCategory::Assist, "cat-assist-engine");
    expect(categoryForKey("show-trajectory") == SettingsCategory::Visual, "cat-traj-visual");
    expect(categoryForKey("show-bake-path") == SettingsCategory::Visual, "cat-bake-path-visual");
    expect(categoryForKey("cube-latency") == SettingsCategory::Modes, "cat-cube-modes");
    expect(categoryForKey("hotkey-debug") == SettingsCategory::Keys, "cat-hotkey-keys");
    expect(categoryForKey("preset") == SettingsCategory::Count, "cat-preset-gone");
    expect(categoryForKey("stage-index") == SettingsCategory::Count, "cat-stage-gone");
    expect(categoryForKey("hotkey-cycle") == SettingsCategory::Count, "cat-cycle-hotkey-gone");
    expect(categoryForKey("cycle-preset-action") == SettingsCategory::Count,
           "cat-cycle-action-gone");
    expect(categoryForKey("title-dev") == SettingsCategory::Dev, "cat-title-dev");
    expect(categoryForKey("dev-start-record-action") == SettingsCategory::Dev, "cat-dev-start");

    // Unknown keys must not dump onto Assist.
    expect(categoryForKey("__eb-missing-key__") == SettingsCategory::Count, "unknown-is-count");
    expect(!keepKey("__eb-missing-key__", SettingsCategory::Assist), "unknown-not-assist");

    expect(keepKey("dev-record-enable", SettingsCategory::Dev), "keep-dev-on-dev");
    expect(!keepKey("dev-record-enable", SettingsCategory::Assist), "drop-dev-on-assist");
    expect(!keepKey("dev-record-enable"), "legacy-drop-dev");
    expect(keepKey("title-scope"), "legacy-keep-scope");
    expect(keepKey("import-bake-replay", SettingsCategory::Bake), "keep-import-on-bake");
    expect(!keepKey("import-bake-replay", SettingsCategory::Main), "drop-import-on-main");

    // Every table entry maps to a real tab (not Count).
    int mapped = 0;
    for (auto const* it = SettingsCategoryMap::begin(); it != SettingsCategoryMap::end(); ++it) {
        auto const candidate = categoryForKey(it->key);
        expect(candidate == it->cat, it->key);
        expect(candidate != SettingsCategory::Count, it->key);
        ++mapped;
    }
    expect(mapped == SettingsCategoryMap::kCount, "map-count-165");

    // Every portable ConfigBundle key must be in the category map.
    for (auto const* it = ConfigBundleKeys::begin(); it != ConfigBundleKeys::end(); ++it) {
        auto const candidate = categoryForKey(it->key);
        expect(candidate != SettingsCategory::Count, it->key);
    }
    expect(ConfigBundleKeys::count() > 0, "config-keys-nonempty");

    expect(preservedContentY(100.f, 800.f, 400.f) == 500.f, "scroll-shrink");
    expect(preservedContentY(0.f, 100.f, 100.f) == 0.f, "scroll-same");
    expect(SettingsCategoryBar::kBarH == 32.f, "chip-bar-height");
    expect(SettingsCategoryBar::kMinFitScale > 0.f, "chip-min-fit");
    expect(SettingsFilterWalk::isOurCategoryChrome(nullptr) == false, "chrome-null");
    expect(SettingsFilterWalk::isCategoryBarScroll(nullptr) == false, "cat-scroll-null");
    expect(SettingsFilterWalk::scrollLooksLikeSettingsList(nullptr) == false, "list-null");

    if (fails == 0)
        report = "InLevelSettingsFilter selfCheck OK";
    else
        report = fmt::format("InLevelSettingsFilter {} fails: {}", fails, report);
    return fails == 0;
}

// Pull committed store values into open SettingNodeV3 drafts so Apply cannot
// overwrite a just-imported config with stale green uncommitted UI.
