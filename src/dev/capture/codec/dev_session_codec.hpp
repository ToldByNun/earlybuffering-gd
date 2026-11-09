#ifndef DEV_SESSION_CODEC_HPP
#define DEV_SESSION_CODEC_HPP

#include <Geode/Geode.hpp>
#include <string>

#include "../dev_trace_store.hpp"

class EbDevSessionCodec {
public:
    static matjson::Value sessionToJsonValue(EbDevSessionFile const& session);
    static std::string sessionToJsonString(EbDevSessionFile const& session);
    static bool sessionFromJsonString(std::string const& body, EbDevSessionFile& out,
                                      std::string& err);

private:
    EbDevSessionCodec() = delete;
};

#endif
