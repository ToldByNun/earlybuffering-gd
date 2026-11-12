#include "desktop_menu_motion.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include <cmath>
#include <cstdint>

class EbDesktopMenuMotionStore {
public:
    // Dense pages (Assist/Dev) use several slots per widget; keep headroom so
    // scroll-away reclaim does not restart fills from 0.
    static constexpr int kSlots = 512;

    class Slot {
    public:
        ImGuiID id = 0;
        float value = 0.0f;
        uint32_t frame = 0;

    private:
    };

    static Slot* slots() {
        static Slot values[kSlots];
        return values;
    }

    static uint32_t& frame() {
        static uint32_t value = 1;
        return value;
    }

    static float clampf(float value, float lo, float hi) {
        if (value < lo) {
            return lo;
        }
        if (value > hi) {
            return hi;
        }
        return value;
    }

    // seedValue: first frame / reclaim snaps to current target so scrolled-off
    // widgets do not replay a 0→target intro when they come back into view.
    static Slot* findOrAlloc(ImGuiID id, float seedValue) {
        Slot* list = slots();
        Slot* freeSlot = nullptr;
        Slot* oldest = &list[0];
        for (int index = 0; index < kSlots; ++index) {
            Slot& slot = list[index];
            if (slot.id == id) {
                return &slot;
            }
            if (slot.id == 0 && !freeSlot) {
                freeSlot = &slot;
            }
            if (slot.frame < oldest->frame) {
                oldest = &slot;
            }
        }
        Slot* slot = freeSlot ? freeSlot : oldest;
        slot->id = id;
        slot->value = seedValue;
        slot->frame = frame();
        return slot;
    }

private:
    EbDesktopMenuMotionStore() = delete;
};

float EbDesktopMenuMotion::approach(float current, float target, float tau, float deltaTime) {
    if (tau <= 0.0001f) {
        return target;
    }
    float const dt = EbDesktopMenuMotionStore::clampf(deltaTime, 0.0f, 1.0f / 20.0f);
    float const blend = 1.0f - std::exp(-dt / tau);
    float const next = current + (target - current) * blend;
    if (std::fabs(target - next) < 0.0005f) {
        return target;
    }
    return next;
}

ImVec2 EbDesktopMenuMotion::approach2(ImVec2 const& current, ImVec2 const& target, float tau,
                                      float deltaTime) {
    return ImVec2(approach(current.x, target.x, tau, deltaTime),
                  approach(current.y, target.y, tau, deltaTime));
}

ImVec4 EbDesktopMenuMotion::lerp4(ImVec4 const& from, ImVec4 const& to, float t) {
    float const x = EbDesktopMenuMotionStore::clampf(t, 0.0f, 1.0f);
    return ImVec4(from.x + (to.x - from.x) * x, from.y + (to.y - from.y) * x,
                  from.z + (to.z - from.z) * x, from.w + (to.w - from.w) * x);
}

void EbDesktopMenuMotion::beginFrame() {
    uint32_t& frame = EbDesktopMenuMotionStore::frame();
    frame += 1;
    if (frame == 0) {
        frame = 1;
    }
    EbDesktopMenuMotionStore::Slot* list = EbDesktopMenuMotionStore::slots();
    for (int index = 0; index < EbDesktopMenuMotionStore::kSlots; ++index) {
        EbDesktopMenuMotionStore::Slot& slot = list[index];
        if (slot.id != 0 && frame - slot.frame > 240) {
            slot.id = 0;
            slot.value = 0.0f;
            slot.frame = 0;
        }
    }
}

float EbDesktopMenuMotion::smoothToward(ImGuiID id, bool active, float tau) {
    return smoothFloat(id, active ? 1.0f : 0.0f, tau);
}

float EbDesktopMenuMotion::smoothFloat(ImGuiID id, float target, float tau) {
    if (id == 0) {
        return target;
    }
    EbDesktopMenuMotionStore::Slot* slot = EbDesktopMenuMotionStore::findOrAlloc(id, target);
    slot->frame = EbDesktopMenuMotionStore::frame();
    float const dt = ImGui::GetIO().DeltaTime;
    slot->value = approach(slot->value, target, tau, dt);
    return slot->value;
}

#endif
