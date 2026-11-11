#ifndef EB_SLIDER_HPP
#define EB_SLIDER_HPP

#include <cstdint>

// Settings slider: label + value on top, thin track below (image-2 chrome).
// Fill uses the sidebar wave; knob is a white circle. Float shows e.g. "5.0F";
// int can append a unit suffix ("px", …).
class EbSlider {
public:
    // Returns true when *value changed.
    static bool drawFloat(char const* id, char const* label, float* value, float minValue,
                          float maxValue, int decimals = 1, char const* suffix = "F",
                          bool enabled = true);

    static bool drawInt(char const* id, char const* label, int* value, int minValue, int maxValue,
                        char const* suffix = "", bool enabled = true);

    static bool drawFloatSetting(char const* key, char const* label, float minValue, float maxValue,
                                 int decimals = 1, char const* suffix = "F", bool enabled = true);

    static bool drawIntSetting(char const* key, char const* label, int64_t minValue,
                               int64_t maxValue, char const* suffix = "", bool enabled = true);

private:
    EbSlider() = delete;
};

#endif
