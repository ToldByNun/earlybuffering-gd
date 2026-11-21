#ifndef NEURAL_SUPERVISED_HPP
#define NEURAL_SUPERVISED_HPP

#include "neural_features.hpp"
#include "neural_model.hpp"
#include <string>
#include <vector>

// Labeled press row for Candidate Scorer pretrain (Real Assist / synthetic).
class EbNeuralSupervisedRow {
public:
    EbNeuralFeatureState state{};
    std::vector<EbNeuralGridObject> objects;
    std::vector<int> positiveDelays;
    int limit = 0;

private:
};

class EbNeuralSupervised {
public:
    // Train rank + confidence heads from PhysicsSafe-style positive delays.
    static int train(EbNeuralModel& model, EbNeuralSupervisedRow const* rows, int rowCount,
                     int passes = 4, float learningRate = 0.02f);

    // Fraction of top-k ranked delays that land in positives (diagnostic VerifyHit@k).
    static float verifyHitAtK(EbNeuralModel const& model, EbNeuralSupervisedRow const& row, int k);

    static bool selfCheck(std::string& report);

private:
    EbNeuralSupervised() = delete;
};

#endif
