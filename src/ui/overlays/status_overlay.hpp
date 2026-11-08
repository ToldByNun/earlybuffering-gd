#ifndef STATUS_OVERLAY_HPP
#define STATUS_OVERLAY_HPP

#include <Geode/Geode.hpp>
#include <string>

class StatusOverlay {
public:
    static void attach(PlayLayer* playLayer);
    static void detach(bool layerDying = false);
    static void update(std::string const& text);
    static void setVisible(bool visible);

private:
    static cocos2d::CCLabelBMFont* s_label;
};

#include "../../config/stage_config.hpp"
#include "../../core/platform/platform_limits.hpp"

class StatusOverlayImplementation {
public:
private:
    friend class StatusOverlay;

    static std::string s_lastText;
    static bool s_lastVisible;

    StatusOverlayImplementation() = delete;
};

#endif
