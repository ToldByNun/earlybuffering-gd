#include "trigger_world_checks.hpp"
#include "../trigger_classify.hpp"
#include "../trigger_world_store.hpp"
#include <Geode/utils/file.hpp>
#include <filesystem>
#include <fmt/format.h>
#include <string>
#include <vector>

bool EbTriggerWorldChecks::selfCheck(std::string& report) {
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok)
            return;
        ++fails;
        if (!report.empty())
            report += ", ";
        report += name;
    };

    // Null-safe write (apply/restore identity without a live GameObject).
    EbTriggerPose dummy{};
    dummy.tick = 0;
    dummy.x = 1.f;
    dummy.y = 2.f;
    dummy.rotation = 3.f;
    dummy.scaleX = 1.f;
    dummy.scaleY = 1.f;
    dummy.active = true;
    EbTriggerWorldStore::writePose(nullptr, dummy);
    expect(true, "writePose-null");
    expect(!EbTriggerWorldStore::poseMatchesObject(nullptr, dummy), "pose-match-null");
    expect(!EbTriggerWorldStore::writePoseIfChanged(nullptr, dummy), "write-if-null");

    expect(EbTriggerClassify::classify(nullptr) == TriggerKind::Ignore, "classify-null");

    auto& store = EbTriggerWorldStore::get();
    store.clear();

    EbTriggerWorldData data;
    data.levelKey = "__selfcheck__";
    data.tps = 240.0;
    data.version = EbTriggerWorldStore::kFileVersion;

    EbTriggerTrack track;
    track.uniqueId = 42;
    track.objectId = 1;
    track.kind = TriggerKind::StaticMove;
    track.ready = true;
    track.keys.push_back(EbTriggerPose{10, 100.f, 200.f, 0.f, 1.f, 1.f, true});
    track.keys.push_back(EbTriggerPose{20, 150.f, 200.f, 45.f, 1.f, 1.f, true});
    data.tracks.push_back(track);

    // Not-ready track must not sample.
    EbTriggerTrack pending;
    pending.uniqueId = 99;
    pending.objectId = 2;
    pending.kind = TriggerKind::StaticToggle;
    pending.ready = false;
    pending.keys.push_back(EbTriggerPose{0, 0.f, 0.f, 0.f, 1.f, 1.f, false});
    data.tracks.push_back(pending);

    store.setData(std::move(data));

    EbTriggerPose output{};
    // Raw store-tick sampling (no live clock).
    expect(!store.samplePoseAtStoreTick(42, 5, output), "before-first");
    expect(store.samplePoseAtStoreTick(42, 10, output) && output.x == 100.f && output.y == 200.f,
           "at-first");
    expect(store.samplePoseAtStoreTick(42, 15, output) && output.x == 100.f, "hold-mid");
    expect(store.samplePoseAtStoreTick(42, 20, output) && output.x == 150.f &&
               output.rotation == 45.f,
           "at-last");
    expect(store.samplePoseAtStoreTick(42, 99, output) && output.x == 150.f, "past-last-hold");
    expect(!store.samplePoseAtStoreTick(999, 10, output), "missing-id");
    // Replacing tracks with the same count must invalidate the lookup index.
    auto original = store.data();
    auto replacement = original;
    replacement.tracks[0].uniqueId = 43;
    store.setData(std::move(replacement));
    expect(!store.samplePoseAtStoreTick(42, 10, output), "replaced-id-gone");
    expect(store.samplePoseAtStoreTick(43, 10, output), "replacement-id-ready");
    store.setData(std::move(original));
    expect(store.samplePoseAtStoreTick(42, 10, output), "restored-id-ready");

    expect(!store.samplePoseAtStoreTick(99, 0, output), "not-ready");

    // Remap: store @ 240tps, live simDt = 1/120 → live 100 → store 200.
    expect(store.remapLiveTick(100, 1.f / 240.f) == 100, "remap-identity");
    expect(store.remapLiveTick(100, 1.f / 120.f) == 200, "remap-2x-live-dt");
    // live 10 @ 2x dt → store 20 → last key pose.
    expect(store.samplePoseAtStoreTick(42, store.remapLiveTick(10, 1.f / 120.f), output) &&
               output.x == 150.f,
           "remap-then-sample");
    // live 10 @ nominal → store 10 → first key.
    expect(store.samplePoseAtStoreTick(42, store.remapLiveTick(10, 1.f / 240.f), output) &&
               output.x == 100.f,
           "remap-id-sample");

    expect(store.readyTrackCount() == 1, "ready-count");
    expect(EbTriggerClassify::isStatic(TriggerKind::StaticToggle), "static-toggle");
    expect(std::string(EbTriggerClassify::kindName(TriggerKind::StaticToggle)) == "StaticToggle",
           "kind-name-toggle");

    // Soft ≥100k; legacy 2048 must not silently drop.
    expect(EbTriggerWorldStore::kSoftMaxTracks >= 100000, "soft-ge-100k");
    expect(EbTriggerWorldStore::kSoftMaxTracks > 2048, "soft-gt-legacy-2048");
    expect(EbTriggerWorldStore::kSoftMaxTracks == 100000, "soft-eq-100k-lock");
    expect(!EbTriggerWorldStore::wouldHitSoftTracks(2048), "2048-under-soft");
    expect(EbTriggerWorldStore::kHardMaxTracks > EbTriggerWorldStore::kSoftMaxTracks, "hard-gt-soft");
    expect(!EbTriggerWorldStore::wouldHitSoftTracks(EbTriggerWorldStore::kSoftMaxTracks - 1),
           "soft-minus-1");
    expect(EbTriggerWorldStore::wouldHitSoftTracks(EbTriggerWorldStore::kSoftMaxTracks), "soft-eq-hit");
    expect(EbTriggerWorldStore::wouldHitSoftKeys(EbTriggerWorldStore::kSoftMaxKeysPerTrack),
           "soft-keys-hit");

    // ≫2048 tracks retained (no silent legacy truncate).
    {
        store.clear();
        EbTriggerWorldData big;
        big.levelKey = "__softcap_tracks__";
        big.tps = 240.0;
        big.version = EbTriggerWorldStore::kFileVersion;
        big.tracks.reserve(3000);
        for (int index = 0; index < 3000; ++index) {
            EbTriggerTrack t;
            t.uniqueId = 10000 + index;
            t.objectId = 1;
            t.kind = TriggerKind::StaticMove;
            t.ready = true;
            t.keys.push_back(EbTriggerPose{0, 0.f, 0.f, 0.f, 1.f, 1.f, true});
            big.tracks.push_back(std::move(t));
        }
        store.setData(std::move(big));
        expect(store.trackCount() == 3000, "retain-3000-no-silent-2048");
        expect(!store.softcapHit(), "3000-not-softcap");
    }

    // Keys over soft but under hard: keep all, flag softcap (no silent 8192 drop).
    {
        store.clear();
        EbTriggerWorldData keyed;
        keyed.levelKey = "__softcap_keys__";
        keyed.tps = 240.0;
        keyed.version = EbTriggerWorldStore::kFileVersion;
        EbTriggerTrack t;
        t.uniqueId = 7;
        t.objectId = 1;
        t.kind = TriggerKind::StaticMove;
        t.ready = true;
        size_t const nKeys = EbTriggerWorldStore::kSoftMaxKeysPerTrack + 16;
        t.keys.reserve(nKeys);
        for (size_t index = 0; index < nKeys; ++index) {
            t.keys.push_back(EbTriggerPose{static_cast<int>(index), 0.f, 0.f, 0.f, 1.f, 1.f, true});
        }
        keyed.tracks.push_back(std::move(t));
        store.setData(std::move(keyed));
        auto const* kept = store.findTrack(7);
        expect(kept && kept->keys.size() == nKeys, "retain-keys-over-soft");
        expect(store.softcapHit(), "keys-softcap-flag");
    }

    // Decimate keeps endpoints.
    {
        std::vector<EbTriggerPose> keys;
        for (int index = 0; index < 100; ++index) {
            keys.push_back(EbTriggerPose{index, static_cast<float>(index), 0.f, 0.f, 1.f, 1.f, true});
        }
        EbTriggerWorldStore::decimateKeys(keys, 10);
        expect(keys.size() <= 10 && keys.size() >= 2, "decimate-size");
        expect(keys.front().tick == 0 && keys.back().tick == 99, "decimate-ends");
    }

    store.clear();
    expect(!store.loaded() && !store.hasTracks(), "cleared");
    expect(!store.softcapHit(), "cleared-softcap");

    if (fails == 0) {
        report = "TriggerWorldStore selfCheck OK";
        return true;
    }
    report = fmt::format("TriggerWorldStore selfCheck {} fails: {}", fails, report);
    return false;
}
