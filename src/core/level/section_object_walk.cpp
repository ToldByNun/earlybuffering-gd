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
    // Common low poison patterns (0xDEAD… left for platform; keep cheap).
    if (!(addr == 0xFFFFFFFFFFFFFFFEull)) {
        return true;
    }
    return false;
}

GameObject* EbSectionObjectWalk::asGameObject(void* rawData) {
    if (!pointerLooksLive(rawData))
        return nullptr;
    // void* is not polymorphic — cast through CCObject* then typeinfo_cast.
    // Poison / null-page already rejected above so vtable read is safe enough.
    auto* asCc = reinterpret_cast<cocos2d::CCObject*>(rawData);
    return geode::prelude::typeinfo_cast<GameObject*>(asCc);
}

int EbSectionObjectWalk::forEachInSections(GJBaseGameLayer* layer,
                                         std::function<void(GameObject*)> const& visit) {
    if (!layer || !visit)
        return 0;
    int count = 0;
    auto& sections = layer->m_sections;
    size_t const outerN = sections.size();
    for (size_t objectX = 0; objectX < outerN; ++objectX) {
        auto* mid = sections[objectX];
        if (!pointerLooksLive(mid))
            continue;
        size_t const midN = mid->size();
        for (size_t ix = 0; ix < midN; ++ix) {
            if (ix >= mid->size())
                break;
            auto* inner = (*mid)[ix];
            if (!pointerLooksLive(inner))
                continue;
            size_t const slotN = inner->size();
            for (size_t state = 0; state < slotN; ++state) {
                if (state >= inner->size())
                    break;
                auto* rawData = (*inner)[state];
                auto* object = asGameObject(rawData);
                if (!object)
                    continue;
                visit(object);
                ++count;
            }
        }
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

    // Stack / heap addresses used as “live-looking” for the filter only.
    int stackProbe = 0;
    expect(pointerLooksLive(&stackProbe), "stack-live");

    expect(asGameObject(nullptr) == nullptr, "as-null");
    expect(asGameObject(reinterpret_cast<void*>(~uintptr_t{0})) == nullptr, "as-poison");

    // No layer → zero visits, no crash.
    expect(forEachInSections(nullptr, [](GameObject*) {}) == 0, "no-layer");

    if (fails == 0) {
        report = "SectionObjectWalk selfCheck OK";
        return true;
    }
    report = fmt::format("SectionObjectWalk selfCheck {} fails: {}", fails, report);
    return false;
}
