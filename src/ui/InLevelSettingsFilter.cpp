#include "InLevelSettingsFilter.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <fmt/format.h>
#include <cstring>
#include <vector>

using namespace geode::prelude;

char const* InLevelSettingsFilter::categoryName(SettingsCategory cat) {
    switch (cat) {
        case SettingsCategory::Main: return "Main";
        case SettingsCategory::Bake: return "Bake";
        case SettingsCategory::Ingame: return "Ingame";
        case SettingsCategory::Assist: return "Assist";
        case SettingsCategory::Visual: return "Visual";
        case SettingsCategory::Modes: return "Modes";
        case SettingsCategory::Keys: return "Keys";
        case SettingsCategory::Count: break;
    }
    return "?";
}

SettingsCategory InLevelSettingsFilter::categoryForKey(std::string const& key) {
    if (key == "title-general" || key == "enabled" || key == "preset"
        || key == "safe-mode" || key == "debug-logging") {
        return SettingsCategory::Main;
    }
    if (key == "title-bake-actions" || key == "import-bake-replay"
        || key == "cycle-assist-mode" || key == "bake-map-action"
        || key == "brute-bake-action" || key == "cancel-bake-action") {
        return SettingsCategory::Bake;
    }
    if (key == "show-overlay" || key == "show-pause-bake"
        || key == "show-pause-assist" || key == "show-pause-hub") {
        return SettingsCategory::Ingame;
    }
    if (key == "title-assist-actions" || key == "cycle-preset-action"
        || key == "reset-learn-action" || key == "export-config-action"
        || key == "import-config-action") {
        return SettingsCategory::Assist;
    }
    if (key == "show-trajectory" || key == "save-feedback" || key == "save-icon-color") {
        return SettingsCategory::Visual;
    }
    if (key == "title-cube" || key.rfind("cube-", 0) == 0
        || key == "title-ship" || key.rfind("ship-", 0) == 0
        || key == "title-ball" || key.rfind("ball-", 0) == 0
        || key == "title-ufo" || key.rfind("ufo-", 0) == 0
        || key == "title-wave" || key.rfind("wave-", 0) == 0
        || key == "title-robot" || key.rfind("robot-", 0) == 0
        || key == "title-spider" || key.rfind("spider-", 0) == 0
        || key == "title-swing" || key.rfind("swing-", 0) == 0) {
        return SettingsCategory::Modes;
    }
    if (key == "title-keys" || key.rfind("hotkey-", 0) == 0
        || key == "title-internal" || key == "stage-index" || key == "swallow-hopeless") {
        return SettingsCategory::Keys;
    }
    return SettingsCategory::Assist;
}

bool InLevelSettingsFilter::keepKey(std::string const& key, SettingsCategory cat) {
    return categoryForKey(key) == cat;
}

bool InLevelSettingsFilter::keepKey(std::string const& key) {
    auto const c = categoryForKey(key);
    return c == SettingsCategory::Main
        || c == SettingsCategory::Ingame
        || c == SettingsCategory::Assist
        || c == SettingsCategory::Visual
        || c == SettingsCategory::Bake;
}

float InLevelSettingsFilter::preservedContentY(
    float posBefore, float heightBefore, float heightAfter
) {
    return posBefore + (heightBefore - heightAfter);
}

namespace {

class FilterWalk {
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
};

class KeeperNode;

class CategoryBar : public CCMenu {
public:
    bool initBar(KeeperNode* owner, float width) {
        if (!CCMenu::init()) return false;
        m_owner = owner;
        this->setID("earlybuffer-settings-category-bar"_spr);
        this->setContentSize({ width, 28.f });
        this->ignoreAnchorPointForPosition(false);
        this->setAnchorPoint({ 0.5f, 0.f });

        auto* leftSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
        leftSpr->setScale(0.55f);
        auto* left = CCMenuItemSpriteExtra::create(
            leftSpr, this, menu_selector(CategoryBar::onPrev)
        );
        left->setPosition({ 24.f, 14.f });
        this->addChild(left);

        auto* rightSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
        rightSpr->setScale(0.55f);
        rightSpr->setFlipX(true);
        auto* right = CCMenuItemSpriteExtra::create(
            rightSpr, this, menu_selector(CategoryBar::onNext)
        );
        right->setPosition({ width - 24.f, 14.f });
        this->addChild(right);

        m_label = CCLabelBMFont::create("MAIN", "goldFont.fnt");
        m_label->setScale(0.45f);
        m_label->setPosition({ width * 0.5f, 14.f });
        this->addChild(m_label);
        return true;
    }

    void setCategory(SettingsCategory cat) {
        if (m_label) m_label->setString(InLevelSettingsFilter::categoryName(cat));
    }

    void onPrev(CCObject*);
    void onNext(CCObject*);

    static CategoryBar* create(KeeperNode* owner, float width) {
        auto* ret = new CategoryBar();
        if (ret && ret->initBar(owner, width)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

private:
    KeeperNode* m_owner = nullptr;
    CCLabelBMFont* m_label = nullptr;
};

class KeeperNode : public CCNode {
public:
    SettingsCategory m_category = SettingsCategory::Main;
    CategoryBar* m_bar = nullptr;
    ScrollLayer* m_list = nullptr;
    std::vector<Ref<SettingNodeV3>> m_all;
    size_t m_lastChildCount = 0;
    SettingsCategory m_appliedCat = SettingsCategory::Count;

    void setCategory(SettingsCategory cat) {
        auto const n = static_cast<int>(SettingsCategory::Count);
        auto i = static_cast<int>(cat) % n;
        if (i < 0) i += n;
        m_category = static_cast<SettingsCategory>(i);
        if (m_bar) m_bar->setCategory(m_category);
        this->refilter(true);
        if (m_list) m_list->moveToTop();
    }

    void cycle(int delta) {
        this->setCategory(static_cast<SettingsCategory>(
            static_cast<int>(m_category) + delta
        ));
    }

    void ensureBar(ScrollLayer* list) {
        if (!list) return;
        m_list = list;
        auto* parent = list->getParent();
        if (!parent) return;

        constexpr float kBarH = 28.f;
        if (auto* existing = typeinfo_cast<CategoryBar*>(
                parent->getChildByID("earlybuffer-settings-category-bar"_spr)
            )) {
            m_bar = existing;
            m_bar->setCategory(m_category);
            return;
        }

        auto const listSize = list->getContentSize();
        if (listSize.height <= kBarH + 40.f) return;

        list->setContentSize({ listSize.width, listSize.height - kBarH });
        if (auto* bar = CategoryBar::create(this, listSize.width)) {
            bar->setPosition({
                list->getPositionX() + listSize.width * 0.5f,
                list->getPositionY() + list->getContentSize().height
            });
            bar->setTouchPriority(list->getTouchPriority() - 1);
            parent->addChild(bar);
            m_bar = bar;
            m_bar->setCategory(m_category);
        }
    }

    void absorbFromTree(CCNode* popup) {
        std::vector<SettingNodeV3*> found;
        ScrollLayer* list = nullptr;
        FilterWalk::collect(popup, found, list);
        if (list) m_list = list;
        for (auto* sett : found) {
            bool known = false;
            for (auto const& ref : m_all) {
                if (ref.data() == sett) { known = true; break; }
            }
            if (!known) m_all.push_back(sett);
        }
    }

    static bool nodeKeep(SettingNodeV3* sett, SettingsCategory cat) {
        if (!sett) return false;
        if (auto setting = sett->getSetting()) {
            return InLevelSettingsFilter::keepKey(setting->getKey(), cat);
        }
        // Unresolved custom ("Missing setting 'key'") — still map by key in the label
        // so Bake/Assist pages aren't empty when registration races load.
        if (auto* label = sett->getNameLabel()) {
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
        return false;
    }

    void refilter(bool force) {
        auto* popup = this->getParent();
        if (!popup) return;

        this->absorbFromTree(popup);
        this->ensureBar(m_list);
        if (!m_list || !m_list->m_contentLayer) return;
        if (m_all.empty()) return;

        auto* content = m_list->m_contentLayer;
        size_t const childCount = content->getChildrenCount();

        if (!force
            && m_appliedCat == m_category
            && childCount == m_lastChildCount) {
            return;
        }

        float const yBefore = content->getPositionY();
        float const hBefore = content->getContentHeight();

        for (auto& sett : m_all) {
            if (sett) sett->removeFromParent();
        }

        bool bg = false;
        for (auto& sett : m_all) {
            if (!sett || !nodeKeep(sett, m_category)) continue;
            sett->setVisible(true);
            content->addChild(sett);
            sett->setDefaultBGColor(ccc4(0, 0, 0, bg ? 60 : 20));
            bg = !bg;
        }

        content->updateLayout();
        float const hAfter = content->getContentHeight();
        if (force && m_appliedCat != m_category) {
            m_list->moveToTop();
        }
        else {
            content->setPositionY(
                InLevelSettingsFilter::preservedContentY(yBefore, hBefore, hAfter)
            );
        }

        m_appliedCat = m_category;
        m_lastChildCount = content->getChildrenCount();
    }

    bool initKeeper() {
        if (!CCNode::init()) return false;
        this->setID("earlybuffer-inlevel-settings-filter"_spr);
        m_changeHandle = SettingNodeValueChangeEvent().listen(
            [this](std::string_view modID, std::string_view, SettingNodeV3*, bool) {
                if (modID != Mod::get()->getID()) return ListenerResult::Propagate;
                Loader::get()->queueInMainThread([self = Ref(this)] {
                    if (self) self->refilter(true);
                });
                return ListenerResult::Propagate;
            }
        );
        this->schedule(schedule_selector(KeeperNode::onTick), 0.05f);
        this->refilter(true);
        return true;
    }

    void onTick(float) {
        this->refilter(false);
    }

    static KeeperNode* create() {
        auto* ret = new KeeperNode();
        if (ret && ret->initKeeper()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    static KeeperNode* findOpen() {
        auto* scene = CCScene::get();
        if (!scene) return nullptr;
        return typeinfo_cast<KeeperNode*>(
            scene->getChildByIDRecursive("earlybuffer-inlevel-settings-filter"_spr)
        );
    }

private:
    ListenerHandle m_changeHandle{};
};

void CategoryBar::onPrev(CCObject*) {
    if (m_owner) m_owner->cycle(-1);
}

void CategoryBar::onNext(CCObject*) {
    if (m_owner) m_owner->cycle(1);
}

} // namespace

void InLevelSettingsFilter::apply(CCNode* popup, SettingsCategory cat) {
    if (!popup) return;
    std::vector<SettingNodeV3*> settings;
    ScrollLayer* list = nullptr;
    FilterWalk::collect(popup, settings, list);
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

    expect(categoryForKey("enabled") == SettingsCategory::Main, "cat-enabled-main");
    expect(categoryForKey("import-bake-replay") == SettingsCategory::Bake, "cat-import-bake");
    expect(categoryForKey("bake-map-action") == SettingsCategory::Bake, "cat-bake-map");
    expect(categoryForKey("show-pause-hub") == SettingsCategory::Ingame, "cat-hub-ingame");
    expect(categoryForKey("cycle-preset-action") == SettingsCategory::Assist, "cat-cycle-preset");
    expect(categoryForKey("assist-mode") == SettingsCategory::Assist, "cat-assist-mode");
    expect(categoryForKey("show-trajectory") == SettingsCategory::Visual, "cat-traj-visual");
    expect(categoryForKey("cube-latency") == SettingsCategory::Modes, "cat-cube-modes");
    expect(categoryForKey("hotkey-cycle") == SettingsCategory::Keys, "cat-hotkey-keys");
    expect(keepKey("import-bake-replay", SettingsCategory::Bake), "keep-import-on-bake");
    expect(!keepKey("import-bake-replay", SettingsCategory::Main), "drop-import-on-main");

    expect(static_cast<int>(SettingsCategory::Count) == 7, "cat-count-7");
    expect(std::strcmp(categoryName(SettingsCategory::Bake), "Bake") == 0, "cat-name-bake");

    expect(preservedContentY(100.f, 800.f, 400.f) == 500.f, "scroll-shrink");
    expect(preservedContentY(0.f, 100.f, 100.f) == 0.f, "scroll-same");

    if (fails == 0) report = "InLevelSettingsFilter selfCheck OK";
    else report = fmt::format("InLevelSettingsFilter {} fails: {}", fails, report);
    return fails == 0;
}

void InLevelSettingsFilterKeeper::attach(CCNode* popup) {
    if (!popup) return;
    if (popup->getChildByID("earlybuffer-inlevel-settings-filter"_spr)) return;
    if (auto* keeper = KeeperNode::create()) {
        popup->addChild(keeper);
    }
}

bool InLevelSettingsFilterKeeper::isOpen() {
    return KeeperNode::findOpen() != nullptr;
}

void InLevelSettingsFilterKeeper::refreshOpen() {
    // Action button rows refresh themselves via onTick.
}

namespace {

class SettingsTabWatchdog : public CCNode {
public:
    static bool popupIsOurSettings(CCNode* root) {
        if (!root || !Mod::get()) return false;
        std::string const ourId = Mod::get()->getID();
        std::vector<SettingNodeV3*> settings;
        ScrollLayer* list = nullptr;
        FilterWalk::collect(root, settings, list);
        for (auto* sett : settings) {
            if (!sett) continue;
            if (auto s = sett->getSetting()) {
                if (s->getModID() == ourId) return true;
            }
            if (auto* label = sett->getNameLabel()) {
                std::string const text = label->getString();
                if (text.find("import-bake-replay") != std::string::npos
                    || text.find("bake-map-action") != std::string::npos
                    || text.find("show-pause-hub") != std::string::npos) {
                    return true;
                }
            }
        }
        // Title fallback (Geode sets "Settings for EarlyBuffer").
        std::vector<CCNode*> stack{ root };
        while (!stack.empty()) {
            auto* n = stack.back();
            stack.pop_back();
            if (auto* lab = typeinfo_cast<CCLabelBMFont*>(n)) {
                std::string const t = lab->getString();
                if (t.find("EarlyBuffer") != std::string::npos
                    && t.find("Settings") != std::string::npos) {
                    return true;
                }
            }
            if (auto* kids = n->getChildren()) {
                for (auto* c : CCArrayExt<CCNode*>(kids)) stack.push_back(c);
            }
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

        // Scan every alert (Geode may nest ModSettings under ModPopup / scene).
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
        if (auto* popup = findUnattachedOurPopup()) {
            InLevelSettingsFilterKeeper::attach(popup);
        }
    }

    bool initWatchdog() {
        if (!CCNode::init()) return false;
        this->setID("earlybuffer-settings-tab-watchdog"_spr);
        this->schedule(schedule_selector(SettingsTabWatchdog::onTick), 0.1f);
        return true;
    }

    static SettingsTabWatchdog* create() {
        auto* ret = new SettingsTabWatchdog();
        if (ret && ret->initWatchdog()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

Ref<SettingsTabWatchdog> g_settingsTabWatchdog;

} // namespace

void InLevelSettingsFilterKeeper::startWatchdog() {
    if (!g_settingsTabWatchdog) {
        g_settingsTabWatchdog = SettingsTabWatchdog::create();
    }
    if (!g_settingsTabWatchdog) return;
    auto* scene = CCScene::get();
    if (!scene) {
        Loader::get()->queueInMainThread([] {
            InLevelSettingsFilterKeeper::startWatchdog();
        });
        return;
    }
    g_settingsTabWatchdog->ensureOnScene();
}
