#ifndef PAUSE_DEV_ACTIONS_HPP
#define PAUSE_DEV_ACTIONS_HPP

class EbPauseDevActions {
public:
    static void stubDevAction(char const* name);
    static void devStartRecord();
    static void devStopRecord();
    static void devEvalLast();
    static void devEvalSession();
    static void devExportSession();
    static void devClearTraces();
    static void devDumpDiag();
    static void devCopyReport();
    static void devExportResearchPack();
    static void devCompareSessions();
    // APR0: import-stamp gold clears; PlayLayer pass still required for reproduce=pass.
    static void devReproduceGold();
    // APR2: Real Assist Runner (Frozen/Online) for case or matrix.
    static void devRunRealAssist();

private:
    EbPauseDevActions() = delete;
};

#endif
