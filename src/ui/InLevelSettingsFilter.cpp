#include "InLevelSettingsFilter.hpp"

#include "../core/ConfigBundleKeys.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <fmt/format.h>
#include <cstring>
#include <vector>

using namespace geode::prelude;

// Exhaustive key → tab table (mod.json order). Unknown keys → Count (no Assist dump).
class SettingsCategoryMap {
public:
    struct Entry {
        char const* key;
        SettingsCategory cat;
    };

    static constexpr int kCount = 171;

    static Entry const* begin() { return kEntries; }
    static Entry const* end() { return kEntries + kCount; }

    static SettingsCategory lookup(std::string const& key) {
        for (auto const* it = begin(); it != end(); ++it) {
            if (std::strcmp(it->key, key.c_str()) == 0) return it->cat;
        }
        return SettingsCategory::Count;
    }

private:
    static Entry const kEntries[kCount];

    SettingsCategoryMap() = delete;
};

SettingsCategoryMap::Entry const SettingsCategoryMap::kEntries[SettingsCategoryMap::kCount] = {
    { "title-general", SettingsCategory::Main },
    { "enabled", SettingsCategory::Main },
    { "preset", SettingsCategory::Main },
    { "safe-mode", SettingsCategory::Main },
    { "show-overlay", SettingsCategory::Main },
    { "debug-logging", SettingsCategory::Main },
    { "perf-trace", SettingsCategory::Main },
    { "lag-probe", SettingsCategory::Main },
    { "show-trajectory", SettingsCategory::Visual },
    { "show-pause-bake", SettingsCategory::Main },
    { "show-pause-assist", SettingsCategory::Main },
    { "show-pause-hub", SettingsCategory::Main },
    { "title-bake-actions", SettingsCategory::Bake },
    { "import-bake-replay", SettingsCategory::Bake },
    { "cycle-assist-mode", SettingsCategory::Bake },
    { "bake-map-action", SettingsCategory::Bake },
    { "brute-bake-action", SettingsCategory::Bake },
    { "cancel-bake-action", SettingsCategory::Bake },
    { "title-assist-actions", SettingsCategory::Assist },
    { "cycle-preset-action", SettingsCategory::Assist },
    { "reset-learn-action", SettingsCategory::Assist },
    { "export-config-action", SettingsCategory::Assist },
    { "import-config-action", SettingsCategory::Assist },
    { "save-feedback-sound", SettingsCategory::Visual },
    { "save-feedback-color", SettingsCategory::Visual },
    { "save-feedback-counter", SettingsCategory::Visual },
    { "save-icon-color", SettingsCategory::Visual },
    { "title-scope", SettingsCategory::Scope },
    { "assist-only-practice", SettingsCategory::Scope },
    { "assist-startpos-only", SettingsCategory::Scope },
    { "assist-percent-min", SettingsCategory::Scope },
    { "assist-percent-max", SettingsCategory::Scope },
    { "title-assist-off", SettingsCategory::Scope },
    { "assist-off-1-from", SettingsCategory::Scope },
    { "assist-off-1-to", SettingsCategory::Scope },
    { "assist-off-2-from", SettingsCategory::Scope },
    { "assist-off-2-to", SettingsCategory::Scope },
    { "assist-off-3-from", SettingsCategory::Scope },
    { "assist-off-3-to", SettingsCategory::Scope },
    { "assist-off-4-from", SettingsCategory::Scope },
    { "assist-off-4-to", SettingsCategory::Scope },
    { "assist-off-5-from", SettingsCategory::Scope },
    { "assist-off-5-to", SettingsCategory::Scope },
    { "assist-off-6-from", SettingsCategory::Scope },
    { "assist-off-6-to", SettingsCategory::Scope },
    { "title-window", SettingsCategory::Assist },
    { "window-unit", SettingsCategory::Assist },
    { "ticks-all", SettingsCategory::Assist },
    { "ticks-wave", SettingsCategory::Assist },
    { "title-algorithm", SettingsCategory::Assist },
    { "death-slack", SettingsCategory::Assist },
    { "look-ahead-cap", SettingsCategory::Assist },
    { "assist-mode", SettingsCategory::Main },
    { "assist-pick", SettingsCategory::Assist },
    { "bake-radius", SettingsCategory::Bake },
    { "cps-limit", SettingsCategory::Assist },
    { "min-click-interval-ms", SettingsCategory::Assist },
    { "assist-orbs", SettingsCategory::Assist },
    { "sim-budget-us", SettingsCategory::Assist },
    { "tick-budget", SettingsCategory::Assist },
    { "assist-press", SettingsCategory::Assist },
    { "assist-release", SettingsCategory::Assist },
    { "title-smart", SettingsCategory::Assist },
    { "smart-depth", SettingsCategory::Assist },
    { "smart-combos", SettingsCategory::Assist },
    { "smart-horizon", SettingsCategory::Assist },
    { "robust-k", SettingsCategory::Assist },
    { "robust-jitter", SettingsCategory::Assist },
    { "title-learning", SettingsCategory::Assist },
    { "auto-tune", SettingsCategory::Assist },
    { "self-bake", SettingsCategory::Assist },
    { "self-bake-normal", SettingsCategory::Assist },
    { "self-bake-refine", SettingsCategory::Assist },
    { "title-trigger-world", SettingsCategory::Assist },
    { "trigger-bake", SettingsCategory::Assist },
    { "trigger-bake-skip-dynamic", SettingsCategory::Assist },
    { "assist-verify", SettingsCategory::Assist },
    { "title-cube", SettingsCategory::Modes },
    { "cube-enable", SettingsCategory::Modes },
    { "cube-overwrite", SettingsCategory::Modes },
    { "cube-ticks", SettingsCategory::Modes },
    { "cube-cap", SettingsCategory::Modes },
    { "cube-slack", SettingsCategory::Modes },
    { "cube-latency", SettingsCategory::Modes },
    { "title-ship", SettingsCategory::Modes },
    { "ship-enable", SettingsCategory::Modes },
    { "ship-overwrite", SettingsCategory::Modes },
    { "ship-ticks", SettingsCategory::Modes },
    { "ship-cap", SettingsCategory::Modes },
    { "ship-slack", SettingsCategory::Modes },
    { "ship-latency", SettingsCategory::Modes },
    { "title-ball", SettingsCategory::Modes },
    { "ball-enable", SettingsCategory::Modes },
    { "ball-overwrite", SettingsCategory::Modes },
    { "ball-ticks", SettingsCategory::Modes },
    { "ball-cap", SettingsCategory::Modes },
    { "ball-slack", SettingsCategory::Modes },
    { "ball-latency", SettingsCategory::Modes },
    { "title-ufo", SettingsCategory::Modes },
    { "ufo-enable", SettingsCategory::Modes },
    { "ufo-overwrite", SettingsCategory::Modes },
    { "ufo-ticks", SettingsCategory::Modes },
    { "ufo-cap", SettingsCategory::Modes },
    { "ufo-slack", SettingsCategory::Modes },
    { "ufo-latency", SettingsCategory::Modes },
    { "title-wave", SettingsCategory::Modes },
    { "wave-enable", SettingsCategory::Modes },
    { "wave-overwrite", SettingsCategory::Modes },
    { "wave-ticks", SettingsCategory::Modes },
    { "wave-cap", SettingsCategory::Modes },
    { "wave-slack", SettingsCategory::Modes },
    { "wave-latency", SettingsCategory::Modes },
    { "title-robot", SettingsCategory::Modes },
    { "robot-enable", SettingsCategory::Modes },
    { "robot-overwrite", SettingsCategory::Modes },
    { "robot-ticks", SettingsCategory::Modes },
    { "robot-cap", SettingsCategory::Modes },
    { "robot-slack", SettingsCategory::Modes },
    { "robot-latency", SettingsCategory::Modes },
    { "title-spider", SettingsCategory::Modes },
    { "spider-enable", SettingsCategory::Modes },
    { "spider-overwrite", SettingsCategory::Modes },
    { "spider-ticks", SettingsCategory::Modes },
    { "spider-cap", SettingsCategory::Modes },
    { "spider-slack", SettingsCategory::Modes },
    { "spider-latency", SettingsCategory::Modes },
    { "title-swing", SettingsCategory::Modes },
    { "swing-enable", SettingsCategory::Modes },
    { "swing-overwrite", SettingsCategory::Modes },
    { "swing-ticks", SettingsCategory::Modes },
    { "swing-cap", SettingsCategory::Modes },
    { "swing-slack", SettingsCategory::Modes },
    { "swing-latency", SettingsCategory::Modes },
    { "title-keys", SettingsCategory::Keys },
    { "hotkey-cycle", SettingsCategory::Keys },
    { "hotkey-debug", SettingsCategory::Keys },
    { "hotkey-reset-level", SettingsCategory::Keys },
    { "hotkey-auto-tune", SettingsCategory::Keys },
    { "hotkey-assist-mode", SettingsCategory::Keys },
    { "hotkey-trajectory", SettingsCategory::Keys },
    { "title-dev", SettingsCategory::Dev },
    { "title-dev-session", SettingsCategory::Dev },
    { "dev-record-enable", SettingsCategory::Dev },
    { "dev-record-count", SettingsCategory::Dev },
    { "dev-record-include-pass", SettingsCategory::Dev },
    { "dev-count-empty-attempts", SettingsCategory::Dev },
    { "dev-freeze-world", SettingsCategory::Dev },
    { "dev-disable-random", SettingsCategory::Dev },
    { "title-dev-eval", SettingsCategory::Dev },
    { "dev-eval-on-death", SettingsCategory::Dev },
    { "dev-eval-grid", SettingsCategory::Dev },
    { "dev-eval-horizon", SettingsCategory::Dev },
    { "dev-eval-tail", SettingsCategory::Dev },
    { "dev-objective", SettingsCategory::Dev },
    { "dev-eval-require-deps", SettingsCategory::Dev },
    { "dev-record-cands", SettingsCategory::Dev },
    { "dev-eval-budget-us", SettingsCategory::Dev },
    { "dev-eval-tick-budget", SettingsCategory::Dev },
    { "title-dev-overlay", SettingsCategory::Dev },
    { "dev-overlay", SettingsCategory::Dev },
    { "title-dev-actions", SettingsCategory::Dev },
    { "dev-start-record-action", SettingsCategory::Dev },
    { "dev-stop-record-action", SettingsCategory::Dev },
    { "dev-eval-last-action", SettingsCategory::Dev },
    { "dev-eval-session-action", SettingsCategory::Dev },
    { "dev-export-session-action", SettingsCategory::Dev },
    { "dev-clear-traces-action", SettingsCategory::Dev },
    { "dev-copy-report-action", SettingsCategory::Dev },
    { "title-internal", SettingsCategory::Keys },
    { "stage-index", SettingsCategory::Keys },
    { "swallow-hopeless", SettingsCategory::Keys },
};

char const* InLevelSettingsFilter::categoryName(SettingsCategory cat) {
    switch (cat) {
        case SettingsCategory::Main: return "Main";
        case SettingsCategory::Scope: return "Scope";
        case SettingsCategory::Assist: return "Assist";
        case SettingsCategory::Bake: return "Bake";
        case SettingsCategory::Modes: return "Modes";
        case SettingsCategory::Visual: return "Visual";
        case SettingsCategory::Keys: return "Keys";
        case SettingsCategory::Dev: return "Dev";
        case SettingsCategory::Count: break;
    }
    return "?";
}

SettingsCategory InLevelSettingsFilter::categoryForKey(std::string const& key) {
    return SettingsCategoryMap::lookup(key);
}

bool InLevelSettingsFilter::keepKey(std::string const& key, SettingsCategory cat) {
    return categoryForKey(key) == cat;
}

bool InLevelSettingsFilter::keepKey(std::string const& key) {
    auto const c = categoryForKey(key);
    return c == SettingsCategory::Main
        || c == SettingsCategory::Scope
        || c == SettingsCategory::Assist
        || c == SettingsCategory::Visual
        || c == SettingsCategory::Bake;
}

float InLevelSettingsFilter::preservedContentY(
    float posBefore, float heightBefore, float heightAfter
) {
    return posBefore + (heightBefore - heightAfter);
}

// File-local UI helpers (classes, not anonymous namespaces).
class SettingsFilterWalk {
public:
    static void collect(CCNode* node, std::vector<SettingNodeV3*>& settings, ScrollLayer*& list) {
        if (!node) return;
        if (auto* scroll = typeinfo_cast<ScrollLayer*>(node)) list = scroll;
        if (auto* sett = typeinfo_cast<SettingNodeV3*>(node)) {
            settings.push_back(sett);
            return;
        }
        auto* children = node->getChildren();
        if (!children) return;
        std::vector<CCNode*> kids;
        kids.reserve(static_cast<size_t>(children->count()));
        for (auto* child : CCArrayExt<CCNode*>(children)) kids.push_back(child);
        for (auto* child : kids) collect(child, settings, list);
    }

private:
    SettingsFilterWalk() = delete;
};

class SettingsFilterKeeperNode;

// Visible chip / segment strip — every tab label is tappable at once.
class SettingsCategoryBar : public CCNode {
public:
    static constexpr float kBarH = 34.f;
    static constexpr float kChipGap = 3.f;
    static constexpr float kSidePad = 4.f;
    static constexpr float kChipFontScale = 0.38f;
    static constexpr float kMinFitScale = 0.26f;
    static constexpr float kMaxFitScale = 1.f;

    bool initBar(SettingsFilterKeeperNode* owner, float width) {
        if (!CCNode::init()) return false;
        if (!owner || width < 40.f) return false;

        this->m_owner = owner;
        this->setID("earlybuffer-settings-category-bar"_spr);
        this->setContentSize({ width, kBarH });
        this->ignoreAnchorPointForPosition(false);
        this->setAnchorPoint({ 0.5f, 0.f });

        this->m_scroll = ScrollLayer::create({ width, kBarH }, true, false);
        if (!this->m_scroll) return false;
        this->m_scroll->setID("earlybuffer-settings-category-scroll"_spr);
        this->m_scroll->setPosition({ 0.f, 0.f });
        this->m_scroll->setCancelTouchLimit(8.f);
        this->addChild(this->m_scroll);

        this->m_menu = CCMenu::create();
        if (!this->m_menu) return false;
        this->m_menu->setID("earlybuffer-settings-category-menu"_spr);
        this->m_menu->setPosition({ 0.f, 0.f });
        this->m_menu->ignoreAnchorPointForPosition(false);
        this->m_menu->setAnchorPoint({ 0.f, 0.f });
        this->m_scroll->m_contentLayer->addChild(this->m_menu);

        this->rebuildChips(SettingsCategory::Main);
        return true;
    }

    void setCategory(SettingsCategory cat) {
        this->rebuildChips(cat);
        this->ensureActiveVisible(cat);
    }

    void setMenuTouchPriority(int priority) {
        if (this->m_menu) this->m_menu->setTouchPriority(priority);
        if (this->m_scroll) this->m_scroll->setTouchPriority(priority + 1);
    }

    void onChip(CCObject* sender);

    static SettingsCategoryBar* create(SettingsFilterKeeperNode* owner, float width) {
        auto* ret = new SettingsCategoryBar();
        if (!ret || !ret->initBar(owner, width)) {
            CC_SAFE_DELETE(ret);
            return nullptr;
        }
        ret->autorelease();
        return ret;
    }

private:
    ButtonSprite* makeChipSprite(SettingsCategory cat, bool active) const {
        char const* const frame = active ? "GJ_button_01.png" : "GJ_button_05.png";
        auto* spr = ButtonSprite::create(
            InLevelSettingsFilter::categoryName(cat),
            "bigFont.fnt",
            frame,
            kChipFontScale
        );
        if (!spr) return nullptr;
        if (!active) spr->setOpacity(210);
        return spr;
    }

    void rebuildChips(SettingsCategory active) {
        if (!this->m_menu || !this->m_scroll || !this->m_scroll->m_contentLayer) return;

        this->m_menu->removeAllChildren();
        this->m_chips.clear();

        float const barW = this->getContentSize().width;
        float const midY = kBarH * 0.5f;

        struct ChipMeas {
            SettingsCategory cat;
            float width;
        };
        std::vector<ChipMeas> measured;
        measured.reserve(static_cast<size_t>(SettingsCategory::Count));

        float natural = kSidePad * 2.f;
        for (int i = 0; i < static_cast<int>(SettingsCategory::Count); ++i) {
            auto const cat = static_cast<SettingsCategory>(i);
            auto* probe = this->makeChipSprite(cat, cat == active);
            float w = 40.f;
            if (probe) {
                w = probe->getContentSize().width * probe->getScale();
                // Not added — release via autorelease pool after frame; keep local Ref.
            }
            if (i > 0) natural += kChipGap;
            natural += w;
            measured.push_back({ cat, w });
        }

        float fit = 1.f;
        if (natural > barW && natural > 0.f) {
            fit = barW / natural;
        }
        if (fit < kMinFitScale) fit = kMinFitScale;
        if (fit > kMaxFitScale) fit = kMaxFitScale;

        float contentW = kSidePad * 2.f;
        for (size_t i = 0; i < measured.size(); ++i) {
            if (i > 0) contentW += kChipGap;
            contentW += measured[i].width * fit;
        }
        if (contentW < barW) contentW = barW;

        this->m_scroll->m_contentLayer->setContentSize({ contentW, kBarH });
        this->m_menu->setContentSize({ contentW, kBarH });

        float x = kSidePad;
        for (size_t i = 0; i < measured.size(); ++i) {
            auto const cat = measured[i].cat;
            bool const isActive = cat == active;
            auto* spr = this->makeChipSprite(cat, isActive);
            if (!spr) continue;
            spr->setScale(spr->getScale() * fit);
            if (isActive) spr->setScale(spr->getScale() * 1.04f);

            auto* item = CCMenuItemSpriteExtra::create(
                spr, this, menu_selector(SettingsCategoryBar::onChip)
            );
            if (!item) continue;
            item->setTag(static_cast<int>(cat));
            float const iw = item->getContentSize().width * item->getScale();
            item->setPosition({ x + iw * 0.5f, midY });
            this->m_menu->addChild(item);
            this->m_chips.push_back(item);
            x += iw + kChipGap;
        }

        // Horizontal lists start scrolled to the left (x = 0).
        this->m_scroll->m_contentLayer->setPosition({ 0.f, 0.f });
    }

    void ensureActiveVisible(SettingsCategory cat) {
        if (!this->m_scroll || !this->m_scroll->m_contentLayer) return;
        auto const idx = static_cast<int>(cat);
        if (idx < 0 || idx >= static_cast<int>(this->m_chips.size())) return;
        auto* item = this->m_chips[static_cast<size_t>(idx)];
        if (!item) return;

        float const viewW = this->getContentSize().width;
        float const contentW = this->m_scroll->m_contentLayer->getContentSize().width;
        if (contentW <= viewW + 0.5f) {
            this->m_scroll->m_contentLayer->setPositionX(0.f);
            return;
        }

        float const itemX = item->getPositionX();
        float const itemHalf = item->getContentSize().width * item->getScale() * 0.5f;
        float const left = itemX - itemHalf;
        float const right = itemX + itemHalf;
        float posX = this->m_scroll->m_contentLayer->getPositionX();
        // Content moves left (negative) to reveal right-side chips.
        float const viewLeft = -posX;
        float const viewRight = viewLeft + viewW;
        if (left < viewLeft) {
            posX = -left + kSidePad;
        }
        else if (right > viewRight) {
            posX = -(right - viewW) - kSidePad;
        }
        float const minX = viewW - contentW;
        if (posX > 0.f) posX = 0.f;
        if (posX < minX) posX = minX;
        this->m_scroll->m_contentLayer->setPositionX(posX);
    }

    SettingsFilterKeeperNode* m_owner = nullptr;
    ScrollLayer* m_scroll = nullptr;
    CCMenu* m_menu = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_chips;
};

class SettingsFilterKeeperNode : public CCNode {
public:
    SettingsCategory m_category = SettingsCategory::Main;
    SettingsCategoryBar* m_bar = nullptr;
    ScrollLayer* m_list = nullptr;
    std::vector<Ref<SettingNodeV3>> m_all;
    size_t m_lastChildCount = 0;
    SettingsCategory m_appliedCat = SettingsCategory::Count;
    bool m_loggedShortList = false;

    void setCategory(SettingsCategory cat) {
        auto const n = static_cast<int>(SettingsCategory::Count);
        auto i = static_cast<int>(cat) % n;
        if (i < 0) i += n;

        this->m_category = static_cast<SettingsCategory>(i);
        if (this->m_bar) this->m_bar->setCategory(this->m_category);
        this->refilter(true);
        if (this->m_list) this->m_list->moveToTop();
    }

    void cycle(int delta) {
        this->setCategory(static_cast<SettingsCategory>(
            static_cast<int>(this->m_category) + delta
        ));
    }

    void ensureBar(ScrollLayer* list) {
        if (!list) return;

        this->m_list = list;
        auto* parent = list->getParent();
        if (!parent) return;

        constexpr float kBarH = SettingsCategoryBar::kBarH;
        if (auto* existing = typeinfo_cast<SettingsCategoryBar*>(
                parent->getChildByID("earlybuffer-settings-category-bar"_spr)
            )) {
            this->m_bar = existing;
            this->m_bar->setCategory(this->m_category);
            return;
        }

        auto const listSize = list->getContentSize();
        // Prefer attaching even on shorter lists; only skip when physically impossible.
        if (listSize.height <= kBarH + 16.f) {
            if (!this->m_loggedShortList) {
                this->m_loggedShortList = true;
                log::warn(
                    "EarlyBuffer settings category bar skipped: list height {:.1f} <= {:.1f}",
                    listSize.height,
                    kBarH + 16.f
                );
            }
            return;
        }

        list->setContentSize({ listSize.width, listSize.height - kBarH });
        auto* bar = SettingsCategoryBar::create(this, listSize.width);
        if (!bar) return;

        bar->setPosition({
            list->getPositionX() + listSize.width * 0.5f,
            list->getPositionY() + list->getContentSize().height
        });
        bar->setMenuTouchPriority(list->getTouchPriority() - 1);
        parent->addChild(bar);
        this->m_bar = bar;
        this->m_bar->setCategory(this->m_category);
    }

    void absorbFromTree(CCNode* popup) {
        if (!popup) return;

        std::vector<SettingNodeV3*> found;
        ScrollLayer* list = nullptr;
        SettingsFilterWalk::collect(popup, found, list);
        if (list) this->m_list = list;

        for (auto* sett : found) {
            if (!sett) continue;
            bool known = false;
            for (auto const& ref : this->m_all) {
                if (ref.data() != sett) continue;
                known = true;
                break;
            }
            if (known) continue;
            this->m_all.push_back(sett);
        }
    }

    static bool nodeKeep(SettingNodeV3* sett, SettingsCategory cat) {
        if (!sett) return false;
        if (auto setting = sett->getSetting()) {
            return InLevelSettingsFilter::keepKey(setting->getKey(), cat);
        }

        // Unresolved custom ("Missing setting 'key'") — still map by key in the label
        // so Bake/Assist pages aren't empty when registration races load.
        auto* label = sett->getNameLabel();
        if (!label) return false;

        std::string const s = label->getString();
        auto match = [&](char const* key) {
            return s.find(key) != std::string::npos
                && InLevelSettingsFilter::keepKey(key, cat);
        };
        return match("import-bake-replay")
            || match("cycle-assist-mode")
            || match("bake-map-action")
            || match("brute-bake-action")
            || match("cancel-bake-action")
            || match("cycle-preset-action")
            || match("reset-learn-action")
            || match("export-config-action")
            || match("import-config-action");
    }

    void refilter(bool force) {
        auto* popup = this->getParent();
        if (!popup) return;

        this->absorbFromTree(popup);
        this->ensureBar(this->m_list);
        if (!this->m_list || !this->m_list->m_contentLayer) return;
        if (this->m_all.empty()) return;

        auto* content = this->m_list->m_contentLayer;
        size_t const childCount = content->getChildrenCount();
        if (!force
            && this->m_appliedCat == this->m_category
            && childCount == this->m_lastChildCount) {
            return;
        }

        float const yBefore = content->getPositionY();
        float const hBefore = content->getContentHeight();

        for (auto& sett : this->m_all) {
            if (!sett) continue;
            sett->removeFromParent();
        }

        bool bg = false;
        for (auto& sett : this->m_all) {
            if (!sett || !nodeKeep(sett, this->m_category)) continue;
            sett->setVisible(true);
            content->addChild(sett);
            sett->setDefaultBGColor(ccc4(0, 0, 0, bg ? 60 : 20));
            bg = !bg;
        }

        content->updateLayout();
        float const hAfter = content->getContentHeight();
        if (force && this->m_appliedCat != this->m_category) {
            this->m_list->moveToTop();
        }
        else {
            content->setPositionY(
                InLevelSettingsFilter::preservedContentY(yBefore, hBefore, hAfter)
            );
        }

        this->m_appliedCat = this->m_category;
        this->m_lastChildCount = content->getChildrenCount();
    }

    bool initKeeper() {
        if (!CCNode::init()) return false;

        this->setID("earlybuffer-inlevel-settings-filter"_spr);
        this->m_changeHandle = SettingNodeValueChangeEvent().listen(
            [this](std::string_view modID, std::string_view, SettingNodeV3*, bool) {
                if (modID != Mod::get()->getID()) return ListenerResult::Propagate;
                Loader::get()->queueInMainThread([self = Ref(this)] {
                    if (!self) return;
                    self->refilter(true);
                });
                return ListenerResult::Propagate;
            }
        );
        this->schedule(schedule_selector(SettingsFilterKeeperNode::onTick), 0.05f);
        this->refilter(true);
        return true;
    }

    void onTick(float) {
        this->refilter(false);
    }

    static SettingsFilterKeeperNode* create() {
        auto* ret = new SettingsFilterKeeperNode();
        if (!ret || !ret->initKeeper()) {
            CC_SAFE_DELETE(ret);
            return nullptr;
        }
        ret->autorelease();
        return ret;
    }

    static SettingsFilterKeeperNode* findOpen() {
        auto* scene = CCScene::get();
        if (!scene) return nullptr;
        return typeinfo_cast<SettingsFilterKeeperNode*>(
            scene->getChildByIDRecursive("earlybuffer-inlevel-settings-filter"_spr)
        );
    }

private:
    ListenerHandle m_changeHandle{};
};

void SettingsCategoryBar::onChip(CCObject* sender) {
    auto* item = typeinfo_cast<CCMenuItem*>(sender);
    if (!item || !this->m_owner) return;
    auto const idx = item->getTag();
    if (idx < 0 || idx >= static_cast<int>(SettingsCategory::Count)) return;
    this->m_owner->setCategory(static_cast<SettingsCategory>(idx));
}

void InLevelSettingsFilter::apply(CCNode* popup, SettingsCategory cat) {
    if (!popup) return;
    std::vector<SettingNodeV3*> settings;
    ScrollLayer* list = nullptr;
    SettingsFilterWalk::collect(popup, settings, list);
    if (!list || !list->m_contentLayer || settings.empty()) return;

    auto* content = list->m_contentLayer;
    float const yBefore = content->getPositionY();
    float const hBefore = content->getContentHeight();

    for (auto* sett : settings) {
        if (!sett) continue;
        bool keep = false;
        if (auto setting = sett->getSetting()) {
            keep = keepKey(setting->getKey(), cat);
        }
        if (!keep) sett->removeFromParent();
        else sett->setVisible(true);
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
    expect(categoryForKey("lag-probe") == SettingsCategory::Main, "cat-lag-main");
    expect(categoryForKey("show-pause-hub") == SettingsCategory::Main, "cat-hub-main");
    expect(categoryForKey("show-overlay") == SettingsCategory::Main, "cat-overlay-main");
    expect(categoryForKey("title-scope") == SettingsCategory::Scope, "cat-title-scope");
    expect(categoryForKey("assist-only-practice") == SettingsCategory::Scope, "cat-practice-scope");
    expect(categoryForKey("assist-off-1-from") == SettingsCategory::Scope, "cat-off-scope");
    expect(categoryForKey("import-bake-replay") == SettingsCategory::Bake, "cat-import-bake");
    expect(categoryForKey("bake-radius") == SettingsCategory::Bake, "cat-bake-radius");
    expect(categoryForKey("cycle-preset-action") == SettingsCategory::Assist, "cat-cycle-preset");
    expect(categoryForKey("assist-pick") == SettingsCategory::Assist, "cat-assist-pick");
    expect(categoryForKey("window-unit") == SettingsCategory::Assist, "cat-window-assist");
    expect(categoryForKey("show-trajectory") == SettingsCategory::Visual, "cat-traj-visual");
    expect(categoryForKey("cube-latency") == SettingsCategory::Modes, "cat-cube-modes");
    expect(categoryForKey("hotkey-cycle") == SettingsCategory::Keys, "cat-hotkey-keys");
    expect(categoryForKey("stage-index") == SettingsCategory::Keys, "cat-stage-keys");
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
        auto const c = categoryForKey(it->key);
        expect(c == it->cat, it->key);
        expect(c != SettingsCategory::Count, it->key);
        ++mapped;
    }
    expect(mapped == SettingsCategoryMap::kCount, "map-count-171");

    // Every portable ConfigBundle key must be in the category map.
    for (auto const* it = ConfigBundleKeys::begin(); it != ConfigBundleKeys::end(); ++it) {
        auto const c = categoryForKey(it->key);
        expect(c != SettingsCategory::Count, it->key);
    }
    expect(ConfigBundleKeys::count() > 0, "config-keys-nonempty");

    expect(preservedContentY(100.f, 800.f, 400.f) == 500.f, "scroll-shrink");
    expect(preservedContentY(0.f, 100.f, 100.f) == 0.f, "scroll-same");
    expect(SettingsCategoryBar::kBarH == 34.f, "chip-bar-height");
    expect(SettingsCategoryBar::kMinFitScale > 0.f, "chip-min-fit");

    if (fails == 0) report = "InLevelSettingsFilter selfCheck OK";
    else report = fmt::format("InLevelSettingsFilter {} fails: {}", fails, report);
    return fails == 0;
}

void InLevelSettingsFilterKeeper::attach(CCNode* popup) {
    if (!popup) return;
    if (popup->getChildByID("earlybuffer-inlevel-settings-filter"_spr)) return;

    auto* keeper = SettingsFilterKeeperNode::create();
    if (!keeper) return;
    popup->addChild(keeper);
}

bool InLevelSettingsFilterKeeper::isOpen() {
    return SettingsFilterKeeperNode::findOpen() != nullptr;
}

void InLevelSettingsFilterKeeper::refreshOpen() {
    // Action button rows refresh themselves via onTick.
}

class SettingsTabWatchdog : public CCNode {
public:
    static bool popupIsOurSettings(CCNode* root) {
        if (!root || !Mod::get()) return false;

        std::string const ourId = Mod::get()->getID();
        std::vector<SettingNodeV3*> settings;
        ScrollLayer* list = nullptr;
        SettingsFilterWalk::collect(root, settings, list);
        for (auto* sett : settings) {
            if (!sett) continue;
            if (auto s = sett->getSetting()) {
                if (s->getModID() == ourId) return true;
            }
            auto* label = sett->getNameLabel();
            if (!label) continue;
            std::string const text = label->getString();
            if (text.find("import-bake-replay") != std::string::npos
                || text.find("bake-map-action") != std::string::npos
                || text.find("show-pause-hub") != std::string::npos) {
                return true;
            }
        }

        std::vector<CCNode*> stack{ root };
        while (!stack.empty()) {
            auto* n = stack.back();
            stack.pop_back();
            if (!n) continue;

            if (auto* lab = typeinfo_cast<CCLabelBMFont*>(n)) {
                std::string const t = lab->getString();
                if (t.find("EarlyBuffer") != std::string::npos
                    && t.find("Settings") != std::string::npos) {
                    return true;
                }
            }
            auto* kids = n->getChildren();
            if (!kids) continue;
            for (auto* c : CCArrayExt<CCNode*>(kids)) stack.push_back(c);
        }
        return false;
    }

    static void collectAlerts(CCNode* node, std::vector<FLAlertLayer*>& out) {
        if (!node) return;
        if (auto* alert = typeinfo_cast<FLAlertLayer*>(node)) {
            out.push_back(alert);
        }
        auto* kids = node->getChildren();
        if (!kids) return;
        for (auto* child : CCArrayExt<CCNode*>(kids)) {
            collectAlerts(child, out);
        }
    }

    static CCNode* findUnattachedOurPopup() {
        auto* scene = CCScene::get();
        if (!scene) return nullptr;
        if (scene->getChildByIDRecursive("earlybuffer-inlevel-settings-filter"_spr)) {
            return nullptr;
        }

        std::vector<FLAlertLayer*> alerts;
        collectAlerts(scene, alerts);
        for (auto* alert : alerts) {
            if (!alert) continue;
            if (alert->getChildByID("earlybuffer-inlevel-settings-filter"_spr)) continue;
            if (!alert->getChildByIDRecursive("search-input")) continue;
            if (popupIsOurSettings(alert)) return alert;
        }
        return nullptr;
    }

    void ensureOnScene() {
        auto* scene = CCScene::get();
        if (!scene) return;
        if (this->getParent() == scene) return;

        this->retain();
        this->removeFromParentAndCleanup(false);
        scene->addChild(this);
        this->release();
    }

    void onTick(float) {
        this->ensureOnScene();
        auto* popup = findUnattachedOurPopup();
        if (!popup) return;
        InLevelSettingsFilterKeeper::attach(popup);
    }

    bool initWatchdog() {
        if (!CCNode::init()) return false;
        this->setID("earlybuffer-settings-tab-watchdog"_spr);
        this->schedule(schedule_selector(SettingsTabWatchdog::onTick), 0.1f);
        return true;
    }

    static SettingsTabWatchdog* create() {
        auto* ret = new SettingsTabWatchdog();
        if (!ret || !ret->initWatchdog()) {
            CC_SAFE_DELETE(ret);
            return nullptr;
        }
        ret->autorelease();
        return ret;
    }
};

class SettingsTabWatchdogState {
public:
    static Ref<SettingsTabWatchdog>& slot() {
        static Ref<SettingsTabWatchdog> s_watchdog;
        return s_watchdog;
    }

private:
    SettingsTabWatchdogState() = delete;
};

void InLevelSettingsFilterKeeper::startWatchdog() {
    auto& slot = SettingsTabWatchdogState::slot();
    if (!slot) {
        slot = SettingsTabWatchdog::create();
    }
    if (!slot) return;
    auto* scene = CCScene::get();
    if (!scene) {
        Loader::get()->queueInMainThread([] {
            InLevelSettingsFilterKeeper::startWatchdog();
        });
        return;
    }
    slot->ensureOnScene();
}
