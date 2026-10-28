#ifndef EARLYBUFFER_UI_SAVEFRAMECOUNTER_HPP
#define EARLYBUFFER_UI_SAVEFRAMECOUNTER_HPP

#include <Geode/Geode.hpp>

// World-space +N frames-saved markers at assisted click positions.
// Markers persist until clear() (death / attempt reset) or detach() (level quit).
class SaveFrameCounter {
public:
    static SaveFrameCounter& get();

    void attach(PlayLayer* layer);
    void detach(bool layerDying);
    void clear();

    // delayTicks = frames held back; color buckets 1..8. No-op until spawn is wired.
    void spawn(PlayLayer* layer, cocos2d::CCPoint worldPos, int delayTicks);

private:
    SaveFrameCounter() = default;

    void ensureRoot(PlayLayer* layer);

    cocos2d::CCNode* m_root = nullptr;
};

#endif // EARLYBUFFER_UI_SAVEFRAMECOUNTER_HPP
