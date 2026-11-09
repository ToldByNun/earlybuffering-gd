#ifndef DEV_EVENT_CODEC_HPP
#define DEV_EVENT_CODEC_HPP

#include <Geode/Geode.hpp>
#include <string>

#include "../dev_trace_store.hpp"

class EbDevEventCodec {
public:
    static matjson::Value eventToJson(EbDevDecisionEvent const& e);
    static bool eventFromJson(matjson::Value const& j, EbDevDecisionEvent& e, std::string& err);

private:
    EbDevEventCodec() = delete;
};

#endif
