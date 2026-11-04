#include "CloudConfigSharePopup.hpp"

#include "../cloud/CloudConfigShare.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/Layout.hpp>

using namespace geode::prelude;

CloudConfigSharePopup* CloudConfigSharePopup::create(ClosedCallback onClosed) {
    auto* ret = new CloudConfigSharePopup();
    if (ret->init(std::move(onClosed))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

void CloudConfigSharePopup::open(ClosedCallback onClosed) {
    if (auto* p = create(std::move(onClosed))) {
        p->show();
    }
}

bool CloudConfigSharePopup::init(ClosedCallback onClosed) {
    m_onClosed = std::move(onClosed);

    if (!Popup::init(300.f, 228.f, "GJ_square01.png")) {
        return false;
    }

    // Cancel/Share already cover dismiss — hide the stacked corner X over parent popups.
    if (m_closeBtn) {
        m_closeBtn->setVisible(false);
        m_closeBtn->setEnabled(false);
    }

    this->setTitle("Share Config", "goldFont.fnt", 0.65f, 14.f);

    auto addLabeledInput = [&](
        char const* label,
        char const* placeholder,
        float y,
        size_t maxChars,
        TextInput*& out
    ) {
        auto* lab = CCLabelBMFont::create(label, "goldFont.fnt");
        lab->setScale(0.38f);
        lab->setAnchorPoint({ 0.f, 0.5f });
        lab->setOpacity(220);
        m_mainLayer->addChildAtPosition(lab, Anchor::TopLeft, ccp(24.f, y + 13.f));

        out = TextInput::create(252.f, placeholder, "bigFont.fnt");
        out->setCommonFilter(CommonFilter::Any);
        out->setTextAlign(TextInputAlign::Left);
        out->setMaxCharCount(maxChars);
        out->setScale(0.92f);
        m_mainLayer->addChildAtPosition(out, Anchor::Top, ccp(0.f, y));
    };

    addLabeledInput("Name", "My pack name", -52.f, 48, m_nameInput);
    addLabeledInput("Author", "Your name", -90.f, 32, m_authorInput);
    addLabeledInput("Description", "What does this pack do?", -136.f, 160, m_descInput);

    m_nameInput->setString("Shared Config");
    m_nameInput->setCallback([this](std::string const&) {
        this->syncIdPreview();
    });

    m_idPreview = CCLabelBMFont::create("suggested id: shared-config", "chatFont.fnt");
    m_idPreview->setScale(0.48f);
    m_idPreview->setOpacity(210);
    m_idPreview->setID("cloud-share-id"_spr);
    m_mainLayer->addChildAtPosition(m_idPreview, Anchor::Bottom, ccp(0.f, 48.f));
    this->syncIdPreview();

    auto* menu = CCMenu::create();
    menu->setContentSize({ 260.f, 30.f });
    menu->setID("cloud-share-menu"_spr);
    m_mainLayer->addChildAtPosition(menu, Anchor::Bottom, ccp(0.f, 22.f));
    menu->setLayout(
        RowLayout::create()
            ->setGap(12.f)
            ->setAxisAlignment(AxisAlignment::Center)
    );

    auto* cancelSpr = ButtonSprite::create(
        "Cancel", "bigFont.fnt", "GJ_button_06.png", 0.8f
    );
    cancelSpr->setScale(0.48f);
    auto* cancelBtn = CCMenuItemSpriteExtra::create(
        cancelSpr, this, menu_selector(CloudConfigSharePopup::onClose)
    );
    menu->addChild(cancelBtn);

    auto* okSpr = ButtonSprite::create(
        "Share", "bigFont.fnt", "GJ_button_01.png", 0.8f
    );
    okSpr->setScale(0.48f);
    auto* okBtn = CCMenuItemSpriteExtra::create(
        okSpr, this, menu_selector(CloudConfigSharePopup::onConfirm)
    );
    menu->addChild(okBtn);
    menu->updateLayout();

    return true;
}

void CloudConfigSharePopup::notifyClosed() {
    if (m_closedNotified) return;
    m_closedNotified = true;
    if (m_onClosed) m_onClosed();
}

void CloudConfigSharePopup::onClose(CCObject* sender) {
    this->notifyClosed();
    Popup::onClose(sender);
}

void CloudConfigSharePopup::syncIdPreview() {
    if (!m_idPreview || !m_nameInput) return;
    auto const id = CloudConfigShare::suggestId(m_nameInput->getString());
    m_idPreview->setString(fmt::format("suggested id: {}", id).c_str());
}

void CloudConfigSharePopup::onConfirm(CCObject*) {
    std::string const name = m_nameInput ? m_nameInput->getString() : "";
    std::string const author = m_authorInput ? m_authorInput->getString() : "";
    std::string const desc = m_descInput ? m_descInput->getString() : "";

    if (name.empty()) {
        FLAlertLayer::create("Share", "Please enter a name for the pack.", "OK")->show();
        return;
    }

    auto const shared = CloudConfigShare::shareCurrent(name, author, desc);
    if (!shared.ok) {
        FLAlertLayer::create(
            "Share",
            shared.error.empty() ? "Share failed." : shared.error.c_str(),
            "OK"
        )->show();
        return;
    }

    this->onClose(nullptr);

    std::string publishLine;
    if (shared.published) {
        publishLine = fmt::format(
            "Sent to catalog.\n{}\n"
            "(CDN may take a minute — then Refresh.)\n\n",
            shared.publishUrl.empty() ? "(see GitHub repo)" : shared.publishUrl
        );
    }
    else if (shared.publishAttempted) {
        publishLine = fmt::format(
            "Online submit failed:\n{}\n"
            "Outbox still saved. If you are the maintainer, check the submit Worker / PAT.\n\n",
            shared.publishError.empty() ? "(unknown)" : shared.publishError
        );
    }
    else {
        publishLine =
            "Online submit off — outbox/clipboard only.\n\n";
    }

    auto const msg = fmt::format(
        "Shared \"{}\" (id: {}).\n\n"
        "{}"
        "Outbox:\n{}\n\n"
        "{}\n\n"
        "{}",
        name,
        shared.suggestedId,
        publishLine,
        shared.outboxPath.string(),
        shared.clipboardOk
            ? "Envelope copied to clipboard."
            : "Clipboard copy failed — use the outbox file.",
        CloudConfigShare::discordCommunityUrl()
    );
    FLAlertLayer::create("Share", msg.c_str(), "OK")->show();
}
