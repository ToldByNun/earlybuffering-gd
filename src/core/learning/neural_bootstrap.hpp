#ifndef NEURAL_BOOTSTRAP_HPP
#define NEURAL_BOOTSTRAP_HPP

#include <string>
#include <vector>

// AP06: merge Hybrid mature proposals into AI verify order (transfer, not dependency).
class EbNeuralBootstrap {
public:
    // Hybrid delays first (deduped), then neural fill, truncated to topK.
    static std::vector<int> mergeVerifyOrder(std::vector<int> const& hybrid,
                                             std::vector<int> const& neural, int topK);

    static bool selfCheck(std::string& report);

private:
    EbNeuralBootstrap() = delete;
};

#endif
