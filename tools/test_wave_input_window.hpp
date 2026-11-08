#ifndef TEST_WAVE_INPUT_WINDOW_HPP
#define TEST_WAVE_INPUT_WINDOW_HPP

#include <array>

class WaveInputWindowRegressionTests {
public:
    static int run();

private:
    class RecordedInput {
    public:
        int pressId;
        std::array<int, 13> deathTicks;
        int expectedDelay;

    private:
    };

    static int chooseRecordedDelay(RecordedInput const& input, int horizon);
    WaveInputWindowRegressionTests() = delete;
};

#endif
