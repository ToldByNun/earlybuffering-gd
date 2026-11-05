#include "RoutePathStore.hpp"
#include "LevelCacheAtomicIo.hpp"
#include "LevelCacheIndex.hpp"

#include <system_error>

RoutePathStore& RoutePathStore::get() { static RoutePathStore store; return store; }
void RoutePathStore::clear() { m_path = {}; m_loaded = false; m_fullKey.clear(); m_startXq = 0; }
std::filesystem::path RoutePathStore::filePath(std::string const& key, int startXq) { return LevelCacheIndex::routePath(key, startXq); }
geode::Result<> RoutePathStore::load(std::string const& key, int startXq) {
    clear(); m_fullKey = key; m_startXq = startXq;
    auto raw = LevelCacheAtomicIo::readJson(filePath(key, startXq));
    if (raw.isErr()) return geode::Err(raw.unwrapErr());
    auto parsed = RoutePath::fromJson(raw.unwrap());
    if (parsed.isErr()) return geode::Err(parsed.unwrapErr());
    m_path = std::move(parsed.unwrap()); m_loaded = true; return geode::Ok();
}
geode::Result<> RoutePathStore::save(std::string const& key, RoutePath const& path, int startXq) {
    if (path.empty() || !path.parityOk) return geode::Err("refusing empty or unvalidated route");
    auto write = LevelCacheAtomicIo::writeJson(filePath(key, startXq), path.toJson());
    if (write.isErr()) return write;
    m_path = path; m_loaded = true; m_fullKey = key; m_startXq = startXq; return geode::Ok();
}
bool RoutePathStore::removeFile(std::string const& key, int startXq, std::string& err) {
    err.clear(); std::error_code ec; std::filesystem::remove(filePath(key, startXq), ec);
    if (ec) { err = ec.message(); return false; }
    auto& current = get(); if (current.m_fullKey == key && current.m_startXq == startXq) current.clear(); return true;
}
std::string RoutePathStore::overlayBit() const { return m_loaded ? fmt::format("route={}", m_path.sampleCount()) : "route=-"; }
bool RoutePathStore::selfCheck(std::string& report) { return RoutePath::selfCheck(report); }
