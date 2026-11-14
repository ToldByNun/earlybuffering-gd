#ifndef NATIVE_ISOLATION_PROBE_HPP
#define NATIVE_ISOLATION_PROBE_HPP

#include <Geode/Geode.hpp>
#include <iosfwd>
#include <string>

class EbShadowSimulator;

class EbNativeIsolationProbe {
public:
    static std::string run();
    static bool creatingWorld();
    static EbShadowSimulator* activeSimulator();
    static void recordRingApplication(RingObject* ring, char const* event, PlayerObject* player);

private:
    static constexpr int kOrbRuns = 1000;
    static constexpr int kModeCount = 8;
    static constexpr int kRunsPerMode = 64;
    static constexpr int kTotalRuns = kOrbRuns + kModeCount * kRunsPerMode;
    static void configureMode(PlayerObject* player, int mode, bool inverted, bool mini, int world);
    static int modeMask(PlayerObject* player);

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
        bool blockActive = false;
        bool toggleModeled = false;
        bool initialBlockActive = false;
        int mode = 0;
        bool modeConfigured = true;
        float vehicleSize = 1.f;
        int inputCommands = 0;
        float width = 0.f;
        float height = 0.f;
        float movingX = 0.f;
        float movingY = 0.f;
        int objectId = -1;
        float objectX = 0.f;
        float objectY = 0.f;

        bool matches(TickResult const& other) const;

    private:
    };

    static thread_local std::ostream* s_ringLog;
    static thread_local bool s_creating;
    static thread_local EbShadowSimulator* s_active;
};

#endif
