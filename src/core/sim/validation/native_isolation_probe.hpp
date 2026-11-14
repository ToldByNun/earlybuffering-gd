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
    static thread_local bool s_creating;
    static thread_local EbShadowSimulator* s_active;
};

#endif
