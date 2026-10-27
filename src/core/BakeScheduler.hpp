#ifndef FRIRECRACKER_CORE_BAKESCHEDULER_HPP
#define FRIRECRACKER_CORE_BAKESCHEDULER_HPP
#include "BakeMap.hpp"
class PlayLayer;
class BakeScheduler {
public:
    static BakeScheduler& get();
    bool start(PlayLayer* layer, int startSlot, int radius, BakeMethod method);
    void cancel();
    bool running() const;
    void tickSlice();
    void markFinished();
private:
    bool m_running = false;
};
#endif