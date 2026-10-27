#include "ReplayImport.hpp"

#include <gdr/gdr.hpp>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <span>

using namespace geode::prelude;



struct FrireGdr : gdr::Replay<FrireGdr, gdr::Input<>> {
    FrireGdr() : Replay("frirecracker", 1) {}
};

std::string lowerExt(std::filesystem::path const& path) {
    auto e = path.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
}

std::string fileStemName(std::filesystem::path const& path) {
    return path.filename().string();
}

bool isMhrJsonName(std::filesystem::path const& path) {
    auto name = path.filename().string();
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return name.ends_with(".mhr.json") || (lowerExt(path) == ".json" && name.find("mhr") != std::string::npos);
}

template <class T>
T readLE(std::ifstream& f) {
    T v{};
    f.read(reinterpret_cast<char*>(&v), sizeof(T));
    return v;
}

template <class T>
T readBE(std::ifstream& f) {
    uint8_t b[sizeof(T)];
    f.read(reinterpret_cast<char*>(b), sizeof(T));
    T v = 0;
    for (size_t i = 0; i < sizeof(T); ++i) {
        v = static_cast<T>((v << 8) | b[i]);
    }
    return v;
}


Result<ImportedReplay> ReplayImport::load(std::filesystem::path const& path) {
    if (!std::filesystem::exists(path)) return Err("File not found");

    auto ext = lowerExt(path);
    auto name = path.filename().string();
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (ext == ".gdr" || ext == ".gdr2") return loadGdr(path);
    if (name.ends_with(".mhr.json") || isMhrJsonName(path)) return loadMhrJson(path);
    if (ext == ".mhr") {
        // JSON saved as .mhr, or binary HACK magic
        std::ifstream probe(path, std::ios::binary);
        if (!probe) return Err("Failed to open file");
        char head[8]{};
        probe.read(head, 8);
        if (head[0] == '{' || head[0] == '[') return loadMhrJson(path);
        return loadMhrBin(path);
    }
    if (ext == ".slc") return loadSlc(path);
    if (ext == ".json") {
        // Try MHR-shaped JSON, else fail
        auto mhr = loadMhrJson(path);
        if (mhr.isOk()) return mhr;
        return Err("Unsupported .json (expected MegaHack meta/events layout)");
    }

    // Magic sniff
    std::ifstream f(path, std::ios::binary);
    char magic[4]{};
    f.read(magic, 4);
    if (std::string_view(magic, 3) == "GDR") return loadGdr(path);
    if (std::string_view(magic, 4) == "SILL") return loadSlc(path);
    if (magic[0] == '{') return loadMhrJson(path);

    return Err(fmt::format("Unsupported replay format: {}", path.filename().string()));
}

Result<ImportedReplay> ReplayImport::loadGdr(std::filesystem::path const& path) {
    auto res = FrireGdr::importData(path);
    if (res.isErr()) return Err(res.unwrapErr());

    auto const& r = res.unwrap();
    ImportedReplay out;
    out.tps = r.framerate > 0.0 ? r.framerate : 240.0;
    out.sourceName = fileStemName(path);
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
    std::stable_sort(out.events.begin(), out.events.end(), [](auto const& a, auto const& b) {
        return a.frame < b.frame;
    });
    return Ok(std::move(out));
}

Result<ImportedReplay> ReplayImport::loadMhrJson(std::filesystem::path const& path) {
    auto jsonRes = file::readJson(path);
    if (jsonRes.isErr()) return Err(jsonRes.unwrapErr());
    auto root = jsonRes.unwrap();

    ImportedReplay out;
    out.sourceName = fileStemName(path);
    out.format = "mhr.json";

    double fps = 240.0;
    if (root.contains("meta") && root["meta"].isObject() && root["meta"].contains("fps")) {
        fps = root["meta"]["fps"].asDouble().unwrapOr(240.0);
    }
    else if (root.contains("fps")) {
        fps = root["fps"].asDouble().unwrapOr(240.0);
    }
    out.tps = fps > 0.0 ? fps : 240.0;

    matjson::Value const* events = nullptr;
    if (root.contains("events") && root["events"].isArray()) events = &root["events"];
    else if (root.contains("inputs") && root["inputs"].isArray()) events = &root["inputs"];
    else return Err("MHR JSON: missing 'events' array");

    for (auto const& ev : *events) {
        if (!ev.isObject()) continue;
        if (!ev.contains("frame")) continue;
        int frame = static_cast<int>(ev["frame"].asInt().unwrapOr(0));
        bool down = false;
        if (ev.contains("down")) down = ev["down"].asBool().unwrapOr(false);
        else if (ev.contains("hold")) down = ev["hold"].asBool().unwrapOr(false);
        else continue;

        bool p2 = false;
        if (ev.contains("p2")) p2 = ev["p2"].asBool().unwrapOr(false);
        else if (ev.contains("player2")) p2 = ev["player2"].asBool().unwrapOr(false);

        int button = 1;
        if (ev.contains("button")) button = static_cast<int>(ev["button"].asInt().unwrapOr(1));

        out.events.push_back(ReplayEvent{ frame, button, p2, down });
    }

    std::stable_sort(out.events.begin(), out.events.end(), [](auto const& a, auto const& b) {
        return a.frame < b.frame;
    });
    if (out.events.empty()) return Err("MHR JSON: no usable events");
    return Ok(std::move(out));
}

Result<ImportedReplay> ReplayImport::loadMhrBin(std::filesystem::path const& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return Err("Failed to open MHR binary");

    // Detect JSON masquerading as .mhr
    {
        auto pos = f.tellg();
        char c = 0;
        f.read(&c, 1);
        f.seekg(pos);
        if (c == '{' || c == '[') return loadMhrJson(path);
    }

    uint32_t magic = readBE<uint32_t>(f);
    if (magic != 0x4841434Bu) { // "HACK"
        return Err(fmt::format("Unknown MHR binary magic {:08x} (export as .mhr.json?)", magic));
    }

    f.seekg(12);
    uint32_t fpsU = readBE<uint32_t>(f);
    ImportedReplay out;
    out.tps = fpsU > 0 ? static_cast<double>(fpsU) : 240.0;
    out.sourceName = fileStemName(path);
    out.format = "mhr";

    f.seekg(28);
    uint32_t num = readBE<uint32_t>(f);
    out.events.reserve(num);
    for (uint32_t i = 0; i < num; ++i) {
        f.seekg(2, std::ios::cur);
        uint8_t downB = 0, p1B = 0;
        f.read(reinterpret_cast<char*>(&downB), 1);
        f.read(reinterpret_cast<char*>(&p1B), 1);
        uint32_t frame = readBE<uint32_t>(f);
        f.seekg(24, std::ios::cur);
        if (!f) break;
        out.events.push_back(ReplayEvent{
            static_cast<int>(frame),
            1,
            p1B != 0, // p1==0 means player1 in zcb3; p1B==0 → !player2
            downB == 1
        });
        // Fix: zcb3 says p1 = (read == 0); so player2 = !p1 = (p1B != 0). Correct above.
    }

    // Correct player2: p1 true when byte==0 → player2 = (p1B != 0) is wrong.
    // p1 = (byte == 0) → player2 = (byte != 0). Yes that's what we have.
    // Wait zcb3: `let p1 = reader.read_u8()? == 0` then if p1 process p1. So byte 0 = p1, nonzero = p2.
    // player2 = (p1B != 0). Good.

    std::stable_sort(out.events.begin(), out.events.end(), [](auto const& a, auto const& b) {
        return a.frame < b.frame;
    });
    if (out.events.empty()) return Err("MHR binary: no actions");
    return Ok(std::move(out));
}

Result<ImportedReplay> ReplayImport::loadSlc(std::filesystem::path const& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return Err("Failed to open SLC");

    char header[4]{};
    f.read(header, 4);
    if (std::string_view(header, 4) != "SILL") {
        return Err("Unsupported SLC (need v2 SILL header; convert v3 to .gdr2 or re-export)");
    }

    ImportedReplay out;
    out.sourceName = fileStemName(path);
    out.format = "slc";

    double tps = 0.0;
    f.read(reinterpret_cast<char*>(&tps), sizeof(double));
    out.tps = tps > 0.0 ? tps : 240.0;

    uint64_t metaSize = 0;
    f.read(reinterpret_cast<char*>(&metaSize), sizeof(uint64_t));
    if (metaSize > 0) f.seekg(static_cast<std::streamoff>(metaSize), std::ios::cur);

    uint64_t length = 0;
    f.read(reinterpret_cast<char*>(&length), sizeof(uint64_t));
    if (length > 10'000'000) return Err("SLC: absurd input count");

    uint64_t blobCount = 0;
    f.read(reinterpret_cast<char*>(&blobCount), sizeof(uint64_t));
    if (blobCount > 1'000'000) return Err("SLC: absurd blob count");

    struct Blob {
        uint64_t byteSize = 1;
        uint64_t start = 0;
        uint64_t length = 0;
    };
    std::vector<Blob> blobs(blobCount);
    for (auto& b : blobs) {
        f.read(reinterpret_cast<char*>(&b.byteSize), sizeof(uint64_t));
        f.read(reinterpret_cast<char*>(&b.start), sizeof(uint64_t));
        f.read(reinterpret_cast<char*>(&b.length), sizeof(uint64_t));
        if (b.byteSize == 0 || b.byteSize > 8) return Err("SLC: bad blob byte size");
    }

    std::vector<uint64_t> states(length, 0);
    std::vector<double> tpsOverrides(length, 0.0);
    uint64_t frame = 0;
    out.events.reserve(length);

    for (auto const& blob : blobs) {
        for (uint64_t i = 0; i < blob.length; ++i) {
            uint64_t idx = blob.start + i;
            if (idx >= length) return Err("SLC: blob index OOB");
            uint64_t state = 0;
            f.read(reinterpret_cast<char*>(&state), static_cast<std::streamsize>(blob.byteSize));
            states[idx] = state;

            uint64_t delta = state >> 5;
            uint8_t type = static_cast<uint8_t>((state & 0b11100) >> 2);
            bool p2 = (state & 2) != 0;
            bool hold = (state & 1) != 0;
            frame += delta;

            if (type == 7) { // TPS
                double ntps = 0.0;
                f.read(reinterpret_cast<char*>(&ntps), sizeof(double));
                if (ntps > 0.0) out.tps = ntps;
                continue;
            }
            if (type == 0 || type >= 4) continue; // Skip / Restart / Death
            // 1 Jump, 2 Left, 3 Right
            out.events.push_back(ReplayEvent{
                static_cast<int>(frame),
                static_cast<int>(type),
                p2,
                hold
            });
        }
    }

    char footer[3]{};
    f.read(footer, 3);
    if (std::string_view(footer, 3) != "EOM") {
        // tolerate missing footer if we got events
        if (out.events.empty()) return Err("SLC: bad footer / empty");
    }

    std::stable_sort(out.events.begin(), out.events.end(), [](auto const& a, auto const& b) {
        return a.frame < b.frame;
    });
    if (out.events.empty()) return Err("SLC: no jump/move inputs");
    return Ok(std::move(out));
}
