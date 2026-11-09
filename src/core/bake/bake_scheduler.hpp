#ifndef BAKE_SCHEDULER_HPP
#define BAKE_SCHEDULER_HPP
#include "bake_map.hpp"
class PlayLayer;
class EbBakeScheduler {
public:
    static EbBakeScheduler& get();
    bool start(PlayLayer* layer, int startSlot, int radius, BakeMethod method);
    void cancel();
    bool running() const;
    void tickSlice();
    void markFinished();

private:
    friend class EbServiceStore;

    bool m_running = false;
};
#endif
