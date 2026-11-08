#ifndef DEV_EVENT_CODEC_HPP
#define DEV_EVENT_CODEC_HPP

#include <Geode/Geode.hpp>
#include <string>

#include "../dev_trace_store.hpp"

class DevEventCodec {
public:
    static matjson::Value eventToJson(DevDecisionEvent const& e);
    static bool eventFromJson(matjson::Value const& j, DevDecisionEvent& e, std::string& err);

private:
    DevEventCodec() = delete;
};

#endif
