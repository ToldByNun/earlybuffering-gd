#include "hybrid_model.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <tuple>
#include <utility>

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
    m_totalPulls = 0;
    m_familiar = 0;
    m_frozen = false;
    m_dirty = true;
}
int EbHybridModel::actionSlots() const {
    return std::clamp(m_maxActionsPerSpot, 1, 4);
}
void EbHybridModel::setMaxUniqueKeys(int value) {
    m_maxUniqueKeys = std::clamp(value, 32, 2048);
}
void EbHybridModel::setMaxActionsPerSpot(int value) {
    m_maxActionsPerSpot = std::clamp(value, 1, 4);
}
void EbHybridModel::setNeighborBleed(float value) {
    m_neighborBleed = std::clamp(value, 0.f, 1.f);
}
void EbHybridModel::setUcbCoefficient(float value) {
    m_ucbCoefficient = std::clamp(value, 0.f, 4.f);
}
float EbHybridModel::neighborBleed() const {
    return m_neighborBleed;
}
float EbHybridModel::ucbCoefficient() const {
    return m_ucbCoefficient;
}
unsigned EbHybridModel::totalPulls() const {
    return m_totalPulls;
}
int EbHybridModel::maxUniqueKeys() const {
    return m_maxUniqueKeys;
}
int EbHybridModel::maxActionsPerSpot() const {
    return m_maxActionsPerSpot;
}
int EbHybridModel::uniqueKeyCount() const {
    int count = 0;
    for (auto const& set : m_sets)
        for (auto const& spot : set)
            if (spot.key)
                ++count;
    return count;
}
int EbHybridModel::retention(Spot const& entry) const {
    int peak = 0;
    for (auto const& action : entry.actions)
        peak = std::max(peak, action.score);
    auto const age = std::min(64u, m_attempt - entry.attempt);
    return peak - 4 * static_cast<int>(age);
}
EbHybridModel::Spot* EbHybridModel::findWeakestSpot() {
    Spot* weakest = nullptr;
    for (auto& set : m_sets)
        for (auto& entry : set) {
            if (!entry.key)
                continue;
            if (!weakest || retention(entry) < retention(*weakest))
                weakest = &entry;
        }
    return weakest;
}
void EbHybridModel::evictIfOverCap(uint64_t incomingKey) {
    if (hasEvidence(incomingKey))
        return;
    while (uniqueKeyCount() >= m_maxUniqueKeys) {
        Spot* weakest = findWeakestSpot();
        if (!weakest || !weakest->key)
            break;
        *weakest = {};
        m_familiar = -1;
        m_dirty = true;
    }
}
void EbHybridModel::applyReward(Action& action, int reward, bool sameAttempt) {
    int const divisor =
        sameAttempt ? 20 : std::min(12, action.count + (reward < -1 ? 2 : 3));
    int const target = 64 * std::clamp(reward, -4, 3);
    action.score += (target - action.score) / divisor;
    if (!sameAttempt)
        action.count = std::min(255, action.count + 1);
    else if (action.count < 1)
        action.count = 1;
    ++m_totalPulls;
}
void EbHybridModel::softNeighbor(Spot& spot, int delay, int reward, int limit) {
    if (m_neighborBleed <= 0.f || !reward)
        return;
    int const bleedReward =
        reward > 0 ? std::max(1, int(std::lround(reward * m_neighborBleed)))
                   : std::min(-1, int(std::lround(reward * m_neighborBleed)));
    int const slots = actionSlots();
    for (int neighbor : {delay - 1, delay + 1}) {
        if (neighbor < 0 || neighbor > limit)
            continue;
        Action* slot = nullptr;
        for (int index = 0; index < slots; ++index) {
            auto& entry = spot.actions[static_cast<size_t>(index)];
            if (entry.delay == neighbor) {
                slot = &entry;
                break;
            }
            if (entry.delay < 0 && !slot)
                slot = &entry;
        }
        // Only seed empty slots or refresh an existing neighbor — never evict a
        // primary delay just to make room for bleed.
        if (!slot)
            continue;
        if (slot->delay != neighbor)
            *slot = {neighbor, 0, 0};
        int const target = 64 * std::clamp(bleedReward, -4, 3);
        slot->score += (target - slot->score) / 18;
        if (slot->count < 1)
            slot->count = 1;
    }
}
double EbHybridModel::ucbValue(Action const& action) const {
    double const visits = std::max(1, action.count);
    double const pulls = std::max(1u, m_totalPulls);
    double const bonus =
        m_ucbCoefficient * 48.0 * std::sqrt(std::log(double(pulls) + 1.0) / (visits + 1.0));
    return double(action.score) + bonus;
}
void EbHybridModel::observe(uint64_t key, int delay, int reward, Evidence const* observation) {
    if (m_frozen || !key || delay < 0 || delay > 96 || (!reward && !observation))
        return;
    evictIfOverCap(key);
    auto& set = m_sets[key % m_sets.size()];
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
        if (!hasEvidence(key) && uniqueKeyCount() >= m_maxUniqueKeys)
            return;
        *spot = {};
        spot->key = key;
    }

    int const slots = actionSlots();
    auto* action = &spot->actions.front();
    for (int index = 0; index < slots; ++index) {
        auto& entry = spot->actions[static_cast<size_t>(index)];
        if (entry.delay == delay) {
            action = &entry;
            break;
        }
        if (entry.delay < 0 || (action->delay >= 0 && entry.score < action->score))
            action = &entry;
    }
    if (action->delay != delay)
        *action = {delay, 0, 0};
    // Drop unused slots beyond the configured action cap.
    for (int index = slots; index < 4; ++index)
        spot->actions[static_cast<size_t>(index)] = {};
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
    // One independent reward update per attempt per spot keeps attempt-gated learning honest.
    if (!reward || spot->attempt == m_attempt)
        return;
    spot->attempt = m_attempt;
    applyReward(*action, reward, false);
    softNeighbor(*spot, delay, reward, 96);
    m_familiar = -1;
    m_dirty = true;
}
void EbHybridModel::noteProbeOutcome(uint64_t key, int delay, bool verifiedSafe) {
    if (m_frozen || !key || delay < 0 || delay > 96)
        return;
    evictIfOverCap(key);
    auto& set = m_sets[key % m_sets.size()];
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
        if (!hasEvidence(key) && uniqueKeyCount() >= m_maxUniqueKeys)
            return;
        *spot = {};
        spot->key = key;
    }
    int const slots = actionSlots();
    auto* action = &spot->actions.front();
    for (int index = 0; index < slots; ++index) {
        auto& entry = spot->actions[static_cast<size_t>(index)];
        if (entry.delay == delay) {
            action = &entry;
            break;
        }
        if (entry.delay < 0 || (action->delay >= 0 && entry.score < action->score))
            action = &entry;
    }
    if (action->delay != delay)
        *action = {delay, 0, 0};
    for (int index = slots; index < 4; ++index)
        spot->actions[static_cast<size_t>(index)] = {};
    auto& evidence = action->evidence;
    if (verifiedSafe)
        evidence.verifiedRescueCount = std::min(65535, evidence.verifiedRescueCount + 1);
    else
        evidence.verifiedHarmCount = std::min(65535, evidence.verifiedHarmCount + 1);
    // Immediate physics feedback may update within the same attempt (soft step).
    applyReward(*action, verifiedSafe ? 2 : -2, true);
    softNeighbor(*spot, delay, verifiedSafe ? 1 : -1, 96);
    m_familiar = -1;
    m_dirty = true;
}
void EbHybridModel::bridgeBetweenSurvivors(uint64_t key, int limit, int minimumAttempts) {
    if (m_frozen || !key || limit < 2)
        return;
    Spot* spot = nullptr;
    for (auto& entry : m_sets[key % m_sets.size()]) {
        if (entry.key == key) {
            spot = &entry;
            break;
        }
    }
    if (!spot)
        return;
    int const minCount = std::clamp(minimumAttempts, 3, 12);
    int const slots = actionSlots();
    std::vector<Action const*> goods;
    goods.reserve(static_cast<size_t>(slots));
    for (int index = 0; index < slots; ++index) {
        auto const& action = spot->actions[static_cast<size_t>(index)];
        if (action.delay < 0 || action.delay > limit)
            continue;
        if (action.count < minCount)
            continue;
        if (action.score < (action.delay == 0 ? 20 : 48))
            continue;
        goods.push_back(&action);
    }
    if (goods.size() < 2)
        return;
    std::sort(goods.begin(), goods.end(),
              [](Action const* a, Action const* b) { return a->delay < b->delay; });
    bool changed = false;
    for (size_t index = 0; index + 1 < goods.size(); ++index) {
        Action const* left = goods[index];
        Action const* right = goods[index + 1];
        if (right->delay - left->delay <= 1)
            continue;
        int const targetScore = std::min(left->score, right->score) - 12;
        int const targetCount = std::max(1, std::min(left->count, right->count) / 2);
        for (int mid = left->delay + 1; mid < right->delay; ++mid) {
            Action* slot = nullptr;
            for (int actionIndex = 0; actionIndex < slots; ++actionIndex) {
                auto& entry = spot->actions[static_cast<size_t>(actionIndex)];
                if (entry.delay == mid) {
                    slot = &entry;
                    break;
                }
                if (entry.delay < 0 && !slot)
                    slot = &entry;
            }
            if (!slot) {
                // Evict weakest slot among the configured action cap.
                slot = &spot->actions.front();
                for (int actionIndex = 1; actionIndex < slots; ++actionIndex) {
                    auto& entry = spot->actions[static_cast<size_t>(actionIndex)];
                    if (entry.score < slot->score)
                        slot = &entry;
                }
                // Never overwrite a strong survivor endpoint.
                if (slot->delay == left->delay || slot->delay == right->delay)
                    continue;
                if (slot->score >= targetScore && slot->count >= targetCount)
                    continue;
                *slot = {};
            }
            if (slot->delay != mid)
                *slot = {mid, 0, 0};
            if (slot->score < targetScore) {
                slot->score += (targetScore - slot->score) / 3;
                changed = true;
            }
            if (slot->count < targetCount) {
                slot->count = targetCount;
                changed = true;
            }
        }
    }
    if (changed) {
        m_familiar = -1;
        m_dirty = true;
    }
}
bool EbHybridModel::hasEvidence(uint64_t key) const {
    auto const& set = m_sets[key % m_sets.size()];
    return key &&
           std::any_of(set.begin(), set.end(), [key](auto const& spot) { return spot.key == key; });
}
int EbHybridModel::proposal(uint64_t key, int minimumAttempts, int limit) const {
    auto ranked = rankedDelays(key, minimumAttempts, limit, false);
    return ranked.empty() ? -1 : ranked.front();
}
std::vector<int> EbHybridModel::rankedDelays(uint64_t key, int minimumAttempts, int limit,
                                            bool explore) const {
    std::vector<int> out;
    if (!key)
        return out;
    for (auto const& spot : m_sets[key % m_sets.size()]) {
        if (spot.key != key)
            continue;
        int const minCount = std::clamp(minimumAttempts, 3, 12);
        int const slots = actionSlots();
        // mature, ucb/score, delay
        std::vector<std::tuple<bool, double, int>> known;
        known.reserve(static_cast<size_t>(slots));
        for (int index = 0; index < slots; ++index) {
            auto const& action = spot.actions[static_cast<size_t>(index)];
            if (action.delay < 0 || action.delay > limit)
                continue;
            bool const mature = action.count >= minCount &&
                                action.score >= (action.delay == 0 ? 20 : 48);
            if (!mature) {
                if (!explore || action.count < 1 || action.score < -48)
                    continue;
            }
            known.emplace_back(mature, explore ? ucbValue(action) : double(action.score),
                               action.delay);
        }
        if (known.empty())
            return out;
        std::sort(known.begin(), known.end(), [](auto const& a, auto const& b) {
            if (std::get<0>(a) != std::get<0>(b))
                return std::get<0>(a) > std::get<0>(b);
            if (std::get<1>(a) != std::get<1>(b))
                return std::get<1>(a) > std::get<1>(b);
            return std::get<2>(a) < std::get<2>(b);
        });
        for (auto const& entry : known)
            out.push_back(std::get<2>(entry));
        // Append interpolated mids between strong known delays (verify after known).
        std::vector<int> sortedDelays;
        sortedDelays.reserve(known.size());
        for (auto const& entry : known)
            if (std::get<0>(entry))
                sortedDelays.push_back(std::get<2>(entry));
        std::sort(sortedDelays.begin(), sortedDelays.end());
        for (size_t index = 0; index + 1 < sortedDelays.size(); ++index) {
            int const left = sortedDelays[index];
            int const right = sortedDelays[index + 1];
            for (int mid = left + 1; mid < right; ++mid) {
                if (mid > limit)
                    break;
                if (std::find(out.begin(), out.end(), mid) == out.end())
                    out.push_back(mid);
            }
        }
        return out;
    }
    return out;
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
    decoded.m_maxUniqueKeys = m_maxUniqueKeys;
    decoded.m_maxActionsPerSpot = m_maxActionsPerSpot;
    decoded.m_neighborBleed = m_neighborBleed;
    decoded.m_ucbCoefficient = m_ucbCoefficient;
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
        decoded.m_totalPulls += static_cast<unsigned>(std::max(0, action.count));
    }
    *this = decoded;
    return true;
}
