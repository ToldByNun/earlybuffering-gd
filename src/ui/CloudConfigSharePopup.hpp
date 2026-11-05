#ifndef EARLYBUFFER_UI_CLOUDCONFIGSHAREPOPUP_HPP
#define EARLYBUFFER_UI_CLOUDCONFIGSHAREPOPUP_HPP

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <functional>
#include <string>

// Collect name / author / description / tags before outbox + submit.
class CloudConfigSharePopup : public geode::Popup {
public:
    using ClosedCallback = std::function<void()>;

    static CloudConfigSharePopup* create(ClosedCallback onClosed = {});
    static void open(ClosedCallback onClosed = {});

protected:
    bool init(ClosedCallback onClosed);
    void onClose(cocos2d::CCObject*) override;

    void onConfirm(cocos2d::CCObject*);
    void syncIdPreview();
    void notifyClosed();

    geode::TextInput* m_nameInput = nullptr;
    geode::TextInput* m_authorInput = nullptr;
    geode::TextInput* m_descInput = nullptr;
    geode::TextInput* m_tagsInput = nullptr;
    cocos2d::CCLabelBMFont* m_idPreview = nullptr;
    ClosedCallback m_onClosed;
    bool m_closedNotified = false;
};

#endif // EARLYBUFFER_UI_CLOUDCONFIGSHAREPOPUP_HPP
