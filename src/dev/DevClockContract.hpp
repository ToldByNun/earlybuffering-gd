#ifndef DEV_CLOCK_CONTRACT_HPP
#define DEV_CLOCK_CONTRACT_HPP

#include <cstdint>
#include <string>
#include <vector>

// Named live/eval sim entry points (Vertrag C report fields).
enum class DevSimPathId {
    None = 0,
    Plan,
    PlanSequence,
    VerifyDelay,
    EvalDelayFromFreeze,
    ReplayActualFire
};

// Schema-versioned time basis shared by live planning and Dev eval.
// Live and eval must call the same effectTick / stepSchedule — never fork.
class DevClockContract {
public:
    static constexpr int kSchema = 1;
    static constexpr char const* kTimeBasisPhysicsTick = "physicsTick";

    struct Stamp {
        int clockSchema = DevClockContract::kSchema;
        std::string timeBasis = DevClockContract::kTimeBasisPhysicsTick;
        int attemptTick = 0;
        int effectTick = 0;
        float simDt = 0.f;
        float firstDt = 0.f;
        bool midStep = false;
        bool continuous = false;
        float timeWarp = 1.f;
        double simTps = 0.0;
        float wallDt = 0.f; // annotation only; ignored by stepSchedule
        bool speedScaleSuspect = false;
        DevSimPathId liveSimPathId = DevSimPathId::None;
        bool valid = false; // set by makeStamp / capture
    };

    enum class Reject {
        Ok = 0,
        UnknownSchema,
        BadTimeBasis,
        BadSimDt,
        BadFirstDt,
        EffectTickMismatch
    };

    static DevClockContract& get();

    static char const* simPathId(DevSimPathId id);
    static bool parseSimPathId(std::string const& s, DevSimPathId& out);
    static char const* rejectId(Reject r);

    // Same formula as production AssistController::resolveEffectTick.
    // Continuous: always attemptTick. Discrete: midStep → attemptTick, else +1.
    int effectTick(int attemptTick, bool midStep, bool continuous) const;

    // firstDt for step 0 only when midStep and firstDt in (0, simDt]; else simDt.
    float step0Dt(Stamp const& stamp) const;

    // Physics dt list for counterfactual delay d and primary horizon H.
    // Length = delay + horizonTicks. Action at index `delay`.
    // wallDt / timeWarp / simTps / speedScaleSuspect do not affect the list.
    std::vector<float> stepSchedule(
        Stamp const& stamp, int delay, int horizonTicks
    ) const;

    // FNV-1a over quantized step dts (microseconds).
    uint64_t hashSteps(std::vector<float> const& steps) const;
    // Stable hash of the selfCheck reference table (report stepScheduleHash).
    uint64_t stepScheduleHash() const;

    // Build stamp at live decision time. Sets effectTick via this->effectTick.
    Stamp makeStamp(
        int attemptTick,
        bool midStep,
        bool continuous,
        float simDt,
        float firstDt,
        float wallDt,
        float timeWarp,
        DevSimPathId livePath
    ) const;

    Reject validate(Stamp const& stamp) const;
    bool clockOk(Stamp const& stamp) const {
        return this->validate(stamp) == Reject::Ok;
    }

    static bool selfCheck(std::string& report);

private:
    DevClockContract() = default;
};

#endif // DEV_CLOCK_CONTRACT_HPP
