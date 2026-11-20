#ifndef NEURAL_FEATURES_HPP
#define NEURAL_FEATURES_HPP

#include "neural_grid.hpp"
#include "neural_model.hpp"
#include <cstdint>
#include <string>

// Continuous + sparse-grid pack for EbNeuralModel (versioned layout).
class EbNeuralFeatureState {
public:
    int modeKind = 0; // 0..7 GameModeKind
    bool press = true;
    bool player1 = true;
    bool dual = false;
    bool practice = false;
    bool grounded = false;
    float xFrac = 0.0f;
    float yNorm = 0.0f;
    float vxSign = 0.0f;
    float vyNorm = 0.0f;
    float speedNorm = 0.0f;
    float sizeNorm = 0.0f;
    float gravityMod = 0.0f;
    bool upsideDown = false;
    bool goingLeft = false;
    bool sideways = false;
    float limitNorm = 0.0f;
    float remainingBudgetNorm = 0.0f;
    float firstDeathHintNorm = -1.0f;
    bool nearOrb = false;
    int candidateDelay = 0;
    int limit = 0;
    // Reserved geometry distances (Nova-style). Unset stays 0; invalid uses sentinel path later.
    float geometry[28]{};

private:
};

class EbNeuralFeatures {
public:
    static constexpr std::uint8_t kVersion = 1;

    static void clear(float* outFeatures);
    static void build(float* outFeatures, EbNeuralGrid const& grid,
                      EbNeuralFeatureState const& state);
    static void writeContinuous(float* outFeatures, EbNeuralFeatureState const& state);
    static bool rejectNonFinite(float const* features);
    static bool selfCheck(std::string& report);

private:
    static float clampUnit(float value, float lo, float hi);
};

#endif
