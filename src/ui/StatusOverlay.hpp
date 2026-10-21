#pragma once

#include <Geode/Geode.hpp>
#include <string>

namespace frire {

class StatusOverlay {
public:
    static void attach(PlayLayer* playLayer);
    static void detach(bool layerDying = false);
    static void update(std::string const& text);
    static void setVisible(bool visible);

private:
    static cocos2d::CCLabelBMFont* s_label;
};

}
