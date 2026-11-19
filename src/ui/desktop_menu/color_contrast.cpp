#include "color_contrast.hpp"
#include <algorithm>
#include <cmath>
double EbColorContrast::linear(double value) {
    value = std::clamp(value, 0.0, 1.0);
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}
double EbColorContrast::luminance(double r, double g, double b) {
    return 0.2126 * linear(r) + 0.7152 * linear(g) + 0.0722 * linear(b);
}
double EbColorContrast::ratio(double a, double b) {
    return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}
float EbColorContrast::maximumBackground(float target, float opacity) {
    double low = 0, high = 1;
    for (int i = 0; i < 24; ++i) {
        double mid = (low + high) * 0.5;
        double foreground = opacity + (1 - opacity) * mid;
        if (ratio(linear(foreground), linear(mid)) >= target)
            low = mid;
        else
            high = mid;
    }
    return float(low);
}
