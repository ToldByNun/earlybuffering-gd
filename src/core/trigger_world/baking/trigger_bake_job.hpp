#ifndef TRIGGER_BAKE_JOB_HPP
#define TRIGGER_BAKE_JOB_HPP

#include "../trigger_world_baker.hpp"

class TriggerBakeJob {
public:
    static void tickSlice(TriggerWorldBaker& owner);

private:
    TriggerBakeJob() = delete;
};

#endif
