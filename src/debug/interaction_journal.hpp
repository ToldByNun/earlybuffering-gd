#ifndef INTERACTION_JOURNAL_HPP
#define INTERACTION_JOURNAL_HPP

#include <chrono>
#include <cstdint>
#include <string>

// Always-on interaction + hitch journal (no Dev Lab required).
// File: {modSaveDir}/earlybuffer-interactions.log
// Flushes on quit / every N lines so leave hitches still leave a trail.
class EbInteractionJournal {
public:
    class Scope {
    public:
        explicit Scope(char const* name, std::string detail = {});
        ~Scope();
        Scope(Scope const&) = delete;
        Scope& operator=(Scope const&) = delete;

    private:
        char const* m_name = nullptr;
        std::string m_detail;
        std::chrono::steady_clock::time_point m_start{};
        bool m_active = false;
    };

    static EbInteractionJournal& get();

    // Always on unless explicitly disabled (default true).
    void setEnabled(bool on);
    bool enabled() const {
        return m_enabled;
    }

    static std::string logPath();

    void note(char const* event, std::string detail = {});
    void noteMs(char const* event, double elapsedMs, std::string detail = {});
    void flush();

private:
    friend class EbServiceStore;

    EbInteractionJournal();

    void appendLine(std::string const& line);
    void writeToFile(std::string const& text);
    void writeHeader();
    void rotateIfHuge();

    bool m_enabled = true;
    bool m_headerWritten = false;
    std::string m_pending;
    int m_pendingLines = 0;
    static constexpr int kFlushEveryLines = 8;
    static constexpr std::uint64_t kRotateBytes = 4ull * 1024ull * 1024ull;
};

#endif
