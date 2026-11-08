#ifndef DEV_SESSION_CODEC_HPP
#define DEV_SESSION_CODEC_HPP

#include <Geode/Geode.hpp>
#include <string>

#include "../dev_trace_store.hpp"

class DevSessionCodec {
public:
    static matjson::Value sessionToJsonValue(DevSessionFile const& session);
    static std::string sessionToJsonString(DevSessionFile const& session);
    static bool sessionFromJsonString(std::string const& body, DevSessionFile& out,
                                      std::string& err);

private:
    DevSessionCodec() = delete;
};

#endif
