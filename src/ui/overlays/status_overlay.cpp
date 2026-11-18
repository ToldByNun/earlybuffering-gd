#include "status_overlay.hpp"
#include "../../config/stage_config.hpp"
#include "../../core/platform/platform_limits.hpp"
#include <algorithm>
#include <cmath>
#include <string>

cocos2d::CCLabelBMFont* EbStatusOverlay::s_label = nullptr;

std::string EbStatusOverlayImplementation::s_lastText;
bool EbStatusOverlayImplementation::s_lastVisible = false;

std::string EbStatusOverlayImplementation::wrapLines(std::string const& text, float maxWidth,
                                                    float scale) {
    if (text.empty() || scale <= 0.f)
        return text;

    // chatFont advance is wider than a tight ASCII guess; keep lines short so
    // the overlay always reads as multiple rows instead of one clipped strip.
    float const charWidth = std::max(5.f, 11.f * scale);
    int widthChars = maxWidth > 8.f ? static_cast<int>(maxWidth / charWidth) : 48;
    int const maxChars = std::clamp(widthChars, 28, 56);
    constexpr int kMaxTokensPerLine = 4;

    std::string normalized = text;
    for (size_t at = 0; (at = normalized.find(" || ", at)) != std::string::npos;) {
        normalized.replace(at, 4, "\n");
        ++at;
    }

    std::string out;
    std::string line;
    int tokensOnLine = 0;
    size_t index = 0;
    auto flushLine = [&]() {
        if (line.empty())
            return;
        if (!out.empty())
            out.push_back('\n');
        out += line;
        line.clear();
        tokensOnLine = 0;
    };

    while (index < normalized.size()) {
        if (normalized[index] == '\n') {
            flushLine();
            ++index;
            continue;
        }
        size_t next = normalized.find(" | ", index);
        size_t end = next == std::string::npos ? normalized.size() : next;
        size_t nl = normalized.find('\n', index);
        if (nl != std::string::npos && nl < end)
            end = nl;
        std::string token = normalized.substr(index, end - index);
        if (token.empty()) {
            index = end == normalized.size() ? end : end + (normalized[end] == '\n' ? 1 : 3);
            continue;
        }

        int const growth = line.empty() ? static_cast<int>(token.size())
                                        : static_cast<int>(3 + token.size());
        bool const wouldOverflow =
            !line.empty() && (static_cast<int>(line.size()) + growth > maxChars ||
                              tokensOnLine >= kMaxTokensPerLine);
        if (wouldOverflow)
            flushLine();
        if (line.empty())
            line = token;
        else {
            line += " | ";
            line += token;
        }
        ++tokensOnLine;

        if (end < normalized.size() && normalized[end] == '\n') {
            flushLine();
            index = end + 1;
            continue;
        }
        index = next == std::string::npos ? normalized.size() : next + 3;
    }
    flushLine();
    return out.empty() ? text : out;
}

void EbStatusOverlay::attach(PlayLayer* playLayer) {
    detach();
    if (!playLayer)
        return;

    s_label = geode::prelude::CCLabelBMFont::create("EarlyBuffer", "chatFont.fnt");
    if (!s_label) {
        s_label = geode::prelude::CCLabelBMFont::create("EarlyBuffer", "bigFont.fnt");
    }
    if (!s_label)
        return;

    s_label->setScale(0.40f);
    s_label->setAnchorPoint({0.f, 1.f});
    s_label->setOpacity(220);
    s_label->setAlignment(geode::prelude::kCCTextAlignmentLeft);
    s_label->setID("earlybuffer-status-overlay"_spr);

    auto const winSize = geode::prelude::CCDirector::sharedDirector()->getWinSize();
    auto& limits = EbPlatformLimits::get();
    float const left = limits.overlayLeftInset();
    float const top = limits.overlayTopInset();
    s_label->setPosition({left, winSize.height - top});

    EbStatusOverlayImplementation::s_lastText = "EarlyBuffer";
    EbStatusOverlayImplementation::s_lastVisible = EbStageConfig::showOverlay();
    s_label->setVisible(EbStatusOverlayImplementation::s_lastVisible);

    playLayer->addChild(s_label, 1000);
    s_label->retain();
}

void EbStatusOverlay::detach(bool layerDying) {
    if (!s_label) {
        EbStatusOverlayImplementation::s_lastText.clear();
        return;
    }
    if (layerDying) {
        s_label->release();
        s_label = nullptr;
        EbStatusOverlayImplementation::s_lastText.clear();
        return;
    }
    if (s_label->getParent()) {
        s_label->removeFromParent();
    }
    s_label->release();
    s_label = nullptr;
    EbStatusOverlayImplementation::s_lastText.clear();
}

void EbStatusOverlay::update(std::string const& text) {
    if (!s_label)
        return;
    bool const vis = EbStageConfig::showOverlay();
    if (vis != EbStatusOverlayImplementation::s_lastVisible) {
        EbStatusOverlayImplementation::s_lastVisible = vis;
        s_label->setVisible(vis);
    }
    if (!vis || text == EbStatusOverlayImplementation::s_lastText)
        return;
    EbStatusOverlayImplementation::s_lastText = text;
    auto const winSize = geode::prelude::CCDirector::sharedDirector()->getWinSize();
    auto& limits = EbPlatformLimits::get();
    float const maxWidth = std::max(120.f, winSize.width - limits.overlayLeftInset() - 16.f);
    auto const wrapped =
        EbStatusOverlayImplementation::wrapLines(text, maxWidth, s_label->getScale());
    s_label->setString(wrapped.c_str());
}

void EbStatusOverlay::setVisible(bool visible) {
    if (!s_label)
        return;
    bool const vis = visible && EbStageConfig::showOverlay();
    if (vis != EbStatusOverlayImplementation::s_lastVisible) {
        EbStatusOverlayImplementation::s_lastVisible = vis;
        s_label->setVisible(vis);
    }
}
