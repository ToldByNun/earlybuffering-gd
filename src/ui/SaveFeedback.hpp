#ifndef EARLYBUFFER_UI_SAVEFEEDBACK_HPP
#define EARLYBUFFER_UI_SAVEFEEDBACK_HPP

#include <Geode/Geode.hpp>

// Plays per-tick save cue sounds and/or flashes the player icon color.
class SaveFeedback {
public:
    static SaveFeedback& get();

    // Call when assist injects a delayed click (delayTicks = ticks held back).
    void onSave(PlayLayer* playLayer, PlayerObject* player, int delayTicks);

    // Restore icon flash after a few physics ticks.
    void onPhysicsTick();

    void reset();

private:
    SaveFeedback() = default;

    void playSound(int delayTicks);
    void flashIcon(PlayerObject* player);

    PlayerObject* m_flashPlayer = nullptr;
    cocos2d::ccColor3B m_savedColor1{ 255, 255, 255 };
    cocos2d::ccColor3B m_savedColor2{ 255, 255, 255 };
    cocos2d::ccColor3B m_savedGlow{ 255, 255, 255 };
    bool m_hadGlow = false;
    bool m_flashing = false;
    int m_flashTicksLeft = 0;
};

#endif
