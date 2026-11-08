#include "cloud_stats_storage.hpp"
#include "../cloud_config_stats.hpp"
#include "../cloud_config_store.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <cctype>
#include <filesystem>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

void CloudStatsStorage::loadLocalSets(CloudConfigStats const& owner) {
    if (!owner.m_impl || owner.m_impl->localLoaded)
        return;
    owner.m_impl->localLoaded = true;
    owner.m_impl->downloaded.clear();
    owner.m_impl->rated.clear();
    owner.m_impl->myStars.clear();

    std::string err;
    std::string body;
    if (!CloudConfigStore::readTextFile(CloudConfigStatsImplementation::localStatsPath(), body,
                                        256u * 1024u, err)) {
        return;
    }
    auto parsed = matjson::parse(body);
    if (parsed.isErr())
        return;
    auto const root = parsed.unwrap();
    if (root.contains("downloaded") && root["downloaded"].isArray()) {
        for (auto const& v : root["downloaded"].asArray().unwrap()) {
            if (!v.isString())
                continue;
            auto const id = v.asString().unwrapOrDefault();
            if (CloudConfigStore::isValidPackId(id))
                owner.m_impl->downloaded.insert(id);
        }
    }
    if (root.contains("rated") && root["rated"].isArray()) {
        for (auto const& v : root["rated"].asArray().unwrap()) {
            if (!v.isString())
                continue;
            auto const id = v.asString().unwrapOrDefault();
            if (CloudConfigStore::isValidPackId(id))
                owner.m_impl->rated.insert(id);
        }
    }
    if (root.contains("myStars") && root["myStars"].isObject()) {
        for (auto const& [id, raw] : root["myStars"]) {
            if (!CloudConfigStore::isValidPackId(id))
                continue;
            int const state = static_cast<int>(raw.asInt().unwrapOr(0));
            if (state < 1 || state > 5)
                continue;
            owner.m_impl->myStars[id] = state;
            owner.m_impl->rated.insert(id);
        }
    }
}

void CloudStatsStorage::saveLocalSets(CloudConfigStats& owner) {
    if (!owner.m_impl)
        return;
    std::string dirErr;
    if (!CloudConfigStore::get().ensureDirs(dirErr))
        return;

    matjson::Value downloaded = matjson::Value::array();
    for (auto const& id : owner.m_impl->downloaded)
        downloaded.push(id);
    matjson::Value rated = matjson::Value::array();
    for (auto const& id : owner.m_impl->rated)
        rated.push(id);
    matjson::Value myStars = matjson::Value::object();
    for (auto const& [id, s] : owner.m_impl->myStars) {
        myStars[id] = s;
    }

    matjson::Value root = matjson::Value::object();
    root["downloaded"] = downloaded;
    root["rated"] = rated;
    root["myStars"] = myStars;

    std::string err;
    (void)CloudConfigStore::writeTextFile(CloudConfigStatsImplementation::localStatsPath(),
                                          root.dump(), err);
}

void CloudStatsStorage::unmarkLocal(CloudConfigStats& owner, std::string const& packId,
                                    bool downloaded, bool rated) {
    if (!owner.m_impl || !CloudConfigStore::isValidPackId(packId))
        return;
    owner.loadLocalSets();
    bool changed = false;
    if (downloaded)
        changed = owner.m_impl->downloaded.erase(packId) > 0 || changed;
    if (rated) {
        changed = owner.m_impl->rated.erase(packId) > 0 || changed;
        changed = owner.m_impl->myStars.erase(packId) > 0 || changed;
    }
    if (changed)
        owner.saveLocalSets();
}

std::string CloudStatsStorage::ensureClientId(CloudConfigStats& owner) {
    if (!owner.m_impl)
        return {};
    if (!owner.m_impl->clientId.empty() && owner.isValidClientId(owner.m_impl->clientId)) {
        return owner.m_impl->clientId;
    }

    std::string dirErr;
    (void)CloudConfigStore::get().ensureDirs(dirErr);

    std::string err;
    std::string existing;
    if (CloudConfigStore::readTextFile(CloudConfigStatsImplementation::clientIdPath(), existing,
                                       128, err)) {
        // Trim whitespace / newlines.
        while (!existing.empty() && (existing.back() == '\n' || existing.back() == '\r' ||
                                     existing.back() == ' ' || existing.back() == '\t')) {
            existing.pop_back();
        }
        size_t start = 0;
        while (start < existing.size() && (existing[start] == ' ' || existing[start] == '\t' ||
                                           existing[start] == '\r' || existing[start] == '\n')) {
            ++start;
        }
        if (start > 0)
            existing = existing.substr(start);
        if (owner.isValidClientId(existing)) {
            owner.m_impl->clientId = existing;
            return owner.m_impl->clientId;
        }
    }

    std::string const fresh = CloudConfigStatsImplementation::makeUuidV4();
    if (!owner.isValidClientId(fresh))
        return {};
    std::string writeErr;
    if (!CloudConfigStore::writeTextFile(CloudConfigStatsImplementation::clientIdPath(),
                                         fresh + "\n", writeErr)) {
        // Still use in-memory id for this session.
        owner.m_impl->clientId = fresh;
        return owner.m_impl->clientId;
    }
    owner.m_impl->clientId = fresh;
    return owner.m_impl->clientId;
}

bool CloudStatsStorage::hasDownloaded(CloudConfigStats const& owner, std::string const& packId) {
    if (!owner.m_impl || !CloudConfigStore::isValidPackId(packId))
        return false;
    owner.loadLocalSets();
    return owner.m_impl->downloaded.count(packId) > 0;
}

bool CloudStatsStorage::hasRated(CloudConfigStats const& owner, std::string const& packId) {
    if (!owner.m_impl || !CloudConfigStore::isValidPackId(packId))
        return false;
    owner.loadLocalSets();
    return owner.m_impl->rated.count(packId) > 0 || owner.m_impl->myStars.count(packId) > 0;
}

int CloudStatsStorage::myStarsFor(CloudConfigStats const& owner, std::string const& packId) {
    if (!owner.m_impl || !CloudConfigStore::isValidPackId(packId))
        return 0;
    owner.loadLocalSets();
    auto it = owner.m_impl->myStars.find(packId);
    if (!(it == owner.m_impl->myStars.end())) {
        return it->second;
    }
    return 0;
}

void CloudStatsStorage::markDownloaded(CloudConfigStats& owner, std::string const& packId) {
    if (!owner.m_impl || !CloudConfigStore::isValidPackId(packId))
        return;
    owner.loadLocalSets();
    if (owner.m_impl->downloaded.insert(packId).second) {
        owner.saveLocalSets();
    }
}

void CloudStatsStorage::markRated(CloudConfigStats& owner, std::string const& packId, int stars) {
    if (!owner.m_impl || !CloudConfigStore::isValidPackId(packId))
        return;
    owner.loadLocalSets();
    bool changed = owner.m_impl->rated.insert(packId).second;
    if (stars >= 1 && stars <= 5) {
        auto it = owner.m_impl->myStars.find(packId);
        if (it == owner.m_impl->myStars.end() || it->second != stars) {
            owner.m_impl->myStars[packId] = stars;
            changed = true;
        }
    }
    if (changed)
        owner.saveLocalSets();
}
