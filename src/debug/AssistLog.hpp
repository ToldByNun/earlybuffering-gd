#ifndef EARLYBUFFER_DEBUG_ASSISTLOG_HPP
#define EARLYBUFFER_DEBUG_ASSISTLOG_HPP

#include <cstdint>
#include <string>

class AssistLog {
public:
    static char const* modTag();
    static void printBanner();
    static void printSavedPress(
        int delayedTicks,
        int frame0,
        int delayedFrame,
        int limit,
        double physicsTps = 240.0
    );
    static void debug(std::string const& message);
    static void info(std::string const& message);

    static double ticksToFrames60(int ticks, double physicsTps);
};

#endif // EARLYBUFFER_DEBUG_ASSISTLOG_HPP
