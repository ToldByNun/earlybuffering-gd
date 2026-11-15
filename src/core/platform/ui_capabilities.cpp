#include "ui_capabilities.hpp"
EbUiCapabilities::Platform EbUiCapabilities::platform() {
#if defined(EB_UI_WINDOWS)
    return Platform::Windows;
#elif defined(EB_UI_MACOS)
    return Platform::MacOS;
#elif defined(EB_UI_ANDROID)
    return Platform::Android;
#elif defined(EB_UI_IOS)
    return Platform::IOS;
#else
    return Platform::Unknown;
#endif
}
bool EbUiCapabilities::desktopLayout() {
    return platform() == Platform::Windows || platform() == Platform::MacOS;
}
bool EbUiCapabilities::mobileLayout() {
    return platform() == Platform::Android || platform() == Platform::IOS;
}
bool EbUiCapabilities::keybindUI() {
    return desktopLayout();
}
bool EbUiCapabilities::classicMaterial() {
    return desktopLayout() || mobileLayout();
}
bool EbUiCapabilities::backgroundDimming() {
    return classicMaterial();
}
bool EbUiCapabilities::expensiveMaterials() {
#if defined(EB_DESKTOP_MATERIALS)
    return desktopLayout();
#else
    return false;
#endif
}
