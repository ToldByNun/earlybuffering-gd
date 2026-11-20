#include "neural_bootstrap.hpp"
#include "neural_model.hpp"
#include <algorithm>

std::vector<int> EbNeuralBootstrap::mergeVerifyOrder(std::vector<int> const& hybrid,
                                                     std::vector<int> const& neural, int topK) {
    std::vector<int> out;
    if (topK <= 0)
        return out;
    out.reserve(static_cast<size_t>(topK));
    bool seen[EbNeuralModel::kActionCount]{};
    auto push = [&](int delay) {
        if (static_cast<int>(out.size()) >= topK)
            return;
        if (delay < 0 || delay > EbNeuralModel::kMaxDelay)
            return;
        if (seen[static_cast<size_t>(delay)])
            return;
        seen[static_cast<size_t>(delay)] = true;
        out.push_back(delay);
    };
    for (int delay : hybrid)
        push(delay);
    for (int delay : neural)
        push(delay);
    return out;
}

bool EbNeuralBootstrap::selfCheck(std::string& report) {
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    auto merged = mergeVerifyOrder({3, 2, 3}, {1, 2, 0}, 3);
    expect(merged.size() == 3, "size-3");
    expect(merged[0] == 3 && merged[1] == 2 && merged[2] == 1, "hybrid-first");

    auto neuralOnly = mergeVerifyOrder({}, {4, 1, 0}, 2);
    expect(neuralOnly.size() == 2 && neuralOnly[0] == 4 && neuralOnly[1] == 1, "neural-fill");

    auto empty = mergeVerifyOrder({1}, {2}, 0);
    expect(empty.empty(), "topk-zero");

    if (fails == 0) {
        report = "NeuralBootstrap OK";
        return true;
    }
    report = std::string("NeuralBootstrap fails: ") + report;
    return false;
}
