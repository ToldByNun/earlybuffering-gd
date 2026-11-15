#ifndef UI_CAPABILITIES_HPP
#define UI_CAPABILITIES_HPP
class EbUiCapabilities {
public:
    enum class Platform { Unknown, Windows, MacOS, Android, IOS };
    static Platform platform();
    static bool desktopLayout();
    static bool mobileLayout();
    static bool keybindUI();
    static bool classicMaterial();
    static bool backgroundDimming();
    static bool expensiveMaterials();

private:
};
#endif
