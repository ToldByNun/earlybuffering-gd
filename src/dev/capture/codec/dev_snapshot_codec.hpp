#ifndef DEV_SNAPSHOT_CODEC_HPP
#define DEV_SNAPSHOT_CODEC_HPP

#include "../dev_clock_contract.hpp"
#include "../dev_live_witness.hpp"
#include <Geode/Geode.hpp>
#include <string>

class EbDevSnapshotCodec {
public:
    static matjson::Value clockStampToJson(EbDevClockContract::Stamp const& st);
    static bool clockStampFromJson(matjson::Value const& j, EbDevClockContract::Stamp& st,
                                   std::string& err);
    static matjson::Value playerBlobToJson(EbDevPlayerBlob const& b);
    static void playerBlobFromJson(matjson::Value const& j, EbDevPlayerBlob& b);
    static matjson::Value witnessToJson(EbDevLiveWitness const& w);
    static bool witnessFromJson(matjson::Value const& j, EbDevLiveWitness& w, std::string& err);

private:
    EbDevSnapshotCodec() = delete;
};

#endif
