#ifndef SHADOW_SECTION_SAFETY_HPP
#define SHADOW_SECTION_SAFETY_HPP

#include "../shadow_simulator.hpp"

class EbShadowSectionSafety {
public:
    static bool tryRemoveFromSection(GJBaseGameLayer* layer, GameObject* object);
    static void scrubOwnedGhostsFromSections(GJBaseGameLayer* layer, EbShadowSimulator* self);
    static void evacuateGhosts(EbShadowSimulator& simulator, GJBaseGameLayer* layer);

private:
    EbShadowSectionSafety() = delete;
};

#endif
