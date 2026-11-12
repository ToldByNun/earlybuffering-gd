#include "config_bundle_keys.hpp"
#include <cstring>
#include <string>
#include <unordered_set>

EbConfigBundleKeys::Entry const EbConfigBundleKeysTable::kEntries[177] = {
    {"enabled", EbConfigBundleKeys::Kind::Bool},
    {"safe-mode", EbConfigBundleKeys::Kind::Bool},
    {"show-overlay", EbConfigBundleKeys::Kind::Bool},
    {"debug-logging", EbConfigBundleKeys::Kind::Bool},
    {"perf-trace", EbConfigBundleKeys::Kind::Bool},
    {"lag-probe", EbConfigBundleKeys::Kind::Bool},
    {"show-trajectory", EbConfigBundleKeys::Kind::Bool},
    {"show-bake-path", EbConfigBundleKeys::Kind::Bool},
    {"show-pause-bake", EbConfigBundleKeys::Kind::Bool},
    {"show-pause-hub", EbConfigBundleKeys::Kind::Bool},
    {"desktop-classic-settings", EbConfigBundleKeys::Kind::Bool},
    {"desktop-accent-color", EbConfigBundleKeys::Kind::Color},
    {"save-feedback-sound", EbConfigBundleKeys::Kind::Bool},
    {"save-feedback-color", EbConfigBundleKeys::Kind::Bool},
    {"save-feedback-counter", EbConfigBundleKeys::Kind::Bool},
    {"save-icon-color", EbConfigBundleKeys::Kind::Color},
    {"assist-only-practice", EbConfigBundleKeys::Kind::Bool},
    {"assist-startpos-only", EbConfigBundleKeys::Kind::Bool},
    {"assist-percent-min", EbConfigBundleKeys::Kind::Int},
    {"assist-percent-max", EbConfigBundleKeys::Kind::Int},
    {"assist-off-ranges", EbConfigBundleKeys::Kind::String},
    {"assist-off-1-from", EbConfigBundleKeys::Kind::Float},
    {"assist-off-1-to", EbConfigBundleKeys::Kind::Float},
    {"assist-off-2-from", EbConfigBundleKeys::Kind::Float},
    {"assist-off-2-to", EbConfigBundleKeys::Kind::Float},
    {"assist-off-3-from", EbConfigBundleKeys::Kind::Float},
    {"assist-off-3-to", EbConfigBundleKeys::Kind::Float},
    {"assist-off-4-from", EbConfigBundleKeys::Kind::Float},
    {"assist-off-4-to", EbConfigBundleKeys::Kind::Float},
    {"assist-off-5-from", EbConfigBundleKeys::Kind::Float},
    {"assist-off-5-to", EbConfigBundleKeys::Kind::Float},
    {"assist-off-6-from", EbConfigBundleKeys::Kind::Float},
    {"assist-off-6-to", EbConfigBundleKeys::Kind::Float},
    {"window-unit", EbConfigBundleKeys::Kind::String},
    {"ticks-all", EbConfigBundleKeys::Kind::Int},
    {"ticks-wave", EbConfigBundleKeys::Kind::Int},
    {"death-slack", EbConfigBundleKeys::Kind::Int},
    {"look-ahead-cap", EbConfigBundleKeys::Kind::Int},
    {"assist-mode", EbConfigBundleKeys::Kind::String},
    {"assist-pick", EbConfigBundleKeys::Kind::String},
    {"assist-engine", EbConfigBundleKeys::Kind::String},
    {"bake-radius", EbConfigBundleKeys::Kind::Int},
    {"cps-limit", EbConfigBundleKeys::Kind::Int},
    {"min-click-interval-ms", EbConfigBundleKeys::Kind::Int},
    {"assist-orbs", EbConfigBundleKeys::Kind::Bool},
    {"sim-budget-us", EbConfigBundleKeys::Kind::Int},
    {"tick-budget", EbConfigBundleKeys::Kind::Int},
    {"assist-press", EbConfigBundleKeys::Kind::Bool},
    {"assist-release", EbConfigBundleKeys::Kind::Bool},
    {"smart-depth", EbConfigBundleKeys::Kind::Int},
    {"smart-combos", EbConfigBundleKeys::Kind::Int},
    {"smart-horizon", EbConfigBundleKeys::Kind::Int},
    {"robust-k", EbConfigBundleKeys::Kind::Int},
    {"robust-jitter", EbConfigBundleKeys::Kind::Bool},
    {"adaptive-plan", EbConfigBundleKeys::Kind::Bool},
    {"adaptive-screen-ticks", EbConfigBundleKeys::Kind::Int},
    {"adaptive-verify-quantum", EbConfigBundleKeys::Kind::Int},
    {"adaptive-reserve-pct", EbConfigBundleKeys::Kind::Int},
    {"auto-tune", EbConfigBundleKeys::Kind::Bool},
    {"self-bake", EbConfigBundleKeys::Kind::Bool},
    {"self-bake-normal", EbConfigBundleKeys::Kind::Bool},
    {"self-bake-refine", EbConfigBundleKeys::Kind::Bool},
    {"trigger-bake", EbConfigBundleKeys::Kind::Bool},
    {"trigger-bake-skip-dynamic", EbConfigBundleKeys::Kind::Bool},
    {"assist-verify", EbConfigBundleKeys::Kind::String},
    {"cube-enable", EbConfigBundleKeys::Kind::Bool},
    {"cube-overwrite", EbConfigBundleKeys::Kind::Bool},
    {"cube-ticks", EbConfigBundleKeys::Kind::Int},
    {"cube-cap", EbConfigBundleKeys::Kind::Int},
    {"cube-slack", EbConfigBundleKeys::Kind::Int},
    {"cube-latency", EbConfigBundleKeys::Kind::Float},
    {"cube-adaptive-overwrite", EbConfigBundleKeys::Kind::Bool},
    {"cube-adaptive-plan", EbConfigBundleKeys::Kind::Bool},
    {"cube-adaptive-screen-ticks", EbConfigBundleKeys::Kind::Int},
    {"cube-adaptive-verify-quantum", EbConfigBundleKeys::Kind::Int},
    {"cube-adaptive-reserve-pct", EbConfigBundleKeys::Kind::Int},
    {"ship-enable", EbConfigBundleKeys::Kind::Bool},
    {"ship-overwrite", EbConfigBundleKeys::Kind::Bool},
    {"ship-ticks", EbConfigBundleKeys::Kind::Int},
    {"ship-cap", EbConfigBundleKeys::Kind::Int},
    {"ship-slack", EbConfigBundleKeys::Kind::Int},
    {"ship-latency", EbConfigBundleKeys::Kind::Float},
    {"ship-adaptive-overwrite", EbConfigBundleKeys::Kind::Bool},
    {"ship-adaptive-plan", EbConfigBundleKeys::Kind::Bool},
    {"ship-adaptive-screen-ticks", EbConfigBundleKeys::Kind::Int},
    {"ship-adaptive-verify-quantum", EbConfigBundleKeys::Kind::Int},
    {"ship-adaptive-reserve-pct", EbConfigBundleKeys::Kind::Int},
    {"ball-enable", EbConfigBundleKeys::Kind::Bool},
    {"ball-overwrite", EbConfigBundleKeys::Kind::Bool},
    {"ball-ticks", EbConfigBundleKeys::Kind::Int},
    {"ball-cap", EbConfigBundleKeys::Kind::Int},
    {"ball-slack", EbConfigBundleKeys::Kind::Int},
    {"ball-latency", EbConfigBundleKeys::Kind::Float},
    {"ball-adaptive-overwrite", EbConfigBundleKeys::Kind::Bool},
    {"ball-adaptive-plan", EbConfigBundleKeys::Kind::Bool},
    {"ball-adaptive-screen-ticks", EbConfigBundleKeys::Kind::Int},
    {"ball-adaptive-verify-quantum", EbConfigBundleKeys::Kind::Int},
    {"ball-adaptive-reserve-pct", EbConfigBundleKeys::Kind::Int},
    {"ufo-enable", EbConfigBundleKeys::Kind::Bool},
    {"ufo-overwrite", EbConfigBundleKeys::Kind::Bool},
    {"ufo-ticks", EbConfigBundleKeys::Kind::Int},
    {"ufo-cap", EbConfigBundleKeys::Kind::Int},
    {"ufo-slack", EbConfigBundleKeys::Kind::Int},
    {"ufo-latency", EbConfigBundleKeys::Kind::Float},
    {"ufo-adaptive-overwrite", EbConfigBundleKeys::Kind::Bool},
    {"ufo-adaptive-plan", EbConfigBundleKeys::Kind::Bool},
    {"ufo-adaptive-screen-ticks", EbConfigBundleKeys::Kind::Int},
    {"ufo-adaptive-verify-quantum", EbConfigBundleKeys::Kind::Int},
    {"ufo-adaptive-reserve-pct", EbConfigBundleKeys::Kind::Int},
    {"wave-enable", EbConfigBundleKeys::Kind::Bool},
    {"wave-overwrite", EbConfigBundleKeys::Kind::Bool},
    {"wave-ticks", EbConfigBundleKeys::Kind::Int},
    {"wave-cap", EbConfigBundleKeys::Kind::Int},
    {"wave-slack", EbConfigBundleKeys::Kind::Int},
    {"wave-latency", EbConfigBundleKeys::Kind::Float},
    {"wave-adaptive-overwrite", EbConfigBundleKeys::Kind::Bool},
    {"wave-adaptive-plan", EbConfigBundleKeys::Kind::Bool},
    {"wave-adaptive-screen-ticks", EbConfigBundleKeys::Kind::Int},
    {"wave-adaptive-verify-quantum", EbConfigBundleKeys::Kind::Int},
    {"wave-adaptive-reserve-pct", EbConfigBundleKeys::Kind::Int},
    {"robot-enable", EbConfigBundleKeys::Kind::Bool},
    {"robot-overwrite", EbConfigBundleKeys::Kind::Bool},
    {"robot-ticks", EbConfigBundleKeys::Kind::Int},
    {"robot-cap", EbConfigBundleKeys::Kind::Int},
    {"robot-slack", EbConfigBundleKeys::Kind::Int},
    {"robot-latency", EbConfigBundleKeys::Kind::Float},
    {"robot-adaptive-overwrite", EbConfigBundleKeys::Kind::Bool},
    {"robot-adaptive-plan", EbConfigBundleKeys::Kind::Bool},
    {"robot-adaptive-screen-ticks", EbConfigBundleKeys::Kind::Int},
    {"robot-adaptive-verify-quantum", EbConfigBundleKeys::Kind::Int},
    {"robot-adaptive-reserve-pct", EbConfigBundleKeys::Kind::Int},
    {"spider-enable", EbConfigBundleKeys::Kind::Bool},
    {"spider-overwrite", EbConfigBundleKeys::Kind::Bool},
    {"spider-ticks", EbConfigBundleKeys::Kind::Int},
    {"spider-cap", EbConfigBundleKeys::Kind::Int},
    {"spider-slack", EbConfigBundleKeys::Kind::Int},
    {"spider-latency", EbConfigBundleKeys::Kind::Float},
    {"spider-adaptive-overwrite", EbConfigBundleKeys::Kind::Bool},
    {"spider-adaptive-plan", EbConfigBundleKeys::Kind::Bool},
    {"spider-adaptive-screen-ticks", EbConfigBundleKeys::Kind::Int},
    {"spider-adaptive-verify-quantum", EbConfigBundleKeys::Kind::Int},
    {"spider-adaptive-reserve-pct", EbConfigBundleKeys::Kind::Int},
    {"swing-enable", EbConfigBundleKeys::Kind::Bool},
    {"swing-overwrite", EbConfigBundleKeys::Kind::Bool},
    {"swing-ticks", EbConfigBundleKeys::Kind::Int},
    {"swing-cap", EbConfigBundleKeys::Kind::Int},
    {"swing-slack", EbConfigBundleKeys::Kind::Int},
    {"swing-latency", EbConfigBundleKeys::Kind::Float},
    {"swing-adaptive-overwrite", EbConfigBundleKeys::Kind::Bool},
    {"swing-adaptive-plan", EbConfigBundleKeys::Kind::Bool},
    {"swing-adaptive-screen-ticks", EbConfigBundleKeys::Kind::Int},
    {"swing-adaptive-verify-quantum", EbConfigBundleKeys::Kind::Int},
    {"swing-adaptive-reserve-pct", EbConfigBundleKeys::Kind::Int},
    {"hotkey-debug", EbConfigBundleKeys::Kind::Keybind},
    {"hotkey-reset-level", EbConfigBundleKeys::Kind::Keybind},
    {"hotkey-auto-tune", EbConfigBundleKeys::Kind::Keybind},
    {"hotkey-assist-mode", EbConfigBundleKeys::Kind::Keybind},
    {"hotkey-trajectory", EbConfigBundleKeys::Kind::Keybind},
    {"hotkey-desktop-menu", EbConfigBundleKeys::Kind::Keybind},
    {"dev-record-enable", EbConfigBundleKeys::Kind::Bool},
    {"dev-record-count", EbConfigBundleKeys::Kind::Int},
    {"dev-count-empty-attempts", EbConfigBundleKeys::Kind::Bool},
    {"dev-freeze-world", EbConfigBundleKeys::Kind::Bool},
    {"dev-record-hazards", EbConfigBundleKeys::Kind::Bool},
    {"dev-record-traj", EbConfigBundleKeys::Kind::Bool},
    {"dev-disable-random", EbConfigBundleKeys::Kind::Bool},
    {"dev-eval-on-death", EbConfigBundleKeys::Kind::Bool},
    {"dev-eval-grid", EbConfigBundleKeys::Kind::String},
    {"dev-eval-horizon", EbConfigBundleKeys::Kind::Int},
    {"dev-eval-tail", EbConfigBundleKeys::Kind::Int},
    {"dev-objective", EbConfigBundleKeys::Kind::String},
    {"dev-eval-require-deps", EbConfigBundleKeys::Kind::Bool},
    {"dev-record-cands", EbConfigBundleKeys::Kind::Bool},
    {"dev-eval-tick-budget", EbConfigBundleKeys::Kind::Int},
    {"dev-overlay", EbConfigBundleKeys::Kind::Bool},
    {"dev-compare-last-n", EbConfigBundleKeys::Kind::Int},
    {"dev-compare-same-level", EbConfigBundleKeys::Kind::Bool}};

EbConfigBundleKeys::Entry const* EbConfigBundleKeys::begin() {
    return EbConfigBundleKeysTable::kEntries;
}

EbConfigBundleKeys::Entry const* EbConfigBundleKeys::end() {
    return EbConfigBundleKeysTable::kEntries + EbConfigBundleKeysTable::kEntryCount;
}

int EbConfigBundleKeys::count() {
    return EbConfigBundleKeysTable::kEntryCount;
}

EbConfigBundleKeys::Entry const* EbConfigBundleKeys::find(char const* key) {
    if (!key) {
        return nullptr;
    }
    for (int index = 0; index < EbConfigBundleKeysTable::kEntryCount; ++index) {
        if (std::strcmp(EbConfigBundleKeysTable::kEntries[index].key, key) == 0) {
            return &EbConfigBundleKeysTable::kEntries[index];
        }
    }
    return nullptr;
}

bool EbConfigBundleKeys::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    expect(EbConfigBundleKeysTable::kEntryCount == 177, "count-177");
    expect(EbConfigBundleKeys::count() == EbConfigBundleKeysTable::kEntryCount, "count-api");
    expect(static_cast<int>(EbConfigBundleKeys::end() - EbConfigBundleKeys::begin()) ==
               EbConfigBundleKeysTable::kEntryCount,
           "begin-end");

    std::unordered_set<std::string> seen;
    int kindBool = 0;
    int kindInt = 0;
    int kindFloat = 0;
    int kindString = 0;
    int kindColor = 0;
    int kindKeybind = 0;
    for (int index = 0; index < EbConfigBundleKeysTable::kEntryCount; ++index) {
        auto const& e = EbConfigBundleKeysTable::kEntries[index];
        expect(e.key != nullptr && e.key[0] != '\0', "nonempty-key");
        bool const inserted = seen.insert(std::string(e.key)).second;
        expect(inserted, "unique");
        switch (e.kind) {
        case Kind::Bool:
            ++kindBool;
            break;
        case Kind::Int:
            ++kindInt;
            break;
        case Kind::Float:
            ++kindFloat;
            break;
        case Kind::String:
            ++kindString;
            break;
        case Kind::Color:
            ++kindColor;
            break;
        case Kind::Keybind:
            ++kindKeybind;
            break;
        default:
            expect(false, "kind-valid");
            break;
        }
    }
    expect(static_cast<int>(seen.size()) == EbConfigBundleKeysTable::kEntryCount, "unique-count");
    expect(kindBool == 67, "kind-bool-67");
    expect(kindInt == 70, "kind-int-70");
    expect(kindFloat == 20, "kind-float-20");
    expect(kindString == 8, "kind-string-8");
    expect(kindColor == 2, "kind-color-2");
    expect(kindKeybind == 6, "kind-keybind-6");

    auto const* enabled = EbConfigBundleKeys::find("enabled");
    expect(enabled != nullptr, "find-enabled");
    expect(enabled && enabled->kind == Kind::Bool, "find-enabled-bool");
    auto const* bakePath = EbConfigBundleKeys::find("show-bake-path");
    expect(bakePath && bakePath->kind == Kind::Bool, "find-show-bake-path");
    auto const* color = EbConfigBundleKeys::find("save-icon-color");
    expect(color && color->kind == Kind::Color, "find-color");
    auto const* accent = EbConfigBundleKeys::find("desktop-accent-color");
    expect(accent && accent->kind == Kind::Color, "find-accent-color");
    auto const* hotkey = EbConfigBundleKeys::find("hotkey-assist-mode");
    expect(hotkey && hotkey->kind == Kind::Keybind, "find-keybind");
    auto const* menuHotkey = EbConfigBundleKeys::find("hotkey-desktop-menu");
    expect(menuHotkey && menuHotkey->kind == Kind::Keybind, "find-desktop-menu-hotkey");
    auto const* classic = EbConfigBundleKeys::find("desktop-classic-settings");
    expect(classic && classic->kind == Kind::Bool, "find-classic-settings");
    auto const* offFrom = EbConfigBundleKeys::find("assist-off-1-from");
    expect(offFrom && offFrom->kind == Kind::Float, "find-float");
    auto const* offRanges = EbConfigBundleKeys::find("assist-off-ranges");
    expect(offRanges && offRanges->kind == Kind::String, "find-assist-off-ranges");
    auto const* waveAdaptive = EbConfigBundleKeys::find("wave-adaptive-plan");
    expect(waveAdaptive && waveAdaptive->kind == Kind::Bool, "find-wave-adaptive-plan");
    expect(EbConfigBundleKeys::find("preset") == nullptr, "preset-removed");
    expect(EbConfigBundleKeys::find("stage-index") == nullptr, "stage-index-removed");
    expect(EbConfigBundleKeys::find("no-such-setting") == nullptr, "find-miss");
    expect(EbConfigBundleKeys::find(nullptr) == nullptr, "find-null");

    if (fails == 0) {
        report = "ConfigBundleKeys selfCheck OK";
        return true;
    }
    report = std::string("ConfigBundleKeys selfCheck fails: ") + report;
    return false;
}
