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

private:
    EbPauseDevActions() = delete;
};

#endif
