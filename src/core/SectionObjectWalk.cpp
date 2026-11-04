#include "SectionObjectWalk.hpp"

#include <Geode/Geode.hpp>
#include <fmt/format.h>

using namespace geode::prelude;

bool SectionObjectWalk::pointerLooksLive(void const* p) {
    if (!p) return false;
    auto const addr = reinterpret_cast<uintptr_t>(p);
    // Null-page and classic freelist / -1 poison — do not deref or cast.
    if (addr < 0x10000ull) return false;
    if (addr >= ~uintptr_t{0} - 0xFFull) return false; // -1 .. -256
    // High nibble all-ones is a common MSVC debug fill / section hole.
    if ((addr & 0xFFFFFFFF00000000ull) == 0xFFFFFFFF00000000ull) return false;
    return true;
}

bool SectionObjectWalk::pointerAndVtableLive(void const* p) {
    if (!pointerLooksLive(p)) return false;
    // Freed slots often keep a user-looking address but vtable qword is -1.
    // typeinfo_cast then AVs reading @ 0xFFFFFFFFFFFFFFFF (bakefail diag).
    void* const vt = *reinterpret_cast<void* const*>(p);
    return pointerLooksLive(vt);
}

GameObject* SectionObjectWalk::asGameObject(void* raw) {
    if (!pointerAndVtableLive(raw)) return nullptr;
    // void* is not polymorphic — cast through CCObject* then typeinfo_cast.
    auto* asCc = reinterpret_cast<cocos2d::CCObject*>(raw);
    return typeinfo_cast<GameObject*>(asCc);
}

int SectionObjectWalk::forEachInSections(
    GJBaseGameLayer* layer,
    std::function<void(GameObject*)> const& visit
) {
    if (!layer || !visit) return 0;
    int n = 0;
    auto& sections = layer->m_sections;
    size_t const outerN = sections.size();
    for (size_t ox = 0; ox < outerN; ++ox) {
        auto* mid = sections[ox];
        if (!pointerLooksLive(mid)) continue;
        size_t const midN = mid->size();
        for (size_t ix = 0; ix < midN; ++ix) {
            if (ix >= mid->size()) break;
            auto* inner = (*mid)[ix];
            if (!pointerLooksLive(inner)) continue;
            size_t const slotN = inner->size();
            for (size_t s = 0; s < slotN; ++s) {
                if (s >= inner->size()) break;
                auto* raw = (*inner)[s];
                auto* obj = asGameObject(raw);
                if (!obj) continue;
                visit(obj);
                ++n;
            }
        }
    }
    return n;
}

bool SectionObjectWalk::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    expect(!pointerLooksLive(nullptr), "null");
    expect(!pointerLooksLive(reinterpret_cast<void*>(0)), "zero");
    expect(!pointerLooksLive(reinterpret_cast<void*>(0x100ull)), "null-page");
    expect(!pointerLooksLive(reinterpret_cast<void*>(~uintptr_t{0})), "poison-ff");
    expect(!pointerLooksLive(reinterpret_cast<void*>(0xFFFFFFFFFFFFFFFEull)), "poison-fe");
    expect(!pointerLooksLive(reinterpret_cast<void*>(0xFFFFFFFF00001234ull)), "poison-hi");

    // Stack / heap addresses used as “live-looking” for the filter only.
    int stackProbe = 0;
    expect(pointerLooksLive(&stackProbe), "stack-live");
    // Positive vtable case: one-slot fake whose first qword is a live address.
    // Do not call pointerAndVtableLive(&stackProbe) — that reads past the int.
    void* fakeVtLive[1] = { &stackProbe };
    expect(pointerAndVtableLive(fakeVtLive), "vt-live");

    expect(asGameObject(nullptr) == nullptr, "as-null");
    expect(asGameObject(reinterpret_cast<void*>(~uintptr_t{0})) == nullptr, "as-poison");
    expect(!pointerAndVtableLive(nullptr), "vt-null");

    // Fake object whose "vtable" qword is -1 must not enter typeinfo_cast.
    void* fakeVtPoison[1] = { reinterpret_cast<void*>(~uintptr_t{0}) };
    expect(asGameObject(fakeVtPoison) == nullptr, "as-vt-poison");
    expect(!pointerAndVtableLive(fakeVtPoison), "vt-poison");

    // No layer → zero visits, no crash.
    expect(forEachInSections(nullptr, [](GameObject*) {}) == 0, "no-layer");

    if (fails == 0) {
        report = "SectionObjectWalk selfCheck OK";
        return true;
    }
    report = fmt::format("SectionObjectWalk selfCheck {} fails: {}", fails, report);
    return false;
}
