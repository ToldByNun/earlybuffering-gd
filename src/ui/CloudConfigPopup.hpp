#ifndef EARLYBUFFER_UI_CLOUDCONFIGPOPUP_HPP
#define EARLYBUFFER_UI_CLOUDCONFIGPOPUP_HPP

#include "../cloud/CloudConfigIndex.hpp"
#include "../cloud/CloudConfigService.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/General.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextArea.hpp>
#include <Geode/ui/TextInput.hpp>
#include <string>

// Catalog browser matching GD / Geode popup + list aesthetics.
class CloudConfigPopup : public geode::Popup {
public:
    static CloudConfigPopup* create();
    static void open();

protected:
    bool init();

    void onRefresh(cocos2d::CCObject*);
    void onInstall(cocos2d::CCObject*);
    void onShare(cocos2d::CCObject*);

    void loadInitial();
    void startRefresh(bool force);
    void onRefreshDone(CloudConfigService::RefreshResult const& r);
    void ensureLocalCatalog();
    void rebuildList();
    void updateDetail();
    void setStatus(std::string const& text, bool isError);
    void selectPack(std::string const& id);
    void doInstall(std::string const& id);
    void onPackFetched(
        CloudConfigService::PackFetchResult const& fr,
        CloudConfigIndex::PackEntry entry
    );

    bool packMatchesSearch(CloudConfigIndex::PackEntry const& e) const;
    std::string liveModVersion() const;
    bool isCached(std::string const& id) const;
    bool isActive(std::string const& id) const;
    bool hasUpdate(CloudConfigIndex::PackEntry const& e) const;
    CloudConfigIndex::PackEntry const* selectedEntry() const;
    void setShareOpen(bool open);

    geode::ScrollLayer* m_listScroll = nullptr;
    geode::ListBorders* m_listBorders = nullptr;
    geode::TextInput* m_search = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCNode* m_detailRoot = nullptr;
    cocos2d::CCLabelBMFont* m_detailTitle = nullptr;
    cocos2d::CCLabelBMFont* m_detailMeta = nullptr;
    geode::SimpleTextArea* m_detailBody = nullptr;
    cocos2d::CCLabelBMFont* m_detailFoot = nullptr;
    cocos2d::CCLayerColor* m_detailBg = nullptr;
    CCMenuItemSpriteExtra* m_installBtn = nullptr;
    CCMenuItemSpriteExtra* m_refreshBtn = nullptr;

    float m_listWidth = 0.f;
    float m_listHeight = 0.f;
    float m_detailWidth = 0.f;
    float m_detailHeight = 0.f;

    std::string m_searchQuery;
    std::string m_selectedId;
    std::string m_activeId;
    bool m_busyUi = false;
    bool m_shareOpen = false;
    uint64_t m_opGen = 0;
};

#endif // EARLYBUFFER_UI_CLOUDCONFIGPOPUP_HPP
