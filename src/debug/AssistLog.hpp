#pragma once

#include <string>
#include <cstdint>

namespace frire {

struct StageInfo;

class AssistLog {
public:
    static char const* modTag();
    static void printBanner();
    static void printCycle(StageInfo const& stage, double physicsTps = 240.0);
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

}
