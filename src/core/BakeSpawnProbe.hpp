#ifndef EARLYBUFFER_CORE_BAKESPAWNPROBE_HPP
#define EARLYBUFFER_CORE_BAKESPAWNPROBE_HPP

#include <string>
#include <vector>

class PlayLayer;
class GameObject;
class PlayerObject;

// Pre-validate spawn overlap: hazards intersecting the start-snap ghost, and
// whether Trigger World tracks those uniqueIds (movers parked on spawn).
class BakeSpawnProbe {
public:
    class Hit {
    public:
        int uniqueId = -1;
        int objectId = 0;
        int objectType = -1;
        float x = 0.f;
        float y = 0.f;
        float rectX = 0.f;
        float rectY = 0.f;
        float rectW = 0.f;
        float rectH = 0.f;
        bool inTriggerWorld = false;
        bool twTrackReady = false;
        int twKeyCount = 0;
        std::string groupsBrief; // up to a few group ids
    };

    class Result {
    public:
        bool ok = false;
        std::string err;
        int startSlot = -1;
        float snapX = 0.f;
        float snapY = 0.f;
        int snapMode = 0;
        bool snapIsDart = false;
        bool jumpHeld = false;
        int twTracks = 0;
        int overlapCount = 0;
        int trackedOverlapCount = 0;
        std::vector<Hit> overlaps; // capped
        bool recommendsTriggerWorld = false;
    };

    // Copy start snap onto shadow (if present) and scan lethal overlaps.
    static Result probe(PlayLayer* layer, int startSnapshotSlot);

    static std::string formatText(Result const& r);
    static bool selfCheck(std::string& report);

private:
    BakeSpawnProbe() = default;

    static bool isLethalOverlapType(GameObject* obj);
    static bool isIgnoredOverlapObject(GameObject* obj);
    static void appendGroupsBrief(GameObject* obj, Hit& hit);
    static void fillTwFields(Hit& hit);
};

#endif // EARLYBUFFER_CORE_BAKESPAWNPROBE_HPP
