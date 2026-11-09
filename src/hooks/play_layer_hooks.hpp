#ifndef PLAY_LAYER_HOOKS_HPP
#define PLAY_LAYER_HOOKS_HPP

#include <Geode/Geode.hpp>
#include <Geode/modify/GJGameLevel.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <fmt/format.h>

class $modify(EbPlayLayer, PlayLayer) {
public:
    static void onModify(auto& self) {
        (void)self.setHookPriority("PlayLayer::destroyPlayer", geode::prelude::Priority::First);
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreatePlayer);

    void resetLevel();

    void onQuit();

    // Do NOT hook onExit → onLevelQuit. GD can call onExit around death/retry
    // UI without leaving the level; that tore down ghosts and killed assist
    // until a full re-enter. Quit cleanup stays in onQuit only.

    void destroyPlayer(PlayerObject* player, GameObject* object);

    void playEndAnimationToPos(cocos2d::CCPoint position);

    void playGravityEffect(bool flip);

    void spawnCircle();

    void levelComplete();

    void showNewBest(bool newReward, int orbs, int diamonds, bool demonKey, bool noRetry,
                     bool noTitle);

    void loadFromCheckpoint(CheckpointObject* checkpoint);

private:
};

class $modify(EbGJGameLevel, GJGameLevel) {
public:
    void savePercentage(int percent, bool isPracticeMode, int clicks, int attempts,
                        bool isChkValid);

private:
};

#endif
