#ifndef SHADOW_LIVE_RESCUE_HPP
#define SHADOW_LIVE_RESCUE_HPP

class EbShadowPlanContext;

class EbShadowLiveRescue {
public:
    static void evaluate(EbShadowPlanContext& context);

private:
    EbShadowLiveRescue() = delete;
};

#endif
