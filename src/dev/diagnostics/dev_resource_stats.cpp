#include "dev_resource_stats.hpp"
#include <algorithm>
#include <chrono>
#include <fmt/format.h>

#if defined(GEODE_IS_WINDOWS) || defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <psapi.h>
#endif

static int64_t ebResourceWallNowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

#if defined(GEODE_IS_WINDOWS) || defined(_WIN32)
static uint64_t ebResourceFileTimeTo100ns(FILETIME const& ft) {
    ULARGE_INTEGER value;
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return value.QuadPart;
}

static bool ebResourceSampleWindows(double& outRamMb, uint64_t& outProc100ns) {
    HANDLE const process = GetCurrentProcess();
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (!GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                              sizeof(memory))) {
        return false;
    }
    // Working set = what the OS currently has resident for this process.
    outRamMb = static_cast<double>(memory.WorkingSetSize) / (1024.0 * 1024.0);

    FILETIME createTime{};
    FILETIME exitTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};
    if (!GetProcessTimes(process, &createTime, &exitTime, &kernelTime, &userTime))
        return false;
    outProc100ns = ebResourceFileTimeTo100ns(kernelTime) + ebResourceFileTimeTo100ns(userTime);
    return true;
}
#endif

static bool ebResourceSampleProcess(double& outRamMb, uint64_t& outProc100ns, bool& outHaveCpu) {
    outHaveCpu = false;
    outRamMb = 0.0;
    outProc100ns = 0;
#if defined(GEODE_IS_WINDOWS) || defined(_WIN32)
    if (!ebResourceSampleWindows(outRamMb, outProc100ns))
        return false;
    outHaveCpu = true;
    return true;
#else
    (void)outRamMb;
    (void)outProc100ns;
    (void)outHaveCpu;
    return false;
#endif
}

void EbDevResourceStats::reset() {
    m_snap = {};
    m_havePrevCpu = false;
    m_prevProc100ns = 0;
    m_prevWallNs = 0;
}

bool EbDevResourceStats::noteSample() {
    double ramMb = 0.0;
    uint64_t proc100ns = 0;
    bool haveCpu = false;
    if (!ebResourceSampleProcess(ramMb, proc100ns, haveCpu))
        return false;

    int64_t const wallNs = ebResourceWallNowNs();
    double cpuPct = 0.0;
    bool cpuOk = false;
    if (haveCpu && m_havePrevCpu && wallNs > m_prevWallNs && proc100ns >= m_prevProc100ns) {
        double const procDeltaSec =
            static_cast<double>(proc100ns - m_prevProc100ns) / 10000000.0; // 100ns → s
        double const wallDeltaSec =
            static_cast<double>(wallNs - m_prevWallNs) / 1000000000.0;
        if (wallDeltaSec > 1e-6) {
            cpuPct = 100.0 * procDeltaSec / wallDeltaSec;
            if (cpuPct < 0.0)
                cpuPct = 0.0;
            // Cap absurd spikes (timer jumps) without hiding multi-thread >100%.
            if (cpuPct > 800.0)
                cpuPct = 800.0;
            cpuOk = true;
        }
    }
    if (haveCpu) {
        m_havePrevCpu = true;
        m_prevProc100ns = proc100ns;
        m_prevWallNs = wallNs;
    }

    if (!m_snap.valid) {
        m_snap.valid = true;
        m_snap.sampleCount = 1;
        m_snap.ramMbMin = ramMb;
        m_snap.ramMbMax = ramMb;
        m_snap.ramMbLast = ramMb;
        if (cpuOk) {
            m_snap.hasCpu = true;
            m_snap.cpuPctMin = cpuPct;
            m_snap.cpuPctMax = cpuPct;
            m_snap.cpuPctLast = cpuPct;
        }
        return true;
    }

    ++m_snap.sampleCount;
    m_snap.ramMbLast = ramMb;
    m_snap.ramMbMin = std::min(m_snap.ramMbMin, ramMb);
    m_snap.ramMbMax = std::max(m_snap.ramMbMax, ramMb);
    if (cpuOk) {
        m_snap.cpuPctLast = cpuPct;
        if (!m_snap.hasCpu) {
            m_snap.hasCpu = true;
            m_snap.cpuPctMin = cpuPct;
            m_snap.cpuPctMax = cpuPct;
        } else {
            m_snap.cpuPctMin = std::min(m_snap.cpuPctMin, cpuPct);
            m_snap.cpuPctMax = std::max(m_snap.cpuPctMax, cpuPct);
        }
    }
    return true;
}

std::string EbDevResourceStats::formatCompact(Snapshot const& snap) {
    if (!snap.valid || snap.sampleCount <= 0)
        return {};
    if (snap.hasCpu) {
        return fmt::format("ram={:.1f}-{:.1f}MB cpu={:.1f}-{:.1f}%", snap.ramMbMin, snap.ramMbMax,
                           snap.cpuPctMin, snap.cpuPctMax);
    }
    return fmt::format("ram={:.1f}-{:.1f}MB", snap.ramMbMin, snap.ramMbMax);
}

bool EbDevResourceStats::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok)
            return;
        ++fails;
        if (!report.empty())
            report += "; ";
        report += name;
    };

    Snapshot empty{};
    expect(formatCompact(empty).empty(), "empty-format");

    Snapshot fake{};
    fake.valid = true;
    fake.sampleCount = 2;
    fake.ramMbMin = 100.0;
    fake.ramMbMax = 200.0;
    fake.ramMbLast = 150.0;
    fake.hasCpu = true;
    fake.cpuPctMin = 10.0;
    fake.cpuPctMax = 50.0;
    fake.cpuPctLast = 25.0;
    auto const bit = formatCompact(fake);
    expect(bit.find("ram=100.0-200.0MB") != std::string::npos, "ram-bit");
    expect(bit.find("cpu=10.0-50.0%") != std::string::npos, "cpu-bit");

    EbDevResourceStats stats;
    stats.reset();
    expect(!stats.snapshot().valid, "reset-invalid");
    // noteSample may fail off-Windows; that is OK for PlanGold hosts.
    (void)stats.noteSample();

    if (fails == 0)
        report = "DevResourceStats OK";
    else
        report = fmt::format("DevResourceStats {} fails: {}", fails, report);
    return fails == 0;
}
