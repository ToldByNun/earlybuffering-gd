#ifndef EARLYBUFFER_CORE_DIVERGENCETRACKER_HPP
#define EARLYBUFFER_CORE_DIVERGENCETRACKER_HPP
#include <string>
class DivergenceTracker {
public:
    static DivergenceTracker& get();
    void note(bool planDelay, bool died, bool expectOrb, bool gotOrb);
    float mismatchRate() const;
    bool shouldWarn() const;
    bool simUntrusted() const { return m_untrusted; }
    void clear();
    std::string overlayBit() const;
    static bool selfCheck(std::string& report);
private:
    static constexpr int N = 48;
    bool m_bad[N]{};
    int m_i = 0;
    int m_count = 0;
    bool m_untrusted = false;
};
#endif