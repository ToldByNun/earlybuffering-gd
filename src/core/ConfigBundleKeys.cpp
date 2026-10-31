#include "ConfigBundleKeys.hpp"

#include <cstring>
#include <string>
#include <unordered_set>

// File-local table storage (class, not a project namespace).
class ConfigBundleKeysTable {
public:
    static ConfigBundleKeys::Entry const kEntries[127];
    static constexpr int kEntryCount = 127;

private:
    ConfigBundleKeysTable() = default;
};

ConfigBundleKeys::Entry const ConfigBundleKeysTable::kEntries[127] = {
    { "enabled", ConfigBundleKeys::Kind::Bool },
    { "preset", ConfigBundleKeys::Kind::String },
    { "safe-mode", ConfigBundleKeys::Kind::Bool },
    { "show-overlay", ConfigBundleKeys::Kind::Bool },
    { "debug-logging", ConfigBundleKeys::Kind::Bool },
    { "perf-trace", ConfigBundleKeys::Kind::Bool },
    { "lag-probe", ConfigBundleKeys::Kind::Bool },
    { "show-trajectory", ConfigBundleKeys::Kind::Bool },
    { "show-bake-path", ConfigBundleKeys::Kind::Bool },
    { "show-pause-bake", ConfigBundleKeys::Kind::Bool },
    { "show-pause-hub", ConfigBundleKeys::Kind::Bool },
    { "save-feedback-sound", ConfigBundleKeys::Kind::Bool },
    { "save-feedback-color", ConfigBundleKeys::Kind::Bool },
    { "save-feedback-counter", ConfigBundleKeys::Kind::Bool },
    { "save-icon-color", ConfigBundleKeys::Kind::Color },
    { "assist-only-practice", ConfigBundleKeys::Kind::Bool },
    { "assist-startpos-only", ConfigBundleKeys::Kind::Bool },
    { "assist-percent-min", ConfigBundleKeys::Kind::Int },
    { "assist-percent-max", ConfigBundleKeys::Kind::Int },
    { "assist-off-1-from", ConfigBundleKeys::Kind::Float },
    { "assist-off-1-to", ConfigBundleKeys::Kind::Float },
    { "assist-off-2-from", ConfigBundleKeys::Kind::Float },
    { "assist-off-2-to", ConfigBundleKeys::Kind::Float },
    { "assist-off-3-from", ConfigBundleKeys::Kind::Float },
    { "assist-off-3-to", ConfigBundleKeys::Kind::Float },
    { "assist-off-4-from", ConfigBundleKeys::Kind::Float },
    { "assist-off-4-to", ConfigBundleKeys::Kind::Float },
    { "assist-off-5-from", ConfigBundleKeys::Kind::Float },
    { "assist-off-5-to", ConfigBundleKeys::Kind::Float },
    { "assist-off-6-from", ConfigBundleKeys::Kind::Float },
    { "assist-off-6-to", ConfigBundleKeys::Kind::Float },
    { "window-unit", ConfigBundleKeys::Kind::String },
    { "ticks-all", ConfigBundleKeys::Kind::Int },
    { "ticks-wave", ConfigBundleKeys::Kind::Int },
    { "death-slack", ConfigBundleKeys::Kind::Int },
    { "look-ahead-cap", ConfigBundleKeys::Kind::Int },
    { "assist-mode", ConfigBundleKeys::Kind::String },
    { "assist-pick", ConfigBundleKeys::Kind::String },
    { "bake-radius", ConfigBundleKeys::Kind::Int },
    { "cps-limit", ConfigBundleKeys::Kind::Int },
    { "min-click-interval-ms", ConfigBundleKeys::Kind::Int },
    { "assist-orbs", ConfigBundleKeys::Kind::Bool },
    { "sim-budget-us", ConfigBundleKeys::Kind::Int },
    { "tick-budget", ConfigBundleKeys::Kind::Int },
    { "assist-press", ConfigBundleKeys::Kind::Bool },
    { "assist-release", ConfigBundleKeys::Kind::Bool },
    { "smart-depth", ConfigBundleKeys::Kind::Int },
    { "smart-combos", ConfigBundleKeys::Kind::Int },
    { "smart-horizon", ConfigBundleKeys::Kind::Int },
    { "robust-k", ConfigBundleKeys::Kind::Int },
    { "robust-jitter", ConfigBundleKeys::Kind::Bool },
    { "auto-tune", ConfigBundleKeys::Kind::Bool },
    { "self-bake", ConfigBundleKeys::Kind::Bool },
    { "self-bake-normal", ConfigBundleKeys::Kind::Bool },
    { "self-bake-refine", ConfigBundleKeys::Kind::Bool },
    { "trigger-bake", ConfigBundleKeys::Kind::Bool },
    { "trigger-bake-skip-dynamic", ConfigBundleKeys::Kind::Bool },
    { "assist-verify", ConfigBundleKeys::Kind::String },
    { "cube-enable", ConfigBundleKeys::Kind::Bool },
    { "cube-overwrite", ConfigBundleKeys::Kind::Bool },
    { "cube-ticks", ConfigBundleKeys::Kind::Int },
    { "cube-cap", ConfigBundleKeys::Kind::Int },
    { "cube-slack", ConfigBundleKeys::Kind::Int },
    { "cube-latency", ConfigBundleKeys::Kind::Float },
    { "ship-enable", ConfigBundleKeys::Kind::Bool },
    { "ship-overwrite", ConfigBundleKeys::Kind::Bool },
    { "ship-ticks", ConfigBundleKeys::Kind::Int },
    { "ship-cap", ConfigBundleKeys::Kind::Int },
    { "ship-slack", ConfigBundleKeys::Kind::Int },
    { "ship-latency", ConfigBundleKeys::Kind::Float },
    { "ball-enable", ConfigBundleKeys::Kind::Bool },
    { "ball-overwrite", ConfigBundleKeys::Kind::Bool },
    { "ball-ticks", ConfigBundleKeys::Kind::Int },
    { "ball-cap", ConfigBundleKeys::Kind::Int },
    { "ball-slack", ConfigBundleKeys::Kind::Int },
    { "ball-latency", ConfigBundleKeys::Kind::Float },
    { "ufo-enable", ConfigBundleKeys::Kind::Bool },
    { "ufo-overwrite", ConfigBundleKeys::Kind::Bool },
    { "ufo-ticks", ConfigBundleKeys::Kind::Int },
    { "ufo-cap", ConfigBundleKeys::Kind::Int },
    { "ufo-slack", ConfigBundleKeys::Kind::Int },
    { "ufo-latency", ConfigBundleKeys::Kind::Float },
    { "wave-enable", ConfigBundleKeys::Kind::Bool },
    { "wave-overwrite", ConfigBundleKeys::Kind::Bool },
    { "wave-ticks", ConfigBundleKeys::Kind::Int },
    { "wave-cap", ConfigBundleKeys::Kind::Int },
    { "wave-slack", ConfigBundleKeys::Kind::Int },
    { "wave-latency", ConfigBundleKeys::Kind::Float },
    { "robot-enable", ConfigBundleKeys::Kind::Bool },
    { "robot-overwrite", ConfigBundleKeys::Kind::Bool },
    { "robot-ticks", ConfigBundleKeys::Kind::Int },
    { "robot-cap", ConfigBundleKeys::Kind::Int },
    { "robot-slack", ConfigBundleKeys::Kind::Int },
    { "robot-latency", ConfigBundleKeys::Kind::Float },
    { "spider-enable", ConfigBundleKeys::Kind::Bool },
    { "spider-overwrite", ConfigBundleKeys::Kind::Bool },
    { "spider-ticks", ConfigBundleKeys::Kind::Int },
    { "spider-cap", ConfigBundleKeys::Kind::Int },
    { "spider-slack", ConfigBundleKeys::Kind::Int },
    { "spider-latency", ConfigBundleKeys::Kind::Float },
    { "swing-enable", ConfigBundleKeys::Kind::Bool },
    { "swing-overwrite", ConfigBundleKeys::Kind::Bool },
    { "swing-ticks", ConfigBundleKeys::Kind::Int },
    { "swing-cap", ConfigBundleKeys::Kind::Int },
    { "swing-slack", ConfigBundleKeys::Kind::Int },
    { "swing-latency", ConfigBundleKeys::Kind::Float },
    { "hotkey-cycle", ConfigBundleKeys::Kind::Keybind },
    { "hotkey-debug", ConfigBundleKeys::Kind::Keybind },
    { "hotkey-reset-level", ConfigBundleKeys::Kind::Keybind },
    { "hotkey-auto-tune", ConfigBundleKeys::Kind::Keybind },
    { "hotkey-assist-mode", ConfigBundleKeys::Kind::Keybind },
    { "hotkey-trajectory", ConfigBundleKeys::Kind::Keybind },
    { "dev-record-enable", ConfigBundleKeys::Kind::Bool },
    { "dev-record-count", ConfigBundleKeys::Kind::Int },
    { "dev-count-empty-attempts", ConfigBundleKeys::Kind::Bool },
    { "dev-freeze-world", ConfigBundleKeys::Kind::Bool },
    { "dev-disable-random", ConfigBundleKeys::Kind::Bool },
    { "dev-eval-on-death", ConfigBundleKeys::Kind::Bool },
    { "dev-eval-grid", ConfigBundleKeys::Kind::String },
    { "dev-eval-horizon", ConfigBundleKeys::Kind::Int },
    { "dev-eval-tail", ConfigBundleKeys::Kind::Int },
    { "dev-objective", ConfigBundleKeys::Kind::String },
    { "dev-eval-require-deps", ConfigBundleKeys::Kind::Bool },
    { "dev-record-cands", ConfigBundleKeys::Kind::Bool },
    { "dev-eval-tick-budget", ConfigBundleKeys::Kind::Int },
    { "dev-overlay", ConfigBundleKeys::Kind::Bool },
    { "stage-index", ConfigBundleKeys::Kind::Int }
};

ConfigBundleKeys::Entry const* ConfigBundleKeys::begin() {
    return ConfigBundleKeysTable::kEntries;
}

ConfigBundleKeys::Entry const* ConfigBundleKeys::end() {
    return ConfigBundleKeysTable::kEntries + ConfigBundleKeysTable::kEntryCount;
}

int ConfigBundleKeys::count() {
    return ConfigBundleKeysTable::kEntryCount;
}

ConfigBundleKeys::Entry const* ConfigBundleKeys::find(char const* key) {
    if (!key) {
        return nullptr;
    }
    for (int i = 0; i < ConfigBundleKeysTable::kEntryCount; ++i) {
        if (std::strcmp(ConfigBundleKeysTable::kEntries[i].key, key) == 0) {
            return &ConfigBundleKeysTable::kEntries[i];
        }
    }
    return nullptr;
}

bool ConfigBundleKeys::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    expect(ConfigBundleKeysTable::kEntryCount == 127, "count-127");
    expect(
        ConfigBundleKeys::count() == ConfigBundleKeysTable::kEntryCount,
        "count-api"
    );
    expect(
        static_cast<int>(ConfigBundleKeys::end() - ConfigBundleKeys::begin())
            == ConfigBundleKeysTable::kEntryCount,
        "begin-end"
    );

    std::unordered_set<std::string> seen;
    int kindBool = 0;
    int kindInt = 0;
    int kindFloat = 0;
    int kindString = 0;
    int kindColor = 0;
    int kindKeybind = 0;
    for (int i = 0; i < ConfigBundleKeysTable::kEntryCount; ++i) {
        auto const& e = ConfigBundleKeysTable::kEntries[i];
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
    expect(
        static_cast<int>(seen.size()) == ConfigBundleKeysTable::kEntryCount,
        "unique-count"
    );
    // show-bake-path added → 49 bools (was 48).
    expect(kindBool == 49, "kind-bool-49");
    expect(kindInt == 44, "kind-int-44");
    expect(kindFloat == 20, "kind-float-20");
    expect(kindString == 7, "kind-string-7");
    expect(kindColor == 1, "kind-color-1");
    expect(kindKeybind == 6, "kind-keybind-6");

    auto const* enabled = ConfigBundleKeys::find("enabled");
    expect(enabled != nullptr, "find-enabled");
    expect(enabled && enabled->kind == Kind::Bool, "find-enabled-bool");
    auto const* bakePath = ConfigBundleKeys::find("show-bake-path");
    expect(bakePath && bakePath->kind == Kind::Bool, "find-show-bake-path");
    auto const* color = ConfigBundleKeys::find("save-icon-color");
    expect(color && color->kind == Kind::Color, "find-color");
    auto const* hotkey = ConfigBundleKeys::find("hotkey-cycle");
    expect(hotkey && hotkey->kind == Kind::Keybind, "find-keybind");
    auto const* offFrom = ConfigBundleKeys::find("assist-off-1-from");
    expect(offFrom && offFrom->kind == Kind::Float, "find-float");
    expect(ConfigBundleKeys::find("no-such-setting") == nullptr, "find-miss");
    expect(ConfigBundleKeys::find(nullptr) == nullptr, "find-null");

    if (fails == 0) {
        report = "ConfigBundleKeys selfCheck OK";
        return true;
    }
    report = std::string("ConfigBundleKeys selfCheck fails: ") + report;
    return false;
}
