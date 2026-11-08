#include "replay_import.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <gdr/gdr.hpp>
#include <span>

std::string ReplayImportImplementation::lowerExt(std::filesystem::path const& path) {
    auto entry = path.extension().string();
    std::transform(entry.begin(), entry.end(), entry.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return entry;
}

std::string ReplayImportImplementation::fileStemName(std::filesystem::path const& path) {
    return path.filename().string();
}

bool ReplayImportImplementation::isMhrJsonName(std::filesystem::path const& path) {
    auto name = path.filename().string();
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return name.ends_with(".mhr.json") || (ReplayImportImplementation::lowerExt(path) == ".json" &&
                                           name.find("mhr") != std::string::npos);
}

template <class T> T readLE(std::ifstream& f) {
    T value{};
    f.read(reinterpret_cast<char*>(&value), sizeof(T));
    return value;
}

template <class T> T readBE(std::ifstream& f) {
    uint8_t b[sizeof(T)];
    f.read(reinterpret_cast<char*>(b), sizeof(T));
    T value = 0;
    for (size_t index = 0; index < sizeof(T); ++index) {
        value = static_cast<T>((value << 8) | b[index]);
    }
    return value;
}

void ReplayImportImplementation::normalizeSoloPlayer2(ImportedReplay& out) {
    // MegaHack GDR often stores almost all inputs as player2 (p1Inputs≈0 or
    // tiny P1 stub). Solo bake/assist only drives P1 — flip majority-P2 to P1.
    int playerOne = 0;
    int playerTwo = 0;
    for (auto const& e : out.events) {
        if (e.player2)
            ++playerTwo;
        else
            ++playerOne;
    }
    if (playerTwo > 0 && playerTwo >= playerOne) {
        for (auto& e : out.events)
            e.player2 = false;
    }
}

geode::Result<ImportedReplay> ReplayImport::load(std::filesystem::path const& path) {
    if (!std::filesystem::exists(path))
        return geode::Err("File not found");

    auto ext = ReplayImportImplementation::lowerExt(path);
    auto name = path.filename().string();
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });

    auto finish = [](geode::Result<ImportedReplay> result) -> geode::Result<ImportedReplay> {
        if (result.isOk())
            ReplayImportImplementation::normalizeSoloPlayer2(result.unwrap());
        return result;
    };

    if (ext == ".gdr" || ext == ".gdr2")
        return finish(loadGdr(path));
    if (name.ends_with(".mhr.json") || ReplayImportImplementation::isMhrJsonName(path))
        return finish(loadMhrJson(path));
    if (ext == ".mhr") {
        // JSON saved as .mhr, or binary HACK magic
        std::ifstream probe(path, std::ios::binary);
        if (!probe)
            return geode::Err("Failed to open file");
        char head[8]{};
        probe.read(head, 8);
        if (head[0] == '{' || head[0] == '[')
            return finish(loadMhrJson(path));
        return finish(loadMhrBin(path));
    }
    if (ext == ".slc")
        return finish(loadSlc(path));
    if (ext == ".json") {
        // Try MHR-shaped JSON, else fail
        auto mhr = loadMhrJson(path);
        if (mhr.isOk()) {
            ReplayImportImplementation::normalizeSoloPlayer2(mhr.unwrap());
            return mhr;
        }
        return geode::Err("Unsupported .json (expected MegaHack meta/events layout)");
    }

    // Magic sniff
    std::ifstream f(path, std::ios::binary);
    char magic[4]{};
    f.read(magic, 4);
    if (std::string_view(magic, 3) == "GDR")
        return finish(loadGdr(path));
    if (std::string_view(magic, 4) == "SILL")
        return finish(loadSlc(path));
    if (!(magic[0] == '{')) {
        return geode::Err(fmt::format("Unsupported replay format: {}", path.filename().string()));
    }
    return finish(loadMhrJson(path));
}

geode::Result<ImportedReplay> ReplayImport::loadGdr(std::filesystem::path const& path) {
    auto result = ImportedReplayData::importData(path);
    if (result.isErr())
        return geode::Err(result.unwrapErr());

    auto const& r = result.unwrap();
    ImportedReplay out;
    out.tps = r.framerate > 0.0 ? r.framerate : 240.0;
    out.sourceName = ReplayImportImplementation::fileStemName(path);
    out.format = "gdr2";
    out.events.reserve(r.inputs.size());
    for (auto const& in : r.inputs) {
        ReplayEvent e;
        e.frame = static_cast<int>(in.frame);
        e.button = in.button == 0 ? 1 : static_cast<int>(in.button);
        e.player2 = in.player2;
        e.down = in.down;
        out.events.push_back(e);
    }
    std::stable_sort(out.events.begin(), out.events.end(),
                     [](auto const& a, auto const& b) { return a.frame < b.frame; });
    return geode::Ok(std::move(out));
}

geode::Result<ImportedReplay> ReplayImport::loadMhrJson(std::filesystem::path const& path) {
    auto jsonRes = geode::utils::file::readJson(path);
    if (jsonRes.isErr())
        return geode::Err(jsonRes.unwrapErr());
    auto root = jsonRes.unwrap();

    ImportedReplay out;
    out.sourceName = ReplayImportImplementation::fileStemName(path);
    out.format = "mhr.json";

    double fps = 240.0;
    if (root.contains("meta") && root["meta"].isObject() && root["meta"].contains("fps")) {
        fps = root["meta"]["fps"].asDouble().unwrapOr(240.0);
    } else if (root.contains("fps")) {
        fps = root["fps"].asDouble().unwrapOr(240.0);
    }
    out.tps = fps > 0.0 ? fps : 240.0;

    matjson::Value const* events = nullptr;
    if (root.contains("events") && root["events"].isArray())
        events = &root["events"];
    else if (root.contains("inputs") && root["inputs"].isArray())
        events = &root["inputs"];
    else
        return geode::Err("MHR JSON: missing 'events' array");

    for (auto const& ev : *events) {
        if (!ev.isObject())
            continue;
        if (!ev.contains("frame"))
            continue;
        int frame = static_cast<int>(ev["frame"].asInt().unwrapOr(0));
        bool down = false;
        if (ev.contains("down"))
            down = ev["down"].asBool().unwrapOr(false);
        else if (ev.contains("hold"))
            down = ev["hold"].asBool().unwrapOr(false);
        else
            continue;

        bool playerTwo = false;
        if (ev.contains("p2"))
            playerTwo = ev["p2"].asBool().unwrapOr(false);
        else if (ev.contains("player2"))
            playerTwo = ev["player2"].asBool().unwrapOr(false);

        int button = 1;
        if (ev.contains("button"))
            button = static_cast<int>(ev["button"].asInt().unwrapOr(1));

        out.events.push_back(ReplayEvent{frame, button, playerTwo, down});
    }

    std::stable_sort(out.events.begin(), out.events.end(),
                     [](auto const& a, auto const& b) { return a.frame < b.frame; });
    if (!(out.events.empty())) {
        return geode::Ok(std::move(out));
    }
    return geode::Err("MHR JSON: no usable events");
}

geode::Result<ImportedReplay> ReplayImport::loadMhrBin(std::filesystem::path const& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return geode::Err("Failed to open MHR binary");

    // Detect JSON masquerading as .mhr
    {
        auto position = f.tellg();
        char candidate = 0;
        f.read(&candidate, 1);
        f.seekg(position);
        if (candidate == '{' || candidate == '[')
            return loadMhrJson(path);
    }

    uint32_t magic = readBE<uint32_t>(f);
    if (magic != 0x4841434Bu) { // "HACK"
        return geode::Err(
            fmt::format("Unknown MHR binary magic {:08x} (export as .mhr.json?)", magic));
    }

    f.seekg(12);
    uint32_t fpsU = readBE<uint32_t>(f);
    ImportedReplay out;
    out.tps = fpsU > 0 ? static_cast<double>(fpsU) : 240.0;
    out.sourceName = ReplayImportImplementation::fileStemName(path);
    out.format = "mhr";

    f.seekg(28);
    uint32_t num = readBE<uint32_t>(f);
    out.events.reserve(num);
    for (uint32_t index = 0; index < num; ++index) {
        f.seekg(2, std::ios::cur);
        uint8_t downB = 0, p1B = 0;
        f.read(reinterpret_cast<char*>(&downB), 1);
        f.read(reinterpret_cast<char*>(&p1B), 1);
        uint32_t frame = readBE<uint32_t>(f);
        f.seekg(24, std::ios::cur);
        if (!f)
            break;
        out.events.push_back(ReplayEvent{static_cast<int>(frame), 1,
                                         p1B != 0, // p1==0 means player1 in zcb3; p1B==0 → !player2
                                         downB == 1});
        // Fix: zcb3 says p1 = (read == 0); so player2 = !p1 = (p1B != 0). Correct above.
    }

    // Correct player2: p1 true when byte==0 → player2 = (p1B != 0) is wrong.
    // p1 = (byte == 0) → player2 = (byte != 0). Yes that's what we have.
    // Wait zcb3: `let p1 = reader.read_u8()? == 0` then if p1 process p1. So byte 0 = p1, nonzero =
    // p2. player2 = (p1B != 0). Good.

    std::stable_sort(out.events.begin(), out.events.end(),
                     [](auto const& a, auto const& b) { return a.frame < b.frame; });
    if (!(out.events.empty())) {
        return geode::Ok(std::move(out));
    }
    return geode::Err("MHR binary: no actions");
}

geode::Result<ImportedReplay> ReplayImport::loadSlc(std::filesystem::path const& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return geode::Err("Failed to open SLC");

    char header[4]{};
    f.read(header, 4);
    if (std::string_view(header, 4) != "SILL") {
        return geode::Err(
            "Unsupported SLC (need v2 SILL header; convert v3 to .gdr2 or re-export)");
    }

    ImportedReplay out;
    out.sourceName = ReplayImportImplementation::fileStemName(path);
    out.format = "slc";

    double tps = 0.0;
    f.read(reinterpret_cast<char*>(&tps), sizeof(double));
    out.tps = tps > 0.0 ? tps : 240.0;

    uint64_t metaSize = 0;
    f.read(reinterpret_cast<char*>(&metaSize), sizeof(uint64_t));
    if (metaSize > 0)
        f.seekg(static_cast<std::streamoff>(metaSize), std::ios::cur);

    uint64_t length = 0;
    f.read(reinterpret_cast<char*>(&length), sizeof(uint64_t));
    if (length > 10'000'000)
        return geode::Err("SLC: absurd input count");

    uint64_t blobCount = 0;
    f.read(reinterpret_cast<char*>(&blobCount), sizeof(uint64_t));
    if (blobCount > 1'000'000)
        return geode::Err("SLC: absurd blob count");

    class Blob {
    public:
        uint64_t byteSize = 1;
        uint64_t start = 0;
        uint64_t length = 0;

    private:
    };
    std::vector<Blob> blobs(blobCount);
    for (auto& b : blobs) {
        f.read(reinterpret_cast<char*>(&b.byteSize), sizeof(uint64_t));
        f.read(reinterpret_cast<char*>(&b.start), sizeof(uint64_t));
        f.read(reinterpret_cast<char*>(&b.length), sizeof(uint64_t));
        if (b.byteSize == 0 || b.byteSize > 8)
            return geode::Err("SLC: bad blob byte size");
    }

    std::vector<uint64_t> states(length, 0);
    std::vector<double> tpsOverrides(length, 0.0);
    uint64_t frame = 0;
    out.events.reserve(length);

    for (auto const& blob : blobs) {
        for (uint64_t index = 0; index < blob.length; ++index) {
            uint64_t indexValue = blob.start + index;
            if (indexValue >= length)
                return geode::Err("SLC: blob index OOB");
            uint64_t state = 0;
            f.read(reinterpret_cast<char*>(&state), static_cast<std::streamsize>(blob.byteSize));
            states[indexValue] = state;

            uint64_t delta = state >> 5;
            uint8_t type = static_cast<uint8_t>((state & 0b11100) >> 2);
            bool playerTwo = (state & 2) != 0;
            bool hold = (state & 1) != 0;
            frame += delta;

            if (type == 7) { // TPS
                double ntps = 0.0;
                f.read(reinterpret_cast<char*>(&ntps), sizeof(double));
                if (ntps > 0.0)
                    out.tps = ntps;
                continue;
            }
            if (type == 0 || type >= 4)
                continue; // Skip / Restart / Death
            // 1 Jump, 2 Left, 3 Right
            out.events.push_back(
                ReplayEvent{static_cast<int>(frame), static_cast<int>(type), playerTwo, hold});
        }
    }

    char footer[3]{};
    f.read(footer, 3);
    if (std::string_view(footer, 3) != "EOM") {
        // tolerate missing footer if we got events
        if (out.events.empty())
            return geode::Err("SLC: bad footer / empty");
    }

    std::stable_sort(out.events.begin(), out.events.end(),
                     [](auto const& a, auto const& b) { return a.frame < b.frame; });
    if (!(out.events.empty())) {
        return geode::Ok(std::move(out));
    }
    return geode::Err("SLC: no jump/move inputs");
}
