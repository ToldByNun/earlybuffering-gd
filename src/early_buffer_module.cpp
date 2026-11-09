#include "early_buffer_module.hpp"
#include "app/class_manager.hpp"
#include <Geode/Geode.hpp>

void EbEarlyBufferModule::onLoaded() {
    EbClassManager::get().initialize();
}

$on_mod(Loaded) {
    EbEarlyBufferModule::onLoaded();
}
