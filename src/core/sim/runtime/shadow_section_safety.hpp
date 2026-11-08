#ifndef SHADOW_SECTION_SAFETY_HPP
#define SHADOW_SECTION_SAFETY_HPP

#include "../shadow_simulator.hpp"

class ShadowSectionSafety {
public:
    static bool tryRemoveFromSection(GJBaseGameLayer* layer, GameObject* object);
    static void scrubOwnedGhostsFromSections(GJBaseGameLayer* layer, ShadowSimulator* self);
    static void evacuateGhosts(ShadowSimulator& simulator, GJBaseGameLayer* layer);

private:
    ShadowSectionSafety() = delete;
};

#endif
