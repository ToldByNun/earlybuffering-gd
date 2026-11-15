#include "hybrid_model.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>

std::vector<EbHybridModel::Inspection> EbHybridModel::inspect(int minimumAttempts) const {
    std::vector<Inspection> rows;
    rows.reserve(4096);
    for (auto const& set : m_sets)
        for (auto const& spot : set)
            if (spot.key)
                for (auto const& action : spot.actions)
                    if (action.delay >= 0)
                        rows.push_back({spot.key, action.delay, action.count, action.score,
                                        proposal(spot.key, minimumAttempts) == action.delay,
                                        action.evidence});
    return rows;
}

uint64_t EbHybridModel::mix(uint64_t key, int value) {
    return (key ^ static_cast<uint32_t>(value)) * 1099511628211ull;
}
void EbHybridModel::setFrozen(bool frozen) {
    m_frozen = frozen;
}
bool EbHybridModel::frozen() const {
    return m_frozen;
}
void EbHybridModel::nextAttempt() {
    ++m_attempt;
}
void EbHybridModel::reset() {
    for (auto& set : m_sets)
        set = {};
    m_attempt = 1;
    m_familiar = 0;
    m_frozen = false;
    m_dirty = true;
}
void EbHybridModel::observe(uint64_t key, int delay, int reward, Evidence const* observation) {
    if (m_frozen || !key || delay < 0 || delay > 96 || (!reward && !observation))
        return;
    auto& set = m_sets[key % m_sets.size()];
    auto retention = [&](Spot const& entry) {
        int peak = 0;
        for (auto const& action : entry.actions)
            peak = std::max(peak, action.score);
        auto const age = std::min(64u, m_attempt - entry.attempt);
        return peak - 4 * static_cast<int>(age);
    };
    auto* spot = &set.front();
    for (auto& entry : set) {
        if (entry.key == key) {
            spot = &entry;
            break;
        }
        if (!entry.key || (spot->key && retention(entry) < retention(*spot)))
            spot = &entry;
    }
    if (spot->key != key) {
        *spot = {};
        spot->key = key;
    }

    auto* action = &spot->actions.front();
    for (auto& entry : spot->actions) {
        if (entry.delay == delay) {
            action = &entry;
            break;
        }
        if (entry.delay < 0 || (action->delay >= 0 && entry.score < action->score))
            action = &entry;
    }
    if (action->delay != delay)
        *action = {delay, 0, 0};
    if (observation) {
        auto& evidence = action->evidence;
        auto add = [](int& total, int value) { total = std::min(65535, total + value); };
        add(evidence.successCount, observation->successCount);
        add(evidence.failureCount, observation->failureCount);
        add(evidence.unknownCount, observation->unknownCount);
        add(evidence.verifiedRescueCount, observation->verifiedRescueCount);
        add(evidence.verifiedHarmCount, observation->verifiedHarmCount);
        if (observation->deathDeltaTicks >= 0) {
            evidence.deathDeltaTicks = observation->deathDeltaTicks;
            evidence.interveningInputs = observation->interveningInputs;
            evidence.timingPhase = observation->timingPhase;
            evidence.inputSequenceSignature = observation->inputSequenceSignature;
        }
        m_dirty = true;
    }
    if (!reward || spot->attempt == m_attempt)
        return;
    spot->attempt = m_attempt;
    // Two neutral prior observations temper new timings. Cap the effective
    // history so repeated failures can still displace a long-established action.
    int const divisor = std::min(12, action->count + (reward < -1 ? 2 : 3));
    int const target = 64 * std::clamp(reward, -4, 3);
    action->score += (target - action->score) / divisor;
    action->count = std::min(255, action->count + 1);
    m_familiar = -1;
    m_dirty = true;
}
bool EbHybridModel::hasEvidence(uint64_t key) const {
    auto const& set = m_sets[key % m_sets.size()];
    return key &&
           std::any_of(set.begin(), set.end(), [key](auto const& spot) { return spot.key == key; });
}
int EbHybridModel::proposal(uint64_t key, int minimumAttempts, int limit) const {
    if (!key)
        return -1;
    for (auto const& spot : m_sets[key % m_sets.size()]) {
        if (spot.key != key)
            continue;
        Action const* best = nullptr;
        int second = 0;
        for (auto const& action : spot.actions) {
            if (action.delay < 0 || action.delay > limit)
                continue;
            if (!best || action.score > best->score) {
                if (best)
                    second = std::max(second, best->score);
                best = &action;
            } else {
                second = std::max(second, action.score);
            }
        }
        if (best && best->count >= std::clamp(minimumAttempts, 3, 12) &&
            best->score >= (best->delay == 0 ? 20 : 48) && best->score - second >= 24)
            return best->delay;
    }
    return -1;
}
int EbHybridModel::familiar() const {
    if (m_familiar >= 0)
        return m_familiar;
    int count = 0;
    for (auto const& set : m_sets)
        for (auto const& spot : set)
            if (spot.key && proposal(spot.key) >= 0)
                ++count;
    m_familiar = count;
    return count;
}
bool EbHybridModel::save(std::filesystem::path const& path) {
    if (!m_dirty)
        return true;
    std::ostringstream payload;
    for (auto const& set : m_sets)
        for (auto const& spot : set)
            if (spot.key)
                for (auto const& action : spot.actions)
                    if (action.delay >= 0)
                        payload << spot.key << ' ' << action.delay << ' ' << action.count << ' '
                                << action.score << ' ' << action.evidence.successCount << ' '
                                << action.evidence.failureCount << ' '
                                << action.evidence.unknownCount << ' '
                                << action.evidence.timingPhase << ' '
                                << action.evidence.inputSequenceSignature << ' '
                                << action.evidence.deathDeltaTicks << ' '
                                << action.evidence.interveningInputs << ' '
                                << action.evidence.verifiedRescueCount << ' '
                                << action.evidence.verifiedHarmCount << '\n';
    auto const body = payload.str();
    uint64_t checksum = 1;
    for (unsigned char byte : body)
        checksum = mix(checksum, byte);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
        return false;
    auto temporary = path;
    temporary += ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output << "EBHY3 " << checksum << '\n' << body;
    output.close();
    if (!output)
        return false;
    std::filesystem::rename(temporary, path, error);
    m_dirty = bool(error);
    return !error;
}
bool EbHybridModel::load(std::filesystem::path const& path) {
    reset();
    m_dirty = false;
    std::error_code error;
    if (std::filesystem::file_size(path, error) > 512 * 1024 || error)
        return false;
    std::ifstream input(path, std::ios::binary);
    std::string version;
    uint64_t expected = 0;
    if (!(input >> version >> expected) || (version != "EBHY1" && version != "EBHY3") ||
        input.get() != '\n')
        return false;
    std::string body{std::istreambuf_iterator<char>(input), {}};
    uint64_t checksum = 1;
    for (unsigned char byte : body)
        checksum = mix(checksum, byte);
    if (checksum != expected)
        return false;
    EbHybridModel decoded;
    std::istringstream rows(body);
    while ((rows >> std::ws).peek() != std::char_traits<char>::eof()) {
        uint64_t key;
        Action action;
        if (!(rows >> key >> action.delay >> action.count >> action.score) || !key ||
            action.delay < 0 || action.delay > 96 || action.count < (version == "EBHY1" ? 1 : 0) ||
            action.count > 255 || action.score < -256 || action.score > 192)
            return false;
        if (version == "EBHY3") {
            auto& e = action.evidence;
            if (!(rows >> e.successCount >> e.failureCount >> e.unknownCount >> e.timingPhase >>
                  e.inputSequenceSignature >> e.deathDeltaTicks >> e.interveningInputs >>
                  e.verifiedRescueCount >> e.verifiedHarmCount))
                return false;
            for (int count : {e.successCount, e.failureCount, e.unknownCount, e.interveningInputs,
                              e.verifiedRescueCount, e.verifiedHarmCount})
                if (count < 0 || count > 65535)
                    return false;
            if (e.timingPhase < -1 || e.timingPhase > 7 || e.inputSequenceSignature < -1 ||
                e.inputSequenceSignature > 255 || e.deathDeltaTicks < -1 || e.deathDeltaTicks > 96)
                return false;
        }
        auto& set = decoded.m_sets[key % m_sets.size()];
        auto spot = std::find_if(set.begin(), set.end(),
                                 [key](auto const& item) { return item.key == key || !item.key; });
        if (spot == set.end())
            return false;
        spot->key = key;
        auto slot = std::find_if(spot->actions.begin(), spot->actions.end(), [&](auto const& item) {
            return item.delay == action.delay || item.delay < 0;
        });
        if (slot == spot->actions.end() || slot->delay >= 0)
            return false;
        *slot = action;
    }
    *this = decoded;
    return true;
}
