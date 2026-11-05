#pragma once

class PlayLayer;
class GJBaseGameLayer;

// Armed explicitly in the pause menu. Each restart begins a separate path.
namespace LiveRouteCapture {
    void arm(PlayLayer* layer);
    bool armed();
    bool recording(GJBaseGameLayer* layer);
    void resetAttempt(PlayLayer* layer);
    void sample(PlayLayer* layer);
    void complete(PlayLayer* layer);
    void cancel();
}
