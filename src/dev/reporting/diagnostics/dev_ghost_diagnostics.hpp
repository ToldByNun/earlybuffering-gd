#ifndef DEV_GHOST_DIAGNOSTICS_HPP
#define DEV_GHOST_DIAGNOSTICS_HPP

#include <string>

class EbDevGhostDiagnostics {
public:
    static void dumpVehicleFlags(std::ostringstream& o, char const* prefix, PlayerObject* player);
    static bool isHazardType(GameObject* object);
    static void dumpRectLine(std::ostringstream& o, char const* name, PlayerObject* player);
    static int countHazardOverlaps(PlayLayer* layer, PlayerObject* player,
                                   std::ostringstream& detail, int maxDetail);
    static std::string buildGhostHitbox();

private:
    EbDevGhostDiagnostics() = delete;
};

#endif
