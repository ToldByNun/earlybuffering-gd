#include "cloud_config_share_popup.hpp"
#include "../../cloud/sharing/cloud_config_share.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/Layout.hpp>

CloudConfigSharePopup* CloudConfigSharePopup::create(ClosedCallback onClosed) {
    auto* createdObject = new CloudConfigSharePopup();
    if (createdObject->init(std::move(onClosed))) {
        createdObject->autorelease();
        return createdObject;
    }
    delete createdObject;
    return nullptr;
}

void CloudConfigSharePopup::open(ClosedCallback onClosed) {
    if (auto* p = create(std::move(onClosed))) {
        p->show();
    }
}

bool CloudConfigSharePopup::init(ClosedCallback onClosed) {
    m_onClosed = std::move(onClosed);

    // Compact height — fields stacked tightly, no big empty band.
    if (!geode::prelude::Popup::init(300.f, 236.f, "GJ_square01.png")) {
        return false;
    }

    if (m_closeBtn) {
        m_closeBtn->setVisible(false);
        m_closeBtn->setEnabled(false);
    }

    this->setTitle("Share Config", "goldFont.fnt", 0.60f, 10.f);

    // One left edge for labels + inputs. No setScale on TextInput (that
    // shrinks the visual box while position still uses full width → staircase).
    float const padX = 24.f;
    float const innerW = 252.f;
    float const gapX = 8.f;
    float const halfW = (innerW - gapX) * 0.5f;

    auto placeLabel = [&](char const* text, float left, float y) {
        auto* lab = geode::prelude::CCLabelBMFont::create(text, "goldFont.fnt");
        lab->setScale(0.32f);
        lab->setAnchorPoint({0.f, 0.5f});
        lab->setOpacity(210);
        m_mainLayer->addChildAtPosition(lab, geode::Anchor::TopLeft, ccp(left, y));
    };

    auto placeInput = [&](geode::prelude::TextInput* input, float left, float width, float y) {
        // TopLeft + center-x of the box → left edge == `left` when anchor is 0.5.
        m_mainLayer->addChildAtPosition(input, geode::Anchor::TopLeft, ccp(left + width * 0.5f, y));
    };

    auto makeInput = [&](float width, char const* placeholder, size_t maxChars) {
        auto* input = geode::prelude::TextInput::create(width, placeholder, "bigFont.fnt");
        input->setCommonFilter(geode::prelude::CommonFilter::Any);
        input->setTextAlign(geode::prelude::TextInputAlign::Left);
        input->setMaxCharCount(maxChars);
        return input;
    };

    // Vertical rhythm (from title): label → input → gap → …
    float const nameLabY = -36.f;
    float const nameInY = -50.f;
    float const descLabY = -72.f;
    float const descInY = -86.f;
    float const tagsLabY = -108.f;
    float const tagsInY = -122.f;
    float const idY = -144.f;

    // Name | Author
    placeLabel("Name", padX, nameLabY);
    placeLabel("Author", padX + halfW + gapX, nameLabY);

    m_nameInput = makeInput(halfW, "My pack", 48);
    m_nameInput->setID("cloud-share-name"_spr);
    placeInput(m_nameInput, padX, halfW, nameInY);

    m_authorInput = makeInput(halfW, "Your name", 32);
    m_authorInput->setID("cloud-share-author"_spr);
    placeInput(m_authorInput, padX + halfW + gapX, halfW, nameInY);

    // Description
    placeLabel("Description", padX, descLabY);
    m_descInput = makeInput(innerW, "What does this pack do?", 160);
    m_descInput->setID("cloud-share-desc"_spr);
    placeInput(m_descInput, padX, innerW, descInY);

    // Tags
    placeLabel("Tags", padX, tagsLabY);
    m_tagsInput = makeInput(innerW, "safe, live, beginner", 96);
    m_tagsInput->setID("cloud-share-tags"_spr);
    placeInput(m_tagsInput, padX, innerW, tagsInY);

    m_nameInput->setString("Shared Config");
    m_nameInput->setCallback([this](std::string const&) { this->syncIdPreview(); });

    m_idPreview = geode::prelude::CCLabelBMFont::create("id: shared-config", "chatFont.fnt");
    m_idPreview->setScale(0.42f);
    m_idPreview->setOpacity(200);
    m_idPreview->setAnchorPoint({0.5f, 0.5f});
    m_idPreview->setID("cloud-share-id"_spr);
    m_mainLayer->addChildAtPosition(m_idPreview, geode::Anchor::Top, ccp(0.f, idY));
    this->syncIdPreview();

    auto* menu = geode::prelude::CCMenu::create();
    menu->setContentSize({260.f, 28.f});
    menu->setID("cloud-share-menu"_spr);
    m_mainLayer->addChildAtPosition(menu, geode::Anchor::Bottom, ccp(0.f, 20.f));
    menu->setLayout(geode::prelude::RowLayout::create()->setGap(12.f)->setAxisAlignment(
        geode::prelude::AxisAlignment::Center));

    auto* cancelSpr = ButtonSprite::create("Cancel", "bigFont.fnt", "GJ_button_06.png", 0.8f);
    cancelSpr->setScale(0.44f);
    menu->addChild(CCMenuItemSpriteExtra::create(cancelSpr, this,
                                                 menu_selector(CloudConfigSharePopup::onClose)));

    auto* okSpr = ButtonSprite::create("Share", "bigFont.fnt", "GJ_button_01.png", 0.8f);
    okSpr->setScale(0.44f);
    menu->addChild(CCMenuItemSpriteExtra::create(okSpr, this,
                                                 menu_selector(CloudConfigSharePopup::onConfirm)));
    menu->updateLayout();

    return true;
}

void CloudConfigSharePopup::notifyClosed() {
    if (m_closedNotified)
        return;
    m_closedNotified = true;
    if (m_onClosed)
        m_onClosed();
}

void CloudConfigSharePopup::onClose(geode::prelude::CCObject* sender) {
    this->notifyClosed();
    geode::prelude::Popup::onClose(sender);
}

void CloudConfigSharePopup::syncIdPreview() {
    if (!m_idPreview || !m_nameInput)
        return;
    auto const id = CloudConfigShare::suggestId(m_nameInput->getString());
    m_idPreview->setString(fmt::format("id: {}", id).c_str());
}

void CloudConfigSharePopup::onConfirm(geode::prelude::CCObject*) {
    std::string const name = m_nameInput ? m_nameInput->getString() : "";
    std::string const author = m_authorInput ? m_authorInput->getString() : "";
    std::string const desc = m_descInput ? m_descInput->getString() : "";
    std::string const tagsRaw = m_tagsInput ? m_tagsInput->getString() : "";

    if (name.empty()) {
        FLAlertLayer::create("Share", "Please enter a name for the pack.", "OK")->show();
        return;
    }

    auto const tags = CloudConfigShare::parseTags(tagsRaw);
    auto const shared = CloudConfigShare::shareCurrent(name, author, desc, tags);
    if (!shared.ok) {
        FLAlertLayer::create("Share", shared.error.empty() ? "Share failed." : shared.error.c_str(),
                             "OK")
            ->show();
        return;
    }

    this->onClose(nullptr);

    std::string publishLine;
    if (shared.published) {
        publishLine =
            fmt::format("Sent to catalog.\n{}\n"
                        "(CDN may take a minute — then Refresh.)\n\n",
                        shared.publishUrl.empty() ? "(see GitHub repo)" : shared.publishUrl);
    } else if (shared.publishAttempted) {
        publishLine = fmt::format("Online submit failed:\n{}\n"
                                  "Outbox still saved.\n\n",
                                  shared.publishError.empty() ? "(unknown)" : shared.publishError);
    } else {
        publishLine = "Online submit off — outbox/clipboard only.\n\n";
    }

    std::string tagsLine;
    if (!shared.tags.empty()) {
        tagsLine = "Tags: ";
        for (size_t index = 0; index < shared.tags.size(); ++index) {
            if (index)
                tagsLine += ", ";
            tagsLine += shared.tags[index];
        }
        tagsLine += "\n\n";
    }

    auto const message =
        fmt::format("Shared \"{}\" (id: {}).\n\n"
                    "{}"
                    "{}"
                    "Outbox:\n{}\n\n"
                    "{}\n\n"
                    "{}",
                    name, shared.suggestedId, tagsLine, publishLine, shared.outboxPath.string(),
                    shared.clipboardOk ? "Envelope copied to clipboard."
                                       : "Clipboard copy failed — use the outbox file.",
                    CloudConfigShare::discordCommunityUrl());
    FLAlertLayer::create("Share", message.c_str(), "OK")->show();
}
