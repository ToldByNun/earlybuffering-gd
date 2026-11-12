#ifndef EB_SECTION_CARD_HPP
#define EB_SECTION_CARD_HPP

// Static section chrome for the desktop ImGui settings content host.
// UI-only — no SettingV3 store, not collapsible. Pair begin/end like ImGui Begin/End.
// Auto-height child card with inner padding; tab host still owns scrolling.
class EbSectionCard {
public:
    // Always draws the titled card body. Returns false only on bad id/title.
    // Always call end() after a successful begin().
    static bool begin(char const* id, char const* title);
    static void end();

private:
    EbSectionCard() = delete;
};

#endif
