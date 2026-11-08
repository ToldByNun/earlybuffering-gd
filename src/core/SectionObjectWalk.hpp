#ifndef EARLYBUFFER_CORE_SECTIONOBJECTWALK_HPP
#define EARLYBUFFER_CORE_SECTIONOBJECTWALK_HPP

#include <cstdint>
#include <functional>
#include <string>

class GameObject;
class GJBaseGameLayer;

// Poison-safe walk of GJBaseGameLayer::m_sections.
// Section slots can hold freed / 0xFFF… entries — never typeinfo_cast those.
class SectionObjectWalk {
public:
    // Reject null, null-page, and classic poison (~0) without dereferencing.
    static bool pointerLooksLive(void const* p);

    // Live check then typeinfo_cast; nullptr if unsafe or wrong type.
    static GameObject* asGameObject(void* raw);

    // Visit every safely cast GameObject* in m_sections (outer→mid→inner).
    // Returns how many objects were handed to visit. Never throws on poison.
    static int forEachInSections(
        GJBaseGameLayer* layer,
        std::function<void(GameObject*)> const& visit
    );

    static bool selfCheck(std::string& report);

private:
    SectionObjectWalk() = delete;
};

#endif // EARLYBUFFER_CORE_SECTIONOBJECTWALK_HPP
