#include "settings_category_map.hpp"
#include "../../../core/bundle/config_bundle_keys.hpp"
#include "../in_level_settings_filter.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <cocos-ext.h>
#include <cstring>
#include <fmt/format.h>
#include <string>

EbSettingsCategoryMap::Entry const EbSettingsCategoryMap::kEntries[EbSettingsCategoryMap::kCount] = {
    // Main — on/off, windows, smart/adaptive prediction
    {"title-general", SettingsCategory::Main},
    {"enabled", SettingsCategory::Main},
    {"safe-mode", SettingsCategory::Main},
    {"assist-mode", SettingsCategory::Main},
    {"title-window", SettingsCategory::Main},
    {"window-unit", SettingsCategory::Main},
    {"ticks-all", SettingsCategory::Main},
    {"ticks-wave", SettingsCategory::Main},
    {"title-smart", SettingsCategory::Main},
    {"smart-depth", SettingsCategory::Main},
    {"smart-combos", SettingsCategory::Main},
    {"smart-horizon", SettingsCategory::Main},
    {"robust-k", SettingsCategory::Main},
    {"robust-jitter", SettingsCategory::Main},
    {"title-adaptive", SettingsCategory::Main},
    {"adaptive-plan", SettingsCategory::Main},
    {"adaptive-screen-ticks", SettingsCategory::Main},
    {"adaptive-verify-quantum", SettingsCategory::Main},
    {"adaptive-reserve-pct", SettingsCategory::Main},

    // Assist — where it runs + core algorithm
    {"title-scope", SettingsCategory::Assist},
    {"assist-only-practice", SettingsCategory::Assist},
    {"assist-startpos-only", SettingsCategory::Assist},
    {"assist-percent-min", SettingsCategory::Assist},
    {"assist-percent-max", SettingsCategory::Assist},
    {"title-assist-off", SettingsCategory::Assist},
    {"assist-off-1-from", SettingsCategory::Assist},
    {"assist-off-1-to", SettingsCategory::Assist},
    {"assist-off-2-from", SettingsCategory::Assist},
    {"assist-off-2-to", SettingsCategory::Assist},
    {"assist-off-3-from", SettingsCategory::Assist},
    {"assist-off-3-to", SettingsCategory::Assist},
    {"assist-off-4-from", SettingsCategory::Assist},
    {"assist-off-4-to", SettingsCategory::Assist},
    {"assist-off-5-from", SettingsCategory::Assist},
    {"assist-off-5-to", SettingsCategory::Assist},
    {"assist-off-6-from", SettingsCategory::Assist},
    {"assist-off-6-to", SettingsCategory::Assist},
    {"title-algorithm", SettingsCategory::Assist},
    {"death-slack", SettingsCategory::Assist},
    {"look-ahead-cap", SettingsCategory::Assist},
    {"assist-pick", SettingsCategory::Assist},
    {"assist-engine", SettingsCategory::Assist},
    {"cps-limit", SettingsCategory::Assist},
    {"min-click-interval-ms", SettingsCategory::Assist},
    {"assist-orbs", SettingsCategory::Assist},
    {"sim-budget-us", SettingsCategory::Assist},
    {"tick-budget", SettingsCategory::Assist},
    {"assist-press", SettingsCategory::Assist},
    {"assist-release", SettingsCategory::Assist},

    // Learn — per-level learning + config I/O
    {"title-learning", SettingsCategory::Learn},
    {"auto-tune", SettingsCategory::Learn},
    {"self-bake", SettingsCategory::Learn},
    {"self-bake-normal", SettingsCategory::Learn},
    {"self-bake-refine", SettingsCategory::Learn},
    {"title-assist-actions", SettingsCategory::Learn},
    {"reset-learn-action", SettingsCategory::Learn},
    {"export-config-action", SettingsCategory::Learn},
    {"import-config-action", SettingsCategory::Learn},
    {"browse-cloud-configs-action", SettingsCategory::Learn},

    // Bake — bake actions + trigger world
    {"title-bake-actions", SettingsCategory::Bake},
    {"import-bake-replay", SettingsCategory::Bake},
    {"cycle-assist-mode", SettingsCategory::Bake},
    {"bake-map-action", SettingsCategory::Bake},
    {"brute-bake-action", SettingsCategory::Bake},
    {"cancel-bake-action", SettingsCategory::Bake},
    {"bake-radius", SettingsCategory::Bake},
    {"title-trigger-world", SettingsCategory::Bake},
    {"trigger-bake", SettingsCategory::Bake},
    {"trigger-bake-skip-dynamic", SettingsCategory::Bake},
    {"assist-verify", SettingsCategory::Bake},

    // Modes
    {"title-cube", SettingsCategory::Modes},
    {"cube-enable", SettingsCategory::Modes},
    {"cube-overwrite", SettingsCategory::Modes},
    {"cube-ticks", SettingsCategory::Modes},
    {"cube-cap", SettingsCategory::Modes},
    {"cube-slack", SettingsCategory::Modes},
    {"cube-latency", SettingsCategory::Modes},
    {"title-ship", SettingsCategory::Modes},
    {"ship-enable", SettingsCategory::Modes},
    {"ship-overwrite", SettingsCategory::Modes},
    {"ship-ticks", SettingsCategory::Modes},
    {"ship-cap", SettingsCategory::Modes},
    {"ship-slack", SettingsCategory::Modes},
    {"ship-latency", SettingsCategory::Modes},
    {"title-ball", SettingsCategory::Modes},
    {"ball-enable", SettingsCategory::Modes},
    {"ball-overwrite", SettingsCategory::Modes},
    {"ball-ticks", SettingsCategory::Modes},
    {"ball-cap", SettingsCategory::Modes},
    {"ball-slack", SettingsCategory::Modes},
    {"ball-latency", SettingsCategory::Modes},
    {"title-ufo", SettingsCategory::Modes},
    {"ufo-enable", SettingsCategory::Modes},
    {"ufo-overwrite", SettingsCategory::Modes},
    {"ufo-ticks", SettingsCategory::Modes},
    {"ufo-cap", SettingsCategory::Modes},
    {"ufo-slack", SettingsCategory::Modes},
    {"ufo-latency", SettingsCategory::Modes},
    {"title-wave", SettingsCategory::Modes},
    {"wave-enable", SettingsCategory::Modes},
    {"wave-overwrite", SettingsCategory::Modes},
    {"wave-ticks", SettingsCategory::Modes},
    {"wave-cap", SettingsCategory::Modes},
    {"wave-slack", SettingsCategory::Modes},
    {"wave-latency", SettingsCategory::Modes},
    {"title-robot", SettingsCategory::Modes},
    {"robot-enable", SettingsCategory::Modes},
    {"robot-overwrite", SettingsCategory::Modes},
    {"robot-ticks", SettingsCategory::Modes},
    {"robot-cap", SettingsCategory::Modes},
    {"robot-slack", SettingsCategory::Modes},
    {"robot-latency", SettingsCategory::Modes},
    {"title-spider", SettingsCategory::Modes},
    {"spider-enable", SettingsCategory::Modes},
    {"spider-overwrite", SettingsCategory::Modes},
    {"spider-ticks", SettingsCategory::Modes},
    {"spider-cap", SettingsCategory::Modes},
    {"spider-slack", SettingsCategory::Modes},
    {"spider-latency", SettingsCategory::Modes},
    {"title-swing", SettingsCategory::Modes},
    {"swing-enable", SettingsCategory::Modes},
    {"swing-overwrite", SettingsCategory::Modes},
    {"swing-ticks", SettingsCategory::Modes},
    {"swing-cap", SettingsCategory::Modes},
    {"swing-slack", SettingsCategory::Modes},
    {"swing-latency", SettingsCategory::Modes},

    // Visual
    {"show-trajectory", SettingsCategory::Visual},
    {"show-bake-path", SettingsCategory::Visual},
    {"save-feedback-sound", SettingsCategory::Visual},
    {"save-feedback-color", SettingsCategory::Visual},
    {"save-feedback-counter", SettingsCategory::Visual},
    {"save-icon-color", SettingsCategory::Visual},

    // UI — chrome + accent + light diagnostics
    {"title-ui", SettingsCategory::UI},
    {"desktop-accent-color", SettingsCategory::UI},
    {"show-overlay", SettingsCategory::UI},
    {"show-pause-hub", SettingsCategory::UI},
    {"show-pause-bake", SettingsCategory::UI},
    {"desktop-classic-settings", SettingsCategory::UI},
    {"debug-logging", SettingsCategory::UI},
    {"perf-trace", SettingsCategory::UI},
    {"lag-probe", SettingsCategory::UI},

    // Keys
    {"title-keys", SettingsCategory::Keys},
    {"hotkey-desktop-menu", SettingsCategory::Keys},
    {"hotkey-debug", SettingsCategory::Keys},
    {"hotkey-reset-level", SettingsCategory::Keys},
    {"hotkey-auto-tune", SettingsCategory::Keys},
    {"hotkey-assist-mode", SettingsCategory::Keys},
    {"hotkey-trajectory", SettingsCategory::Keys},

    // Dev
    {"title-dev", SettingsCategory::Dev},
    {"title-dev-session", SettingsCategory::Dev},
    {"dev-record-enable", SettingsCategory::Dev},
    {"dev-record-count", SettingsCategory::Dev},
    {"dev-count-empty-attempts", SettingsCategory::Dev},
    {"dev-freeze-world", SettingsCategory::Dev},
    {"dev-disable-random", SettingsCategory::Dev},
    {"title-dev-eval", SettingsCategory::Dev},
    {"dev-eval-on-death", SettingsCategory::Dev},
    {"dev-eval-grid", SettingsCategory::Dev},
    {"dev-eval-horizon", SettingsCategory::Dev},
    {"dev-eval-tail", SettingsCategory::Dev},
    {"dev-objective", SettingsCategory::Dev},
    {"dev-eval-require-deps", SettingsCategory::Dev},
    {"dev-record-cands", SettingsCategory::Dev},
    {"dev-eval-tick-budget", SettingsCategory::Dev},
    {"title-dev-overlay", SettingsCategory::Dev},
    {"dev-overlay", SettingsCategory::Dev},
    {"title-dev-actions", SettingsCategory::Dev},
    {"dev-start-record-action", SettingsCategory::Dev},
    {"dev-stop-record-action", SettingsCategory::Dev},
    {"dev-eval-last-action", SettingsCategory::Dev},
    {"dev-eval-session-action", SettingsCategory::Dev},
    {"dev-export-session-action", SettingsCategory::Dev},
    {"dev-clear-traces-action", SettingsCategory::Dev},
    {"dev-copy-report-action", SettingsCategory::Dev},
    {"title-dev-diag", SettingsCategory::Dev},
    {"dev-dump-diag-action", SettingsCategory::Dev},
};

auto EbSettingsCategoryMap::begin() -> Entry const* {
    return kEntries;
}

auto EbSettingsCategoryMap::end() -> Entry const* {
    return kEntries + kCount;
}

auto EbSettingsCategoryMap::lookup(std::string const& key) -> SettingsCategory {
    for (auto const* it = begin(); it != end(); ++it) {
        if (std::strcmp(it->key, key.c_str()) == 0)
            return it->cat;
    }
    return SettingsCategory::Count;
}
