#include "early_buffer_module.hpp"
#include "app/class_manager.hpp"
#include "ui/desktop_menu/desktop_menu.hpp"
#include <Geode/Geode.hpp>

void EbEarlyBufferModule::onLoaded() {
    EbClassManager::get().initialize();
    EbDesktopMenu::install();
}

$on_mod(Loaded) {
    EbEarlyBufferModule::onLoaded();
}
