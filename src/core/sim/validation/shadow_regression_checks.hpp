#ifndef SHADOW_REGRESSION_CHECKS_HPP
#define SHADOW_REGRESSION_CHECKS_HPP

#include <string>

class EbShadowRegressionChecks {
public:
    static bool regressionSelfCheck(std::string& report);

private:
    EbShadowRegressionChecks() = delete;
};

#endif
