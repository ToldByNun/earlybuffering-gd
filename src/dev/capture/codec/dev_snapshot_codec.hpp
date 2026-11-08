#ifndef DEV_SNAPSHOT_CODEC_HPP
#define DEV_SNAPSHOT_CODEC_HPP

#include "../dev_clock_contract.hpp"
#include "../dev_live_witness.hpp"
#include <Geode/Geode.hpp>
#include <string>

class DevSnapshotCodec {
public:
    static matjson::Value clockStampToJson(DevClockContract::Stamp const& st);
    static bool clockStampFromJson(matjson::Value const& j, DevClockContract::Stamp& st,
                                   std::string& err);
    static matjson::Value playerBlobToJson(DevPlayerBlob const& b);
    static void playerBlobFromJson(matjson::Value const& j, DevPlayerBlob& b);
    static matjson::Value witnessToJson(DevLiveWitness const& w);
    static bool witnessFromJson(matjson::Value const& j, DevLiveWitness& w, std::string& err);

private:
    DevSnapshotCodec() = delete;
};

#endif
