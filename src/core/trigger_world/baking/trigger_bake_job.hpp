#ifndef TRIGGER_BAKE_JOB_HPP
#define TRIGGER_BAKE_JOB_HPP

#include "../trigger_world_baker.hpp"

class EbTriggerBakeJob {
public:
    static void tickSlice(EbTriggerWorldBaker& owner);

private:
    EbTriggerBakeJob() = delete;
};

#endif
