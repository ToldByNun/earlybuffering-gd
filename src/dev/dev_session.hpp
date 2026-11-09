#ifndef DEV_SESSION_HPP
#define DEV_SESSION_HPP

#include <filesystem>
#include <string>

// Dev Lab session controller (Commit 13).
class EbDevSession {
public:
    enum class State { Idle = 0, Recording };
    static EbDevSession& get();
    State state() const {
        return this->m_state;
    }
    bool isRecording() const {
        return this->m_state == State::Recording;
    }
    bool start();
    void stop();
    std::string statusLine() const;
    std::filesystem::path sessionPath() const;
    int clearTraces();
    void refreshLevelMeta();
    static bool selfCheck(std::string& report);

private:
    friend class EbServiceStore;

    EbDevSession() = default;
    State m_state = State::Idle;
};
#endif
