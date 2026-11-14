#include "test_trigger_motion.hpp"
#include "../src/core/trigger_world/trigger_motion_anchor.hpp"
#include <cmath>
#include <iostream>

int TriggerMotionRegression::run() {
    int failures = 0;
    auto expect = [&](bool condition) { failures += !condition; };
    EbTriggerPose reference{100, 50.f, 20.f, 45.f, 2.f, 2.f, true};
    EbTriggerPose live{100, 350.f, 120.f, 90.f, 3.f, 4.f, false};
    auto atCapture = EbTriggerMotionAnchor::project(live, reference, reference);
    expect(atCapture.x == 350.f && atCapture.y == 120.f);
    expect(atCapture.rotation == 90.f && atCapture.scaleX == 3.f);
    expect(!atCapture.active);
    // Prior movement remains present while a new relative movement progresses.
    auto future = reference;
    future.tick = 110;
    future.x += 20.f;
    future.y -= 10.f;
    future.rotation += 30.f;
    future.scaleX *= 2.f;
    auto moved = EbTriggerMotionAnchor::project(live, reference, future);
    expect(moved.x == 370.f && moved.y == 110.f);
    expect(moved.rotation == 120.f && moved.scaleX == 6.f && moved.scaleY == 4.f);
    expect(moved.tick == 110 && !moved.active);
    // Completed earlier motion contributes no additional motion after capture.
    auto held = EbTriggerMotionAnchor::project(live, future, future);
    expect(held.x == live.x && held.rotation == live.rotation);
    // Historical visibility survives until a later track change.
    reference.active = false;
    future.active = true;
    expect(EbTriggerMotionAnchor::project(live, reference, future).active);
    // Zero-scale tracks stay finite.
    reference.scaleX = 0.f;
    future.scaleX = 1.f;
    expect(std::isfinite(EbTriggerMotionAnchor::project(live, reference, future).scaleX));
    // Returning to the reference key removes only the new displacement.
    auto returned = EbTriggerMotionAnchor::project(live, reference, reference);
    expect(returned.x == live.x && returned.y == live.y);
    std::cout << "Trigger motion regression: " << failures << " failures" << '\n';
    return failures ? 1 : 0;
}

int main() {
    return TriggerMotionRegression::run();
}
