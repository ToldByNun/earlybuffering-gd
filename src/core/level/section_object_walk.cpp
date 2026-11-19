#include "section_object_walk.hpp"
#include <Geode/Geode.hpp>
#include <fmt/format.h>

bool EbSectionObjectWalk::pointerLooksLive(void const* valuePointer) {
    if (!valuePointer)
        return false;
    auto const addr = reinterpret_cast<uintptr_t>(valuePointer);
    // Null-page and classic freelist / -1 poison — do not deref or cast.
    if (addr < 0x10000ull)
        return false;
    if (addr == ~uintptr_t{0})
        return false;
    if (addr == 0xFFFFFFFFFFFFFFFEull)
        return false;
    // Reject non-canonical / non-user addresses. Section slots can hold tagged
    // garbage (e.g. 0x9…); typeinfo_cast then AVs reading vtable @ -1.
    constexpr uintptr_t kUserCanonicalMax = 0x00007FFFFFFFFFFFull;
    if (addr > kUserCanonicalMax)
        return false;
    return true;
}

GameObject* EbSectionObjectWalk::asGameObject(cocos2d::CCObject* object) {
    if (!pointerLooksLive(object))
        return nullptr;
    return geode::prelude::typeinfo_cast<GameObject*>(object);
}

cocos2d::CCArray* EbSectionObjectWalk::objectArray(GJBaseGameLayer* layer) {
    if (!layer)
        return nullptr;
    if (layer->m_objects)
        return layer->m_objects;
    return layer->m_objectLayer ? layer->m_objectLayer->getChildren() : nullptr;
}

int EbSectionObjectWalk::forEachOwnedObject(GJBaseGameLayer* layer,
                                            std::function<void(GameObject*)> const& visit) {
    if (!visit)
        return 0;
    auto* objects = objectArray(layer);
    if (!objects)
        return 0;
    int count = 0;
    for (unsigned index = 0; index < objects->count(); ++index) {
        auto* object = asGameObject(objects->objectAtIndex(index));
        if (!object)
            continue;
        visit(object);
        ++count;
    }
    return count;
}

bool EbSectionObjectWalk::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok)
            return;
        ++fails;
        if (!report.empty())
            report += "; ";
        report += name;
    };

    expect(!pointerLooksLive(nullptr), "null");
    expect(!pointerLooksLive(reinterpret_cast<void*>(0)), "zero");
    expect(!pointerLooksLive(reinterpret_cast<void*>(0x100ull)), "null-page");
    expect(!pointerLooksLive(reinterpret_cast<void*>(~uintptr_t{0})), "poison-ff");
    expect(!pointerLooksLive(reinterpret_cast<void*>(0xFFFFFFFFFFFFFFFEull)), "poison-fe");
    expect(!pointerLooksLive(reinterpret_cast<void*>(0x90037d0012ab6a0eull)), "non-canonical");

    // Stack / heap addresses used as “live-looking” for the filter only.
    int stackProbe = 0;
    expect(pointerLooksLive(&stackProbe), "stack-live");

    expect(asGameObject(nullptr) == nullptr, "as-null");
    expect(asGameObject(reinterpret_cast<cocos2d::CCObject*>(~uintptr_t{0})) == nullptr,
           "as-poison");

    // No layer → zero visits, no crash.
    expect(forEachOwnedObject(nullptr, [](GameObject*) {}) == 0, "no-layer");

    if (fails == 0) {
        report = "SectionObjectWalk selfCheck OK";
        return true;
    }
    report = fmt::format("SectionObjectWalk selfCheck {} fails: {}", fails, report);
    return false;
}
