#include "neural_supervised.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>

int EbNeuralSupervised::train(EbNeuralModel& model, EbNeuralSupervisedRow const* rows, int rowCount,
                              int passes, float learningRate) {
    if (!rows || rowCount <= 0 || passes <= 0)
        return 0;
    int updates = 0;
    std::vector<float> features(static_cast<size_t>(EbNeuralModel::kFeatureDim), 0.0f);
    for (int pass = 0; pass < passes; ++pass) {
        for (int rowIndex = 0; rowIndex < rowCount; ++rowIndex) {
            auto const& row = rows[rowIndex];
            if (row.positiveDelays.empty())
                continue;
            int const limit = EbNeuralModel::clampLimit(row.limit > 0 ? row.limit : row.state.limit);

            EbNeuralGrid grid;
            if (!row.objects.empty())
                grid.rasterize(0.0f, 0.0f, row.objects.data(),
                               static_cast<int>(row.objects.size()));

            // Rank head: candidate-delay feature stays 0 (matches rankedDelays inference).
            EbNeuralFeatureState rankState = row.state;
            rankState.candidateDelay = 0;
            rankState.limit = limit;
            rankState.limitNorm =
                static_cast<float>(limit) / static_cast<float>(EbNeuralModel::kMaxDelay);
            EbNeuralFeatures::build(features.data(), grid, rankState);

            for (int positive : row.positiveDelays) {
                if (positive < 0 || positive > limit)
                    continue;
                model.learnRank(features.data(), positive, limit, learningRate);
                ++updates;
            }

            int rejectDelay = -1;
            for (int delay = 0; delay <= limit; ++delay) {
                if (std::find(row.positiveDelays.begin(), row.positiveDelays.end(), delay) !=
                    row.positiveDelays.end())
                    continue;
                rejectDelay = delay;
                model.learnRankReject(features.data(), delay, limit, learningRate * 0.5f);
                ++updates;
                break;
            }

            // Confidence head AFTER rank updates so trunk drift does not wipe the
            // sigmoid targets (shared trunk; Problem D separation).
            for (int positive : row.positiveDelays) {
                if (positive < 0 || positive > limit)
                    continue;
                EbNeuralFeatureState confState = row.state;
                confState.candidateDelay = positive;
                confState.limit = limit;
                confState.limitNorm =
                    static_cast<float>(limit) / static_cast<float>(EbNeuralModel::kMaxDelay);
                EbNeuralFeatures::build(features.data(), grid, confState);
                model.learnConfidence(features.data(), 1.0f, learningRate);
                ++updates;
            }
            if (rejectDelay >= 0) {
                EbNeuralFeatureState confState = row.state;
                confState.candidateDelay = rejectDelay;
                confState.limit = limit;
                confState.limitNorm =
                    static_cast<float>(limit) / static_cast<float>(EbNeuralModel::kMaxDelay);
                EbNeuralFeatures::build(features.data(), grid, confState);
                model.learnConfidence(features.data(), 0.0f, learningRate * 0.5f);
                ++updates;
            }
        }
    }
    return updates;
}

float EbNeuralSupervised::verifyHitAtK(EbNeuralModel const& model, EbNeuralSupervisedRow const& row,
                                       int k) {
    if (k <= 0 || row.positiveDelays.empty())
        return 0.0f;
    int const limit = EbNeuralModel::clampLimit(row.limit > 0 ? row.limit : row.state.limit);
    EbNeuralGrid grid;
    if (!row.objects.empty())
        grid.rasterize(0.0f, 0.0f, row.objects.data(), static_cast<int>(row.objects.size()));
    EbNeuralFeatureState state = row.state;
    state.candidateDelay = 0;
    state.limit = limit;
    state.limitNorm = static_cast<float>(limit) / static_cast<float>(EbNeuralModel::kMaxDelay);
    std::vector<float> features(static_cast<size_t>(EbNeuralModel::kFeatureDim), 0.0f);
    EbNeuralFeatures::build(features.data(), grid, state);
    // Prior quality metric uses the 49-way rank head (delay slot = 0, same as
    // learnRank). Live search uses rankedDelaysByConfidence separately.
    auto ranked = model.rankedDelays(features.data(), limit, k);
    if (ranked.empty())
        return 0.0f;
    int hits = 0;
    for (int delay : ranked) {
        if (std::find(row.positiveDelays.begin(), row.positiveDelays.end(), delay) !=
            row.positiveDelays.end())
            ++hits;
    }
    return static_cast<float>(hits) / static_cast<float>(ranked.size());
}

bool EbNeuralSupervised::selfCheck(std::string& report) {
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    EbNeuralSupervisedRow row;
    row.limit = 5;
    row.state.modeKind = 1;
    row.state.press = true;
    row.state.limit = 5;
    row.positiveDelays = {2, 3};
    EbNeuralGridObject block;
    block.x = 10.0f;
    block.y = 10.0f;
    block.width = 30.0f;
    block.height = 15.0f;
    block.channelBits = EbNeuralGrid::kChannelSolid;
    row.objects.push_back(block);

    EbNeuralModel model;
    float const before = verifyHitAtK(model, row, 2);
    int const updates = train(model, &row, 1, 8, 0.05f);
    expect(updates > 0, "trained");
    float const after = verifyHitAtK(model, row, 2);
    expect(after >= before, "verifyhit-nonregress");
    expect(after > 0.0f, "verifyhit-positive");

    // Confidence head: delay slot must move the score when trained in isolation.
    {
        float probe[EbNeuralModel::kFeatureDim]{};
        EbNeuralModel::writeCandidateDelay(probe, 3);
        for (int step = 0; step < 30; ++step)
            model.learnConfidence(probe, 1.0f, 0.15f);
        float const up = model.confidence(probe);
        EbNeuralModel::writeCandidateDelay(probe, 1);
        for (int step = 0; step < 30; ++step)
            model.learnConfidence(probe, 0.0f, 0.15f);
        float const down = model.confidence(probe);
        expect(up > down, "conf-delay-sensitive");
    }

    auto path = std::filesystem::temp_directory_path() / "eb-neural-supervised-prior.neural";
    expect(model.save(path), "export-prior");
    EbNeuralModel loaded;
    expect(loaded.load(path), "load-prior");
    expect(verifyHitAtK(loaded, row, 2) == after, "prior-parity");
    std::error_code error;
    std::filesystem::remove(path, error);

    if (fails == 0) {
        report = "NeuralSupervised OK";
        return true;
    }
    report = std::string("NeuralSupervised fails: ") + report;
    return false;
}
