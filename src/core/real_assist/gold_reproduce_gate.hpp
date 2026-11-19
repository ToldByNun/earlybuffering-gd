#ifndef GOLD_REPRODUCE_GATE_HPP
#define GOLD_REPRODUCE_GATE_HPP

#include <filesystem>
#include <string>
#include <vector>

// APR0: gold clear is untrusted for oracle until PlayLayer reproduce=pass.
class EbGoldReproduceGate {
public:
    static constexpr char const* kSchema = "1";
    static constexpr char const* kEnvCorpusDir = "EB_REAL_CORPUS_DIR";
    static constexpr char const* kEnvAuto = "EB_REAL_ASSIST_AUTO";
    static constexpr char const* kEnvCase = "EB_REAL_ASSIST_CASE";

    static std::filesystem::path corpusRoot();
    static std::vector<std::string> ids();
    static std::filesystem::path idDir(std::string const& corpusId);
    static std::filesystem::path clearPath(std::string const& corpusId);
    static std::filesystem::path reproducePath(std::string const& corpusId);

    // Offline/import half of the gate: load clear.gdr2, refresh reproduce.json import block.
    // Does not mark pass — PlayLayer clear is still required.
    static bool importAndStamp(std::string const& corpusId, std::string& report);

    // Run import stamps for selected ids (empty / "all" → every known id).
    static bool runImportGate(std::string const& caseFilter, std::string& report);

    // Dev / Lab entry: import gate + queue note for full PlayLayer reproduce.
    // Returns false if any selected id failed import; fills report with PASS/FAIL block.
    static bool runDevAction(std::string& report);

    // After mod load: if EB_REAL_ASSIST_AUTO is set, queue the import gate on the main thread.
    static void maybeQueueAutoFromEnv();

    static bool statusIsPass(std::string const& corpusId);
    static bool selfCheck(std::string& report);

private:
    EbGoldReproduceGate() = delete;

    static bool envTruthy(char const* name);
    static std::string envOrEmpty(char const* name);
    static std::filesystem::path saveCorpusRoot();
    static bool writeLastRunSummary(std::string const& text, std::string& err);
};

#endif
