#ifndef MENU_LAYER_HOOKS_HPP
#define MENU_LAYER_HOOKS_HPP

#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>

// APR2 auto-driver: fire once when the main menu is actually up (survives loading scenes).
class $modify(EbMenuLayer, MenuLayer) {
public:
    bool init();
};

#endif
