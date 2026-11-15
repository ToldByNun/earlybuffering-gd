#ifndef COLOR_CONTRAST_HPP
#define COLOR_CONTRAST_HPP
class EbColorContrast {
public:
    static double linear(double channel);
    static double luminance(double r, double g, double b);
    static double ratio(double foreground, double background);
    static float maximumBackground(float target, float textOpacity = 1.f);

private:
};
#endif
