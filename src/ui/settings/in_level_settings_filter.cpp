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

char const* EbInLevelSettingsFilter::categoryName(SettingsCategory category) {
    switch (category) {
    case SettingsCategory::Main:
        return "Main";
    case SettingsCategory::Assist:
        return "Assist";
    case SettingsCategory::Learn:
        return "Learn";
    case SettingsCategory::Bake:
        return "Bake";
    case SettingsCategory::Modes:
        return "Modes";
    case SettingsCategory::Visual:
        return "Visual";
    case SettingsCategory::UI:
        return "UI";
    case SettingsCategory::Keys:
        return "Keys";
    case SettingsCategory::Dev:
        return "Dev";
    case SettingsCategory::Count:
        break;
    }
    return "?";
}

SettingsCategory EbInLevelSettingsFilter::categoryForKey(std::string const& key) {
    return EbSettingsCategoryMap::lookup(key);
}

bool EbInLevelSettingsFilter::keepKey(std::string const& key, SettingsCategory category) {
    return categoryForKey(key) == category;
}

bool EbInLevelSettingsFilter::keepKey(std::string const& key) {
    auto const candidate = categoryForKey(key);
    return candidate == SettingsCategory::Main || candidate == SettingsCategory::Assist ||
           candidate == SettingsCategory::Learn || candidate == SettingsCategory::Visual ||
           candidate == SettingsCategory::Bake || candidate == SettingsCategory::UI;
}

float EbInLevelSettingsFilter::preservedContentY(float posBefore, float heightBefore,
                                                 float heightAfter) {
    return posBefore + (heightBefore - heightAfter);
}

void EbInLevelSettingsFilter::apply(geode::prelude::CCNode* popup, SettingsCategory category) {
    if (!popup)
        return;
    std::vector<geode::prelude::SettingNodeV3*> settings;
    geode::prelude::ScrollLayer* list = nullptr;
    EbSettingsFilterWalk::collect(popup, settings, list);
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

bool EbInLevelSettingsFilter::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    expect(static_cast<int>(SettingsCategory::Count) == 9, "cat-count-9");
    expect(std::strcmp(categoryName(SettingsCategory::Main), "Main") == 0, "cat-name-main");
    expect(std::strcmp(categoryName(SettingsCategory::Assist), "Assist") == 0, "cat-name-assist");
    expect(std::strcmp(categoryName(SettingsCategory::Learn), "Learn") == 0, "cat-name-learn");
    expect(std::strcmp(categoryName(SettingsCategory::Bake), "Bake") == 0, "cat-name-bake");
    expect(std::strcmp(categoryName(SettingsCategory::Modes), "Modes") == 0, "cat-name-modes");
    expect(std::strcmp(categoryName(SettingsCategory::Visual), "Visual") == 0, "cat-name-visual");
    expect(std::strcmp(categoryName(SettingsCategory::UI), "UI") == 0, "cat-name-ui");
    expect(std::strcmp(categoryName(SettingsCategory::Keys), "Keys") == 0, "cat-name-keys");
    expect(std::strcmp(categoryName(SettingsCategory::Dev), "Dev") == 0, "cat-name-dev");
    expect(std::strcmp(categoryName(SettingsCategory::Count), "?") == 0, "cat-name-count");

    expect(categoryForKey("enabled") == SettingsCategory::Main, "cat-enabled-main");
    expect(categoryForKey("assist-mode") == SettingsCategory::Main, "cat-assist-mode-main");
    expect(categoryForKey("title-window") == SettingsCategory::Main, "cat-title-window-main");
    expect(categoryForKey("window-unit") == SettingsCategory::Main, "cat-window-main");
    expect(categoryForKey("ticks-all") == SettingsCategory::Main, "cat-ticks-all-main");
    expect(categoryForKey("ticks-wave") == SettingsCategory::Main, "cat-ticks-wave-main");
    expect(categoryForKey("smart-depth") == SettingsCategory::Main, "cat-smart-main");
    expect(categoryForKey("adaptive-plan") == SettingsCategory::Main, "cat-adaptive-main");
    expect(categoryForKey("lag-probe") == SettingsCategory::UI, "cat-lag-ui");
    expect(categoryForKey("show-pause-hub") == SettingsCategory::UI, "cat-hub-ui");
    expect(categoryForKey("desktop-classic-settings") == SettingsCategory::UI, "cat-classic-ui");
    expect(categoryForKey("show-overlay") == SettingsCategory::UI, "cat-overlay-ui");
    expect(categoryForKey("title-ui") == SettingsCategory::UI, "cat-title-ui");
    expect(categoryForKey("title-scope") == SettingsCategory::Assist, "cat-title-scope-assist");
    expect(categoryForKey("assist-only-practice") == SettingsCategory::Assist, "cat-practice-assist");
    expect(categoryForKey("assist-off-1-from") == SettingsCategory::Assist, "cat-off-assist");
    expect(categoryForKey("import-bake-replay") == SettingsCategory::Bake, "cat-import-bake");
    expect(categoryForKey("bake-radius") == SettingsCategory::Bake, "cat-bake-radius");
    expect(categoryForKey("trigger-bake") == SettingsCategory::Bake, "cat-trigger-bake");
    expect(categoryForKey("reset-learn-action") == SettingsCategory::Learn, "cat-reset-learn");
    expect(categoryForKey("auto-tune") == SettingsCategory::Learn, "cat-auto-tune-learn");
    expect(categoryForKey("assist-pick") == SettingsCategory::Assist, "cat-assist-pick");
    expect(categoryForKey("assist-engine") == SettingsCategory::Assist, "cat-assist-engine");
    expect(categoryForKey("show-trajectory") == SettingsCategory::Visual, "cat-traj-visual");
    expect(categoryForKey("show-bake-path") == SettingsCategory::Visual, "cat-bake-path-visual");
    expect(categoryForKey("cube-latency") == SettingsCategory::Modes, "cat-cube-modes");
    expect(categoryForKey("hotkey-debug") == SettingsCategory::Keys, "cat-hotkey-keys");
    expect(categoryForKey("hotkey-desktop-menu") == SettingsCategory::Keys, "cat-hotkey-menu");
    expect(categoryForKey("preset") == SettingsCategory::Count, "cat-preset-gone");
    expect(categoryForKey("stage-index") == SettingsCategory::Count, "cat-stage-gone");
    expect(categoryForKey("hotkey-cycle") == SettingsCategory::Count, "cat-cycle-hotkey-gone");
    expect(categoryForKey("cycle-preset-action") == SettingsCategory::Count,
           "cat-cycle-action-gone");
    expect(categoryForKey("title-dev") == SettingsCategory::Dev, "cat-title-dev");
    expect(categoryForKey("dev-start-record-action") == SettingsCategory::Dev, "cat-dev-start");

    expect(categoryForKey("__eb-missing-key__") == SettingsCategory::Count, "unknown-is-count");
    expect(!keepKey("__eb-missing-key__", SettingsCategory::Assist), "unknown-not-assist");

    expect(keepKey("dev-record-enable", SettingsCategory::Dev), "keep-dev-on-dev");
    expect(!keepKey("dev-record-enable", SettingsCategory::Assist), "drop-dev-on-assist");
    expect(!keepKey("dev-record-enable"), "legacy-drop-dev");
    expect(keepKey("title-scope"), "legacy-keep-scope");
    expect(keepKey("smart-depth"), "legacy-keep-smart");
    expect(keepKey("title-ui"), "legacy-keep-ui");
    expect(keepKey("import-bake-replay", SettingsCategory::Bake), "keep-import-on-bake");
    expect(!keepKey("import-bake-replay", SettingsCategory::Main), "drop-import-on-main");

    int mapped = 0;
    for (auto const* it = EbSettingsCategoryMap::begin(); it != EbSettingsCategoryMap::end(); ++it) {
        auto const candidate = categoryForKey(it->key);
        expect(candidate == it->cat, it->key);
        expect(candidate != SettingsCategory::Count, it->key);
        ++mapped;
    }
    expect(mapped == EbSettingsCategoryMap::kCount, "map-count-175");

    for (auto const* it = EbConfigBundleKeys::begin(); it != EbConfigBundleKeys::end(); ++it) {
        auto const candidate = categoryForKey(it->key);
        expect(candidate != SettingsCategory::Count, it->key);
    }
    expect(EbConfigBundleKeys::count() > 0, "config-keys-nonempty");

    expect(preservedContentY(100.f, 800.f, 400.f) == 500.f, "scroll-shrink");
    expect(preservedContentY(0.f, 100.f, 100.f) == 0.f, "scroll-same");
    expect(EbSettingsCategoryBar::kBarH == 32.f, "chip-bar-height");
    expect(EbSettingsCategoryBar::kMinFitScale > 0.f, "chip-min-fit");
    expect(EbSettingsFilterWalk::isOurCategoryChrome(nullptr) == false, "chrome-null");
    expect(EbSettingsFilterWalk::isCategoryBarScroll(nullptr) == false, "cat-scroll-null");
    expect(EbSettingsFilterWalk::scrollLooksLikeSettingsList(nullptr) == false, "list-null");

    if (fails == 0)
        report = "InLevelSettingsFilter selfCheck OK";
    else
        report = fmt::format("InLevelSettingsFilter {} fails: {}", fails, report);
    return fails == 0;
}
