#ifndef NATIVE_ISOLATION_PROBE_HPP
#define NATIVE_ISOLATION_PROBE_HPP

#include <Geode/Geode.hpp>
#include <string>

class EbShadowSimulator;

class EbNativeIsolationProbe {
public:
    static std::string run();
    static bool creatingWorld();
    static EbShadowSimulator* activeSimulator();

private:
    static void moveFixtureObject(PlayLayer* layer, GameObject* object,
                                  cocos2d::CCPoint const& position);

    class TickResult {
    public:
        float x = 0.f;
        float y = 0.f;
        double velocity = 0.;
        bool grounded = false;
        bool upsideDown = false;
        bool dead = false;
        int ringCount = 0;
        bool orbContact = false;
        bool jumpHeld = false;
        bool orbOverlap = false;
        float movingX = 0.f;
        float movingY = 0.f;
        int objectId = -1;
        float objectX = 0.f;
        float objectY = 0.f;

        bool matches(TickResult const& other) const;

    private:
    };

    static thread_local bool s_creating;
    static thread_local EbShadowSimulator* s_active;
};

#endif
