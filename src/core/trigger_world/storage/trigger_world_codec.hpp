#ifndef TRIGGER_WORLD_CODEC_HPP
#define TRIGGER_WORLD_CODEC_HPP

#include "../trigger_world_store.hpp"

class TriggerWorldCodec {
public:
    static geode::Result<> writeFile(TriggerWorldStore const& owner);
    static geode::Result<> readFile(TriggerWorldStore& owner);

private:
    TriggerWorldCodec() = delete;
};

#endif
