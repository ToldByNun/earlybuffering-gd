#ifndef TRIGGER_WORLD_CODEC_HPP
#define TRIGGER_WORLD_CODEC_HPP

#include "../trigger_world_store.hpp"

class EbTriggerWorldCodec {
public:
    static geode::Result<> writeFile(EbTriggerWorldStore const& owner);
    static geode::Result<> readFile(EbTriggerWorldStore& owner);

private:
    EbTriggerWorldCodec() = delete;
};

#endif
