#include "interaction_journal.hpp"
#include "../app/class_manager.hpp"
#include "assist_log.hpp"
#include <Geode/Geode.hpp>
#include <asp/fs/std.hpp>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>

EbInteractionJournal& EbInteractionJournal::get() {
    return EbClassManager::get().service<EbInteractionJournal>();
}

EbInteractionJournal::EbInteractionJournal() {
    this->writeHeader();
}

EbInteractionJournal::Scope::Scope(char const* name, std::string detail)
    : m_name(name), m_detail(std::move(detail)), m_start(std::chrono::steady_clock::now()),
      m_active(name != nullptr) {}

EbInteractionJournal::Scope::~Scope() {
    if (!this->m_active || !this->m_name)
        return;
    double const elapsedMicroseconds =
        static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - this->m_start)
                                .count());
    EbInteractionJournal::get().noteMs(this->m_name, elapsedMicroseconds / 1000.0, this->m_detail);
}

std::string EbInteractionJournal::logPath() {
    auto* mod = geode::prelude::Mod::get();
    if (!mod)
        return {};
    return (mod->getSaveDir().toStdPath().unwrap() / "earlybuffer-interactions.log").string();
}

void EbInteractionJournal::setEnabled(bool on) {
    if (this->m_enabled == on)
        return;
    this->m_enabled = on;
    if (on) {
        this->writeHeader();
        this->flush();
        EbAssistLog::info(fmt::format("InteractionJournal ON → {}", logPath()));
        return;
    }
    this->flush();
    EbAssistLog::info("InteractionJournal OFF");
}

void EbInteractionJournal::writeToFile(std::string const& text) {
    if (text.empty())
        return;
    auto const path = logPath();
    if (path.empty())
        return;
    this->rotateIfHuge();
    std::ofstream out(path, std::ios::app);
    if (!out)
        return;
    out << text;
    if (!text.empty() && text.back() != '\n')
        out << '\n';
}

void EbInteractionJournal::rotateIfHuge() {
    auto const path = logPath();
    if (path.empty())
        return;
    std::error_code ec;
    auto const size = std::filesystem::file_size(path, ec);
    if (ec || size < kRotateBytes)
        return;
    auto const bak = path + ".1";
    std::filesystem::remove(bak, ec);
    std::filesystem::rename(path, bak, ec);
    this->m_headerWritten = false;
}

void EbInteractionJournal::appendLine(std::string const& line) {
    if (!this->m_enabled)
        return;
    this->m_pending += line;
    this->m_pending.push_back('\n');
    ++this->m_pendingLines;
    if (this->m_pendingLines >= kFlushEveryLines)
        this->flush();
}

void EbInteractionJournal::flush() {
    if (this->m_pending.empty())
        return;
    this->writeToFile(this->m_pending);
    this->m_pending.clear();
    this->m_pendingLines = 0;
}

void EbInteractionJournal::writeHeader() {
    if (!this->m_enabled || this->m_headerWritten)
        return;
    this->m_headerWritten = true;
    auto const now = std::chrono::system_clock::now();
    std::time_t const time = std::chrono::system_clock::to_time_t(now);
    char stamp[64] = {};
#if defined(_WIN32)
    std::tm local{};
    localtime_s(&local, &time);
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
#else
    std::tm local{};
    localtime_r(&time, &local);
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
#endif
    this->appendLine("================================================================");
    this->appendLine("EarlyBuffer InteractionJournal — always-on (no Dev Lab needed)");
    this->appendLine(fmt::format("file: {}", logPath()));
    this->appendLine(fmt::format("session: {}", stamp));
    this->appendLine("----------------------------------------------------------------");
    this->appendLine("Events: enter/quit/reset/death/checkpoint/click/plan/tw-* /hitch");
    this->appendLine("Paste the latest block after a laggy leave or FPS drop.");
    this->appendLine("================================================================");
    this->flush();
}

void EbInteractionJournal::note(char const* event, std::string detail) {
    if (!this->m_enabled || !event)
        return;
    if (!this->m_headerWritten)
        this->writeHeader();
    auto const ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count();
    if (detail.empty()) {
        this->appendLine(fmt::format("+{}ms {}", ms, event));
        return;
    }
    this->appendLine(fmt::format("+{}ms {} | {}", ms, event, detail));
}

void EbInteractionJournal::noteMs(char const* event, double elapsedMs, std::string detail) {
    if (!this->m_enabled || !event)
        return;
    if (!this->m_headerWritten)
        this->writeHeader();
    auto const ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count();
    if (detail.empty()) {
        this->appendLine(fmt::format("+{}ms {} = {:.2f}ms", ms, event, elapsedMs));
        return;
    }
    this->appendLine(fmt::format("+{}ms {} = {:.2f}ms | {}", ms, event, elapsedMs, detail));
}
