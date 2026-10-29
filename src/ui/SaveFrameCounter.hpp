#ifndef EARLYBUFFER_UI_SAVEFRAMECOUNTER_HPP
#define EARLYBUFFER_UI_SAVEFRAMECOUNTER_HPP

#include <Geode/Geode.hpp>

// World-space +N markers at assisted click positions (N = physics ticks held).
// Markers persist until clear() (death / attempt reset) or detach() (level quit).
class SaveFrameCounter {
public:
    static constexpr int kMaxMarkers = 64;

    static SaveFrameCounter& get();

    void attach(PlayLayer* layer);
    void detach(bool layerDying);
    void clear();

    // delayTicks = physics ticks held back (not 60fps frames). Bucket/label 1..8.
    void spawn(PlayLayer* layer, cocos2d::CCPoint worldPos, int delayTicks);

    // Exposed for selfCheck / tests.
    static int bucketFrames(int delayTicks);
    static cocos2d::ccColor3B colorForBucket(int bucket);

private:
    static constexpr float kRingRadius = 8.f;
    static constexpr float kRingBorder = 1.8f;
    static constexpr float kOutlineBorder = 1.25f;
    static constexpr float kOutlinePad = 1.35f;
    static constexpr float kLabelScale = 0.34f;
    static constexpr float kLabelGap = 5.f;
    static constexpr float kLabelOutlinePx = 1.6f;
    static constexpr unsigned kRingSegments = 32;

    SaveFrameCounter() = default;

    void ensureRoot(PlayLayer* layer);
    void dropOldestIfOverCap();
    static cocos2d::ccColor4F to4F(cocos2d::ccColor3B c, float a);
    static cocos2d::CCNode* makeMarker(cocos2d::CCPoint worldPos, int delayTicks);
    static void addOutlinedLabel(
        cocos2d::CCNode* parent,
        char const* text,
        cocos2d::ccColor3B color,
        float x,
        float y
    );

    cocos2d::CCNode* m_root = nullptr;
};

#endif // EARLYBUFFER_UI_SAVEFRAMECOUNTER_HPP
