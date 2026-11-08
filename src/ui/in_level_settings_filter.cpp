#include "in_level_settings_filter.hpp"
#include "../core/config_bundle_keys.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <cmath>
#include <cocos-ext.h>
#include <cstring>
#include <fmt/format.h>
#include <vector>

// Exhaustive key → tab table (mod.json order). Unknown keys → Count (no Assist dump).

SettingsCategoryMap::Entry const SettingsCategoryMap::kEntries[SettingsCategoryMap::kCount] = {
    {"title-general", SettingsCategory::Main},
    {"enabled", SettingsCategory::Main},
    {"safe-mode", SettingsCategory::Main},
    {"assist-mode", SettingsCategory::Main},
    {"title-window", SettingsCategory::Main},
    {"window-unit", SettingsCategory::Main},
    {"ticks-all", SettingsCategory::Main},
    {"ticks-wave", SettingsCategory::Main},
    {"show-overlay", SettingsCategory::Main},
    {"show-pause-hub", SettingsCategory::Main},
    {"show-pause-bake", SettingsCategory::Main},
    {"debug-logging", SettingsCategory::Main},
    {"perf-trace", SettingsCategory::Main},
    {"lag-probe", SettingsCategory::Main},
    {"title-scope", SettingsCategory::Scope},
    {"assist-only-practice", SettingsCategory::Scope},
    {"assist-startpos-only", SettingsCategory::Scope},
    {"assist-percent-min", SettingsCategory::Scope},
    {"assist-percent-max", SettingsCategory::Scope},
    {"title-assist-off", SettingsCategory::Scope},
    {"assist-off-1-from", SettingsCategory::Scope},
    {"assist-off-1-to", SettingsCategory::Scope},
    {"assist-off-2-from", SettingsCategory::Scope},
    {"assist-off-2-to", SettingsCategory::Scope},
    {"assist-off-3-from", SettingsCategory::Scope},
    {"assist-off-3-to", SettingsCategory::Scope},
    {"assist-off-4-from", SettingsCategory::Scope},
    {"assist-off-4-to", SettingsCategory::Scope},
    {"assist-off-5-from", SettingsCategory::Scope},
    {"assist-off-5-to", SettingsCategory::Scope},
    {"assist-off-6-from", SettingsCategory::Scope},
    {"assist-off-6-to", SettingsCategory::Scope},
    {"title-assist-actions", SettingsCategory::Assist},
    {"reset-learn-action", SettingsCategory::Assist},
    {"export-config-action", SettingsCategory::Assist},
    {"import-config-action", SettingsCategory::Assist},
    {"browse-cloud-configs-action", SettingsCategory::Assist},
    {"title-algorithm", SettingsCategory::Assist},
    {"death-slack", SettingsCategory::Assist},
    {"look-ahead-cap", SettingsCategory::Assist},
    {"assist-pick", SettingsCategory::Assist},
    {"assist-engine", SettingsCategory::Assist},
    {"cps-limit", SettingsCategory::Assist},
    {"min-click-interval-ms", SettingsCategory::Assist},
    {"assist-orbs", SettingsCategory::Assist},
    {"sim-budget-us", SettingsCategory::Assist},
    {"tick-budget", SettingsCategory::Assist},
    {"assist-press", SettingsCategory::Assist},
    {"assist-release", SettingsCategory::Assist},
    {"title-smart", SettingsCategory::Assist},
    {"smart-depth", SettingsCategory::Assist},
    {"smart-combos", SettingsCategory::Assist},
    {"smart-horizon", SettingsCategory::Assist},
    {"robust-k", SettingsCategory::Assist},
    {"robust-jitter", SettingsCategory::Assist},
    {"title-learning", SettingsCategory::Assist},
    {"auto-tune", SettingsCategory::Assist},
    {"self-bake", SettingsCategory::Assist},
    {"self-bake-normal", SettingsCategory::Assist},
    {"self-bake-refine", SettingsCategory::Assist},
    {"title-trigger-world", SettingsCategory::Assist},
    {"trigger-bake", SettingsCategory::Assist},
    {"trigger-bake-skip-dynamic", SettingsCategory::Assist},
    {"assist-verify", SettingsCategory::Assist},
    {"title-bake-actions", SettingsCategory::Bake},
    {"import-bake-replay", SettingsCategory::Bake},
    {"cycle-assist-mode", SettingsCategory::Bake},
    {"bake-map-action", SettingsCategory::Bake},
    {"brute-bake-action", SettingsCategory::Bake},
    {"cancel-bake-action", SettingsCategory::Bake},
    {"bake-radius", SettingsCategory::Bake},
    {"title-cube", SettingsCategory::Modes},
    {"cube-enable", SettingsCategory::Modes},
    {"cube-overwrite", SettingsCategory::Modes},
    {"cube-ticks", SettingsCategory::Modes},
    {"cube-cap", SettingsCategory::Modes},
    {"cube-slack", SettingsCategory::Modes},
    {"cube-latency", SettingsCategory::Modes},
    {"title-ship", SettingsCategory::Modes},
    {"ship-enable", SettingsCategory::Modes},
    {"ship-overwrite", SettingsCategory::Modes},
    {"ship-ticks", SettingsCategory::Modes},
    {"ship-cap", SettingsCategory::Modes},
    {"ship-slack", SettingsCategory::Modes},
    {"ship-latency", SettingsCategory::Modes},
    {"title-ball", SettingsCategory::Modes},
    {"ball-enable", SettingsCategory::Modes},
    {"ball-overwrite", SettingsCategory::Modes},
    {"ball-ticks", SettingsCategory::Modes},
    {"ball-cap", SettingsCategory::Modes},
    {"ball-slack", SettingsCategory::Modes},
    {"ball-latency", SettingsCategory::Modes},
    {"title-ufo", SettingsCategory::Modes},
    {"ufo-enable", SettingsCategory::Modes},
    {"ufo-overwrite", SettingsCategory::Modes},
    {"ufo-ticks", SettingsCategory::Modes},
    {"ufo-cap", SettingsCategory::Modes},
    {"ufo-slack", SettingsCategory::Modes},
    {"ufo-latency", SettingsCategory::Modes},
    {"title-wave", SettingsCategory::Modes},
    {"wave-enable", SettingsCategory::Modes},
    {"wave-overwrite", SettingsCategory::Modes},
    {"wave-ticks", SettingsCategory::Modes},
    {"wave-cap", SettingsCategory::Modes},
    {"wave-slack", SettingsCategory::Modes},
    {"wave-latency", SettingsCategory::Modes},
    {"title-robot", SettingsCategory::Modes},
    {"robot-enable", SettingsCategory::Modes},
    {"robot-overwrite", SettingsCategory::Modes},
    {"robot-ticks", SettingsCategory::Modes},
    {"robot-cap", SettingsCategory::Modes},
    {"robot-slack", SettingsCategory::Modes},
    {"robot-latency", SettingsCategory::Modes},
    {"title-spider", SettingsCategory::Modes},
    {"spider-enable", SettingsCategory::Modes},
    {"spider-overwrite", SettingsCategory::Modes},
    {"spider-ticks", SettingsCategory::Modes},
    {"spider-cap", SettingsCategory::Modes},
    {"spider-slack", SettingsCategory::Modes},
    {"spider-latency", SettingsCategory::Modes},
    {"title-swing", SettingsCategory::Modes},
    {"swing-enable", SettingsCategory::Modes},
    {"swing-overwrite", SettingsCategory::Modes},
    {"swing-ticks", SettingsCategory::Modes},
    {"swing-cap", SettingsCategory::Modes},
    {"swing-slack", SettingsCategory::Modes},
    {"swing-latency", SettingsCategory::Modes},
    {"show-trajectory", SettingsCategory::Visual},
    {"show-bake-path", SettingsCategory::Visual},
    {"save-feedback-sound", SettingsCategory::Visual},
    {"save-feedback-color", SettingsCategory::Visual},
    {"save-feedback-counter", SettingsCategory::Visual},
    {"save-icon-color", SettingsCategory::Visual},
    {"title-keys", SettingsCategory::Keys},
    {"hotkey-debug", SettingsCategory::Keys},
    {"hotkey-reset-level", SettingsCategory::Keys},
    {"hotkey-auto-tune", SettingsCategory::Keys},
    {"hotkey-assist-mode", SettingsCategory::Keys},
    {"hotkey-trajectory", SettingsCategory::Keys},
    {"title-dev", SettingsCategory::Dev},
    {"title-dev-session", SettingsCategory::Dev},
    {"dev-record-enable", SettingsCategory::Dev},
    {"dev-record-count", SettingsCategory::Dev},
    {"dev-count-empty-attempts", SettingsCategory::Dev},
    {"dev-freeze-world", SettingsCategory::Dev},
    {"dev-disable-random", SettingsCategory::Dev},
    {"title-dev-eval", SettingsCategory::Dev},
    {"dev-eval-on-death", SettingsCategory::Dev},
    {"dev-eval-grid", SettingsCategory::Dev},
    {"dev-eval-horizon", SettingsCategory::Dev},
    {"dev-eval-tail", SettingsCategory::Dev},
    {"dev-objective", SettingsCategory::Dev},
    {"dev-eval-require-deps", SettingsCategory::Dev},
    {"dev-record-cands", SettingsCategory::Dev},
    {"dev-eval-tick-budget", SettingsCategory::Dev},
    {"title-dev-overlay", SettingsCategory::Dev},
    {"dev-overlay", SettingsCategory::Dev},
    {"title-dev-actions", SettingsCategory::Dev},
    {"dev-start-record-action", SettingsCategory::Dev},
    {"dev-stop-record-action", SettingsCategory::Dev},
    {"dev-eval-last-action", SettingsCategory::Dev},
    {"dev-eval-session-action", SettingsCategory::Dev},
    {"dev-export-session-action", SettingsCategory::Dev},
    {"dev-clear-traces-action", SettingsCategory::Dev},
    {"dev-copy-report-action", SettingsCategory::Dev},
    {"title-dev-diag", SettingsCategory::Dev},
    {"dev-dump-diag-action", SettingsCategory::Dev},
};

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

void SettingsCategoryBar::onChip(geode::prelude::CCObject* sender) {
    auto* item = geode::prelude::typeinfo_cast<geode::prelude::CCMenuItem*>(sender);
    if (!item || !this->m_owner)
        return;
    int const tag = item->getTag();
    if (tag < 0 || tag >= static_cast<int>(SettingsCategory::Count))
        return;
    auto* owner = this->m_owner;
    auto const category = static_cast<SettingsCategory>(tag);
    // Defer past the menu callback so Geode touch dispatch stays stable.
    geode::prelude::Loader::get()->queueInMainThread(
        [owner = geode::prelude::Ref(owner), category] {
            if (!owner)
                return;
            owner->setCategory(category);
        });
}

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

void InLevelSettingsFilterKeeper::attach(geode::prelude::CCNode* popup) {
    if (!popup)
        return;
    if (popup->getChildByID("earlybuffer-inlevel-settings-filter"_spr))
        return;

    auto* keeper = SettingsFilterKeeperNode::create();
    if (!keeper)
        return;
    popup->addChild(keeper);
}

bool InLevelSettingsFilterKeeper::isOpen() {
    return SettingsFilterKeeperNode::findOpen() != nullptr;
}

void InLevelSettingsFilterKeeper::refreshOpen() {
    // Action button rows refresh themselves via onTick.
}

// Pull committed store values into open SettingNodeV3 drafts so Apply cannot
// overwrite a just-imported config with stale green uncommitted UI.

void InLevelSettingsFilterKeeper::reloadAfterImport() {
    auto* scene = geode::prelude::CCScene::get();
    if (!scene)
        return;
    auto* mod = geode::prelude::Mod::get();
    if (!mod)
        return;
    std::string const ourId = mod->getID();

    SettingsCategory restoreCat = SettingsCategory::Main;
    if (auto* open = SettingsFilterKeeperNode::findOpen()) {
        restoreCat = open->m_category;
    }

    geode::prelude::CCNode* popup = SettingsImportUiSync::findOurSettingsPopup(scene, ourId);
    if (!popup)
        return;

    SettingsImportUiSync::syncNodesFromStore(popup, ourId);

    int const restoreI = static_cast<int>(restoreCat);
    // Rebuild next frame: import runs from a SettingNode button callback — do not
    // destroy the popup until that stack unwinds. Rebuild also refreshes keybinds.
    geode::prelude::Loader::get()->queueInMainThread([restoreI] {
        auto* scene = geode::prelude::CCScene::get();
        if (!scene)
            return;
        auto* mod = geode::prelude::Mod::get();
        if (!mod)
            return;

        if (auto* popup = SettingsImportUiSync::findOurSettingsPopup(scene, mod->getID())) {
            popup->removeFromParentAndCleanup(true);
        }
        auto* fresh = geode::openSettingsPopup(mod, false);
        if (!fresh)
            return;
        InLevelSettingsFilterKeeper::attach(fresh);
        if (auto* keeper = SettingsFilterKeeperNode::findOpen()) {
            auto const category = static_cast<SettingsCategory>(restoreI);
            if (category >= SettingsCategory::Main && category < SettingsCategory::Count) {
                keeper->setCategory(category);
            }
        }
    });
}

void InLevelSettingsFilterKeeper::startWatchdog() {
    auto& slot = SettingsTabWatchdogState::slot();
    if (!slot) {
        slot = SettingsTabWatchdog::create();
    }
    if (!slot)
        return;
    auto* scene = geode::prelude::CCScene::get();
    if (!scene) {
        geode::prelude::Loader::get()->queueInMainThread(
            [] { InLevelSettingsFilterKeeper::startWatchdog(); });
        return;
    }
    slot->ensureOnScene();
}

auto SettingsCategoryMap::begin() -> Entry const* {
    return kEntries;
}

auto SettingsCategoryMap::end() -> Entry const* {
    return kEntries + kCount;
}

auto SettingsCategoryMap::lookup(std::string const& key) -> SettingsCategory {
    for (auto const* it = begin(); it != end(); ++it) {
        if (std::strcmp(it->key, key.c_str()) == 0)
            return it->cat;
    }
    return SettingsCategory::Count;
}

auto SettingsFilterWalk::isOurCategoryChrome(geode::prelude::CCNode* node) -> bool {
    if (!node)
        return false;
    auto const id = node->getID();
    return id == "earlybuffer-settings-category-bar"_spr ||
           id == "earlybuffer-settings-category-menu"_spr;
}

auto SettingsFilterWalk::isCategoryBarScroll(geode::prelude::ScrollLayer* scroll) -> bool {
    if (!scroll)
        return false;
    if (scroll->getID() == "earlybuffer-settings-category-scroll"_spr)
        return true;
    for (auto* count = scroll->getParent(); count; count = count->getParent()) {
        if (count->getID() == "earlybuffer-settings-category-bar"_spr)
            return true;
    }
    return false;
}

auto SettingsFilterWalk::scrollLooksLikeSettingsList(geode::prelude::ScrollLayer* scroll) -> bool {
    if (!scroll || !scroll->m_contentLayer)
        return false;
    if (isCategoryBarScroll(scroll))
        return false;
    // Real ModSettingsPopup list is tall; chip strip is ~34px.
    if (!(scroll->getContentSize().height < 80.f)) {
        return true;
    }
    return false;
}

auto SettingsFilterWalk::collect(geode::prelude::CCNode* node,
                                 std::vector<geode::prelude::SettingNodeV3*>& settings,
                                 geode::prelude::ScrollLayer*& list) -> void {
    if (!node)
        return;
    // Do not descend into our chip bar — its nested ScrollLayer would steal `list`.
    if (isOurCategoryChrome(node))
        return;

    if (auto* scroll = geode::prelude::typeinfo_cast<geode::prelude::ScrollLayer*>(node)) {
        if (scrollLooksLikeSettingsList(scroll)) {
            if (!list || scroll->getContentSize().height > list->getContentSize().height) {
                list = scroll;
            }
        }
    }
    if (auto* sett = geode::prelude::typeinfo_cast<geode::prelude::SettingNodeV3*>(node)) {
        settings.push_back(sett);
        return;
    }
    auto* children = node->getChildren();
    if (!children)
        return;
    std::vector<geode::prelude::CCNode*> kids;
    kids.reserve(static_cast<size_t>(children->count()));
    for (auto* child : geode::prelude::CCArrayExt<geode::prelude::CCNode*>(children))
        kids.push_back(child);
    for (auto* child : kids)
        collect(child, settings, list);
}

auto SettingsCategoryBar::initBar(SettingsFilterKeeperNode* owner, float width) -> bool {
    if (!geode::prelude::CCNode::init())
        return false;
    if (!owner || width < 40.f)
        return false;

    this->m_owner = owner;
    this->m_active = SettingsCategory::Main;
    this->setID("earlybuffer-settings-category-bar"_spr);
    this->setContentSize({width, kBarH});
    this->ignoreAnchorPointForPosition(false);
    this->setAnchorPoint({0.5f, 0.f});

    this->m_labels = geode::prelude::CCNode::create();
    if (!this->m_labels)
        return false;
    this->m_labels->setContentSize({width, kBarH});
    this->m_labels->setPosition({0.f, 0.f});
    this->addChild(this->m_labels, 1);

    this->m_menu = geode::prelude::CCMenu::create();
    if (!this->m_menu)
        return false;
    this->m_menu->setID("earlybuffer-settings-category-menu"_spr);
    this->m_menu->setContentSize({width, kBarH});
    this->m_menu->setPosition({0.f, 0.f});
    this->m_menu->ignoreAnchorPointForPosition(false);
    this->m_menu->setAnchorPoint({0.f, 0.f});
    this->addChild(this->m_menu, 2);

    this->rebuild(this->m_active);
    return true;
}

auto SettingsCategoryBar::setCategory(SettingsCategory category) -> void {
    if (category == this->m_active)
        return;
    this->m_active = category;
    // Never destroy menu items mid-click — only recolor labels.
    this->paintLabels(category);
}

auto SettingsCategoryBar::setMenuTouchPriority(int priority) -> void {
    if (this->m_menu)
        this->m_menu->setTouchPriority(priority);
}

auto SettingsCategoryBar::create(SettingsFilterKeeperNode* owner,
                                 float width) -> SettingsCategoryBar* {
    auto* createdObject = new SettingsCategoryBar();
    if (!createdObject || !createdObject->initBar(owner, width)) {
        CC_SAFE_DELETE(createdObject);
        return nullptr;
    }
    createdObject->autorelease();
    return createdObject;
}

auto SettingsCategoryBar::paintLabels(SettingsCategory active) -> void {
    if (!this->m_labels)
        return;
    auto* kids = this->m_labels->getChildren();
    if (!kids)
        return;
    for (unsigned index = 0; index < kids->count(); ++index) {
        auto* label = geode::prelude::typeinfo_cast<geode::prelude::CCLabelBMFont*>(
            kids->objectAtIndex(index));
        if (!label)
            continue;
        bool const isActive = static_cast<int>(active) == static_cast<int>(index);
        if (isActive) {
            label->setColor({255, 255, 255});
            label->setOpacity(255);
        } else {
            label->setColor({150, 150, 150});
            label->setOpacity(200);
        }
    }
}

auto SettingsCategoryBar::rebuild(SettingsCategory active) -> void {
    if (!this->m_menu || !this->m_labels)
        return;
    this->m_menu->removeAllChildren();
    this->m_labels->removeAllChildren();

    float const barW = this->getContentSize().width;
    int const count = static_cast<int>(SettingsCategory::Count);
    if (count <= 0)
        return;

    float const segW = (barW - kSidePad * 2.f) / static_cast<float>(count);
    if (segW < 8.f)
        return;

    this->m_menu->setContentSize({barW, kBarH});
    float x = kSidePad;
    for (int index = 0; index < count; ++index) {
        auto const category = static_cast<SettingsCategory>(index);
        bool const isActive = category == active;

        auto* label = geode::prelude::CCLabelBMFont::create(
            InLevelSettingsFilter::categoryName(category), "bigFont.fnt");
        if (label) {
            float scale = kMaxFitScale;
            float const maxW = segW - 4.f;
            if (label->getContentSize().width > 0.f) {
                float const fit = maxW / label->getContentSize().width;
                if (fit < scale)
                    scale = fit;
            }
            if (scale < kMinFitScale)
                scale = kMinFitScale;
            label->setScale(scale);
            label->setAnchorPoint({0.5f, 0.5f});
            label->setPosition({x + segW * 0.5f, kBarH * 0.5f});
            if (isActive) {
                label->setColor({255, 255, 255});
                label->setOpacity(255);
            } else {
                label->setColor({150, 150, 150});
                label->setOpacity(200);
            }
            this->m_labels->addChild(label);
        }

        // Near-invisible hit plate (opacity 0 often skips hit-tests in cocos).
        auto* hit = geode::prelude::CCScale9Sprite::create("square02b_001.png");
        if (!hit) {
            x += segW;
            continue;
        }
        hit->setOpacity(1);
        hit->setContentSize({segW, kBarH});

        auto* item =
            CCMenuItemSpriteExtra::create(hit, this, menu_selector(SettingsCategoryBar::onChip));
        if (!item) {
            x += segW;
            continue;
        }
        item->setTag(static_cast<int>(category));
        item->setContentSize({segW, kBarH});
        if (auto* img = item->getNormalImage()) {
            img->setAnchorPoint({0.5f, 0.5f});
            img->setPosition({segW * 0.5f, kBarH * 0.5f});
        }
        item->setPosition({x + segW * 0.5f, kBarH * 0.5f});
        this->m_menu->addChild(item);
        x += segW;
    }
}

auto SettingsFilterKeeperNode::setCategory(SettingsCategory category) -> void {
    auto const count = static_cast<int>(SettingsCategory::Count);
    auto index = static_cast<int>(category) % count;
    if (index < 0)
        index += count;

    this->m_category = static_cast<SettingsCategory>(index);
    if (this->m_bar)
        this->m_bar->setCategory(this->m_category);
    this->refilter(true);
    if (this->m_list)
        this->m_list->moveToTop();
}

auto SettingsFilterKeeperNode::cycle(int delta) -> void {
    this->setCategory(static_cast<SettingsCategory>(static_cast<int>(this->m_category) + delta));
}

auto SettingsFilterKeeperNode::layoutBar(geode::prelude::ScrollLayer* list,
                                         SettingsCategoryBar* bar) -> void {
    if (!list || !bar)
        return;
    constexpr float kBarH = SettingsCategoryBar::kBarH;
    auto const size = list->getContentSize();
    // Geode can restore list height every frame — keep carving the tab strip out.
    if (size.height > this->m_listFullH)
        this->m_listFullH = size.height;
    if (this->m_listFullH < kBarH + 16.f) {
        if (!this->m_loggedShortList) {
            this->m_loggedShortList = true;
            geode::log::warn(
                "EarlyBuffer settings category bar skipped: list height {:.1f} <= {:.1f}",
                this->m_listFullH, kBarH + 16.f);
        }
        return;
    }
    float const wantH = this->m_listFullH - kBarH;
    if (std::fabs(size.height - wantH) > 0.5f) {
        list->setContentSize({size.width, wantH});
    }
    bar->setPosition({list->getPositionX() + size.width * 0.5f, list->getPositionY() + wantH});
    // Beat ScrollLayer + Geode menus so tab hits actually fire.
    bar->setMenuTouchPriority(list->getTouchPriority() - 64);
    bar->setZOrder(list->getZOrder() + 20);
}

auto SettingsFilterKeeperNode::ensureBar(geode::prelude::ScrollLayer* list) -> void {
    if (!list || SettingsFilterWalk::isCategoryBarScroll(list))
        return;

    this->m_list = list;
    auto* parent = list->getParent();
    if (!parent)
        return;

    if (this->m_listFullH <= 0.f) {
        this->m_listFullH = list->getContentSize().height;
    }

    if (auto* existing = geode::prelude::typeinfo_cast<SettingsCategoryBar*>(
            parent->getChildByID("earlybuffer-settings-category-bar"_spr))) {
        this->m_bar = existing;
        this->layoutBar(list, existing);
        this->m_bar->setCategory(this->m_category);
        return;
    }

    constexpr float kBarH = SettingsCategoryBar::kBarH;
    if (this->m_listFullH <= kBarH + 16.f) {
        if (!this->m_loggedShortList) {
            this->m_loggedShortList = true;
            geode::log::warn(
                "EarlyBuffer settings category bar skipped: list height {:.1f} <= {:.1f}",
                this->m_listFullH, kBarH + 16.f);
        }
        return;
    }

    float const width = list->getContentSize().width;
    auto* bar = SettingsCategoryBar::create(this, width);
    if (!bar)
        return;

    parent->addChild(bar);
    this->m_bar = bar;
    this->layoutBar(list, bar);
    this->m_bar->setCategory(this->m_category);
}

auto SettingsFilterKeeperNode::absorbFromTree(geode::prelude::CCNode* popup) -> void {
    if (!popup)
        return;

    std::vector<geode::prelude::SettingNodeV3*> found;
    geode::prelude::ScrollLayer* list = nullptr;
    SettingsFilterWalk::collect(popup, found, list);
    if (list && SettingsFilterWalk::scrollLooksLikeSettingsList(list)) {
        // Never replace a good settings list with a shorter / chip-bar scroll.
        if (!this->m_list || SettingsFilterWalk::isCategoryBarScroll(this->m_list) ||
            list->getContentSize().height >= this->m_list->getContentSize().height) {
            this->m_list = list;
        }
    }

    for (auto* sett : found) {
        if (!sett)
            continue;
        bool known = false;
        for (auto const& ref : this->m_all) {
            if (ref.data() != sett)
                continue;
            known = true;
            break;
        }
        if (known)
            continue;
        this->m_all.push_back(sett);
    }
}

auto SettingsFilterKeeperNode::nodeKeep(geode::prelude::SettingNodeV3* sett,
                                        SettingsCategory category) -> bool {
    if (!sett)
        return false;
    if (auto setting = sett->getSetting()) {
        return InLevelSettingsFilter::keepKey(setting->getKey(), category);
    }

    // Unresolved custom ("Missing setting 'key'") — still map by key in the label
    // so Bake/Assist pages aren't empty when registration races load.
    auto* label = sett->getNameLabel();
    if (!label)
        return false;

    std::string const text = label->getString();
    auto match = [&](char const* key) {
        return text.find(key) != std::string::npos && InLevelSettingsFilter::keepKey(key, category);
    };
    return match("import-bake-replay") || match("cycle-assist-mode") || match("bake-map-action") ||
           match("brute-bake-action") || match("cancel-bake-action") ||
           match("reset-learn-action") || match("export-config-action") ||
           match("import-config-action") || match("browse-cloud-configs-action");
}

auto SettingsFilterKeeperNode::refilter(bool force) -> void {
    auto* popup = this->getParent();
    if (!popup)
        return;

    this->absorbFromTree(popup);
    this->ensureBar(this->m_list);
    if (!this->m_list || !this->m_list->m_contentLayer ||
        SettingsFilterWalk::isCategoryBarScroll(this->m_list)) {
        return;
    }
    if (this->m_all.empty())
        return;

    auto* content = this->m_list->m_contentLayer;
    size_t const childCount = content->getChildrenCount();
    if (!force && this->m_appliedCat == this->m_category && childCount == this->m_lastChildCount) {
        return;
    }

    float const yBefore = content->getPositionY();
    float const hBefore = content->getContentHeight();

    for (auto& sett : this->m_all) {
        if (!sett)
            continue;
        sett->removeFromParent();
    }

    bool bg = false;
    for (auto& sett : this->m_all) {
        if (!sett || !nodeKeep(sett, this->m_category))
            continue;
        sett->setVisible(true);
        content->addChild(sett);
        sett->setDefaultBGColor(geode::prelude::ccc4(0, 0, 0, bg ? 60 : 20));
        bg = !bg;
    }

    content->updateLayout();
    float const hAfter = content->getContentHeight();
    if (force && this->m_appliedCat != this->m_category) {
        this->m_list->moveToTop();
    } else {
        content->setPositionY(InLevelSettingsFilter::preservedContentY(yBefore, hBefore, hAfter));
    }

    this->m_appliedCat = this->m_category;
    this->m_lastChildCount = content->getChildrenCount();
}

auto SettingsFilterKeeperNode::initKeeper() -> bool {
    if (!geode::prelude::CCNode::init())
        return false;

    this->setID("earlybuffer-inlevel-settings-filter"_spr);
    this->m_changeHandle = geode::prelude::SettingNodeValueChangeEvent().listen(
        [this](std::string_view modID, std::string_view, geode::prelude::SettingNodeV3*, bool) {
            if (modID != geode::prelude::Mod::get()->getID())
                return geode::prelude::ListenerResult::Propagate;
            geode::prelude::Loader::get()->queueInMainThread([self = geode::prelude::Ref(this)] {
                if (!self)
                    return;
                self->refilter(true);
            });
            return geode::prelude::ListenerResult::Propagate;
        });
    this->schedule(schedule_selector(SettingsFilterKeeperNode::onTick), 0.05f);
    this->refilter(true);
    return true;
}

auto SettingsFilterKeeperNode::onTick(float) -> void {
    this->refilter(false);
}

auto SettingsFilterKeeperNode::create() -> SettingsFilterKeeperNode* {
    auto* createdObject = new SettingsFilterKeeperNode();
    if (!createdObject || !createdObject->initKeeper()) {
        CC_SAFE_DELETE(createdObject);
        return nullptr;
    }
    createdObject->autorelease();
    return createdObject;
}

auto SettingsFilterKeeperNode::findOpen() -> SettingsFilterKeeperNode* {
    auto* scene = geode::prelude::CCScene::get();
    if (!(!scene)) {
        return geode::prelude::typeinfo_cast<SettingsFilterKeeperNode*>(
            scene->getChildByIDRecursive("earlybuffer-inlevel-settings-filter"_spr));
    }
    return nullptr;
}

auto SettingsImportUiSync::syncNodesFromStore(geode::prelude::CCNode* root,
                                              std::string const& ourId) -> void {
    if (!root)
        return;
    std::vector<geode::prelude::SettingNodeV3*> settings;
    geode::prelude::ScrollLayer* list = nullptr;
    SettingsFilterWalk::collect(root, settings, list);
    for (auto* sett : settings) {
        if (!sett)
            continue;
        auto setting = sett->getSetting();
        if (!setting || setting->getModID() != ourId)
            continue;
        if (syncValueNode<geode::prelude::BoolSettingV3>(sett))
            continue;
        if (syncValueNode<geode::prelude::IntSettingV3>(sett))
            continue;
        if (syncValueNode<geode::prelude::FloatSettingV3>(sett))
            continue;
        if (syncValueNode<geode::prelude::StringSettingV3>(sett))
            continue;
        if (syncValueNode<geode::prelude::Color3BSettingV3>(sett))
            continue;
        if (syncValueNode<geode::prelude::Color4BSettingV3>(sett))
            continue;
        // KeybindSettingNodeV3 has no public setValue — popup rebuild covers it.
    }
}

auto SettingsImportUiSync::findOurSettingsPopup(
    geode::prelude::CCNode* scene, std::string const& ourId) -> geode::prelude::CCNode* {
    if (!scene)
        return nullptr;
    if (auto* keeper = SettingsFilterKeeperNode::findOpen()) {
        if (auto* parent = keeper->getParent())
            return parent;
    }
    for (auto* child : geode::prelude::CCArrayExt<geode::prelude::CCNode*>(scene->getChildren())) {
        auto* alert = geode::prelude::typeinfo_cast<FLAlertLayer*>(child);
        if (!alert)
            continue;
        std::vector<geode::prelude::SettingNodeV3*> settings;
        geode::prelude::ScrollLayer* list = nullptr;
        SettingsFilterWalk::collect(alert, settings, list);
        for (auto* sett : settings) {
            if (!sett)
                continue;
            auto state = sett->getSetting();
            if (state && state->getModID() == ourId)
                return alert;
        }
    }
    return nullptr;
}

auto SettingsTabWatchdog::popupIsOurSettings(geode::prelude::CCNode* root) -> bool {
    if (!root || !geode::prelude::Mod::get())
        return false;

    std::string const ourId = geode::prelude::Mod::get()->getID();
    std::vector<geode::prelude::SettingNodeV3*> settings;
    geode::prelude::ScrollLayer* list = nullptr;
    SettingsFilterWalk::collect(root, settings, list);
    for (auto* sett : settings) {
        if (!sett)
            continue;
        if (auto s = sett->getSetting()) {
            if (s->getModID() == ourId)
                return true;
        }
        auto* label = sett->getNameLabel();
        if (!label)
            continue;
        std::string const text = label->getString();
        if (text.find("import-bake-replay") != std::string::npos ||
            text.find("bake-map-action") != std::string::npos ||
            text.find("show-pause-hub") != std::string::npos) {
            return true;
        }
    }

    std::vector<geode::prelude::CCNode*> stack{root};
    while (!stack.empty()) {
        auto* count = stack.back();
        stack.pop_back();
        if (!count)
            continue;

        if (auto* lab = geode::prelude::typeinfo_cast<geode::prelude::CCLabelBMFont*>(count)) {
            std::string const text = lab->getString();
            if (text.find("EarlyBuffer") != std::string::npos &&
                text.find("Settings") != std::string::npos) {
                return true;
            }
        }
        auto* kids = count->getChildren();
        if (!kids)
            continue;
        for (auto* candidate : geode::prelude::CCArrayExt<geode::prelude::CCNode*>(kids))
            stack.push_back(candidate);
    }
    return false;
}

auto SettingsTabWatchdog::collectAlerts(geode::prelude::CCNode* node,
                                        std::vector<FLAlertLayer*>& out) -> void {
    if (!node)
        return;
    if (auto* alert = geode::prelude::typeinfo_cast<FLAlertLayer*>(node)) {
        out.push_back(alert);
    }
    auto* kids = node->getChildren();
    if (!kids)
        return;
    for (auto* child : geode::prelude::CCArrayExt<geode::prelude::CCNode*>(kids)) {
        collectAlerts(child, out);
    }
}

auto SettingsTabWatchdog::findUnattachedOurPopup() -> geode::prelude::CCNode* {
    auto* scene = geode::prelude::CCScene::get();
    if (!scene)
        return nullptr;
    if (scene->getChildByIDRecursive("earlybuffer-inlevel-settings-filter"_spr)) {
        return nullptr;
    }

    std::vector<FLAlertLayer*> alerts;
    collectAlerts(scene, alerts);
    for (auto* alert : alerts) {
        if (!alert)
            continue;
        if (alert->getChildByID("earlybuffer-inlevel-settings-filter"_spr))
            continue;
        if (!alert->getChildByIDRecursive("search-input"))
            continue;
        if (popupIsOurSettings(alert))
            return alert;
    }
    return nullptr;
}

auto SettingsTabWatchdog::ensureOnScene() -> void {
    auto* scene = geode::prelude::CCScene::get();
    if (!scene)
        return;
    if (this->getParent() == scene)
        return;

    this->retain();
    this->removeFromParentAndCleanup(false);
    scene->addChild(this);
    this->release();
}

auto SettingsTabWatchdog::onTick(float) -> void {
    this->ensureOnScene();
    auto* popup = findUnattachedOurPopup();
    if (!popup)
        return;
    InLevelSettingsFilterKeeper::attach(popup);
}

auto SettingsTabWatchdog::initWatchdog() -> bool {
    if (!geode::prelude::CCNode::init())
        return false;
    this->setID("earlybuffer-settings-tab-watchdog"_spr);
    this->schedule(schedule_selector(SettingsTabWatchdog::onTick), 0.1f);
    return true;
}

auto SettingsTabWatchdog::create() -> SettingsTabWatchdog* {
    auto* createdObject = new SettingsTabWatchdog();
    if (!createdObject || !createdObject->initWatchdog()) {
        CC_SAFE_DELETE(createdObject);
        return nullptr;
    }
    createdObject->autorelease();
    return createdObject;
}

auto SettingsTabWatchdogState::slot() -> geode::prelude::Ref<SettingsTabWatchdog>& {
    static geode::prelude::Ref<SettingsTabWatchdog> s_watchdog;
    return s_watchdog;
}
