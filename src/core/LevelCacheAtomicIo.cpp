#include "LevelCacheAtomicIo.hpp"

#include <Geode/utils/file.hpp>
#include <fmt/format.h>
#include <fstream>
#include <system_error>

using namespace geode::prelude;

std::filesystem::path LevelCacheAtomicIo::tmpPathFor(
    std::filesystem::path const& path
) {
    auto tmp = path;
    tmp += ".tmp";
    return tmp;
}

geode::Result<> LevelCacheAtomicIo::renameReplace(
    std::filesystem::path const& tmp,
    std::filesystem::path const& path
) {
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (!ec) return Ok();

    // Windows: replace existing.
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(tmp, path, ec);
    if (!ec) return Ok();

    std::filesystem::copy_file(
        tmp,
        path,
        std::filesystem::copy_options::overwrite_existing,
        ec
    );
    std::error_code rmEc;
    std::filesystem::remove(tmp, rmEc);
    if (!ec) return Ok();
    return Err(fmt::format(
        "atomic rename failed {} → {}: {}",
        tmp.string(),
        path.string(),
        ec.message()
    ));
}

geode::Result<> LevelCacheAtomicIo::writeJson(
    std::filesystem::path const& path,
    matjson::Value const& value
) {
    if (path.empty()) return Err("atomic write path empty");

    auto const parent = path.parent_path();
    if (!parent.empty()) {
        auto mk = file::createDirectoryAll(parent);
        if (mk.isErr()) {
            return Err(fmt::format(
                "cannot create parent {}: {}",
                parent.string(),
                mk.unwrapErr()
            ));
        }
    }

    auto const tmp = tmpPathFor(path);
    auto w = file::writeToJson(tmp, value);
    if (w.isErr()) {
        std::error_code rmEc;
        std::filesystem::remove(tmp, rmEc);
        return Err(fmt::format(
            "atomic tmp write failed {}: {}",
            tmp.string(),
            w.unwrapErr()
        ));
    }

    // Best-effort flush: reopen and flush via ofstream if needed.
    {
        std::ofstream flushProbe(tmp, std::ios::binary | std::ios::app);
        if (flushProbe) {
            flushProbe.flush();
        }
    }

    auto ren = renameReplace(tmp, path);
    if (ren.isErr()) {
        std::error_code rmEc;
        std::filesystem::remove(tmp, rmEc);
        return ren;
    }
    return Ok();
}

geode::Result<matjson::Value> LevelCacheAtomicIo::readJson(
    std::filesystem::path const& path
) {
    if (path.empty()) return Err("atomic read path empty");

    // Never treat orphan .tmp as the real document.
    if (!existsRegular(path)) {
        return Err(fmt::format("missing {}", path.string()));
    }

    auto res = file::readJson(path);
    if (res.isErr()) {
        return Err(fmt::format(
            "corrupt or unreadable {}: {}",
            path.string(),
            res.unwrapErr()
        ));
    }
    return res;
}

bool LevelCacheAtomicIo::existsRegular(std::filesystem::path const& path) {
    if (path.empty()) return false;
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec) && !ec;
}

bool LevelCacheAtomicIo::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    {
        auto const tmpName = tmpPathFor(std::filesystem::path("a.json")).string();
        expect(tmpName.find(".tmp") != std::string::npos, "tmp-suffix");
    }

    std::filesystem::path dir;
    try {
        dir = std::filesystem::temp_directory_path() / "earlybuffer-atomic-io-selfcheck";
    }
    catch (...) {
        expect(false, "temp-dir");
        report = fmt::format("LevelCacheAtomicIo selfCheck {} fails: {}", fails, report);
        return false;
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    ec.clear();
    std::filesystem::create_directories(dir, ec);
    expect(!ec, "mkdir");

    auto const path = dir / "doc.json";
    auto const orphanTmp = tmpPathFor(path);

    matjson::Value root = matjson::Value::object();
    root["schema"] = 1;
    root["hello"] = "world";

    auto w = writeJson(path, root);
    expect(w.isOk(), "write-ok");
    expect(existsRegular(path), "exists-after-write");
    expect(!existsRegular(orphanTmp), "no-tmp-left");

    auto r = readJson(path);
    expect(r.isOk(), "read-ok");
    if (r.isOk()) {
        auto const v = r.unwrap();
        expect(v["hello"].asString().unwrapOr("") == "world", "roundtrip");
    }

    // Orphan .tmp only → read must miss (not treat tmp as doc).
    std::filesystem::remove(path, ec);
    auto orphanWrite = file::writeToJson(orphanTmp, root);
    expect(orphanWrite.isOk(), "orphan-tmp-write");
    auto miss = readJson(path);
    expect(miss.isErr(), "orphan-tmp-ignored");
    std::filesystem::remove(orphanTmp, ec);

    // Corrupt JSON → Err, no crash.
    {
        std::ofstream bad(path, std::ios::binary | std::ios::trunc);
        bad << "{not-json";
        bad.close();
    }
    auto corrupt = readJson(path);
    expect(corrupt.isErr(), "corrupt-err");

    // Simulated write-fail: target under a file-as-parent (cannot create).
    auto const blocked = dir / "as-file";
    {
        std::ofstream f(blocked, std::ios::binary | std::ios::trunc);
        f << "x";
    }
    auto failWrite = writeJson(blocked / "child.json", root);
    expect(failWrite.isErr(), "write-fail-parent");

    std::filesystem::remove_all(dir, ec);

    if (fails == 0) {
        report = "LevelCacheAtomicIo selfCheck OK";
        return true;
    }
    report = fmt::format("LevelCacheAtomicIo selfCheck {} fails: {}", fails, report);
    return false;
}
