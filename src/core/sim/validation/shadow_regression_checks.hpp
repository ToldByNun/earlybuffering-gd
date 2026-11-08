#ifndef SHADOW_REGRESSION_CHECKS_HPP
#define SHADOW_REGRESSION_CHECKS_HPP

#include <string>

class ShadowRegressionChecks {
public:
    static bool regressionSelfCheck(std::string& report);

private:
    ShadowRegressionChecks() = delete;
};

#endif
