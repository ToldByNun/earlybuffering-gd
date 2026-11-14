#ifndef NATIVE_ISOLATION_PROBE_HPP
#define NATIVE_ISOLATION_PROBE_HPP

#include <string>

class EbShadowSimulator;

class EbNativeIsolationProbe {
public:
    static std::string run();
    static bool creatingWorld();
    static EbShadowSimulator* activeSimulator();

private:
    class TickResult {
    public:
        float x = 0.f;
        float y = 0.f;
        double velocity = 0.;
        bool grounded = false;
        bool upsideDown = false;
        bool dead = false;
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
