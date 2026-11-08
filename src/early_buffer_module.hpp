#ifndef EARLY_BUFFER_MODULE_HPP
#define EARLY_BUFFER_MODULE_HPP

#include "config/stage_config.hpp"
#include "core/assist/assist_controller.hpp"
#include "core/assist/contracts/assist_contracts140.hpp"
#include "core/bake/bake_map.hpp"
#include "core/platform/save_drop_paths.hpp"
#include "core/sim/shadow_simulator.hpp"
#include "core/trigger_world/trigger_classify.hpp"
#include "core/trigger_world/trigger_world_store.hpp"
#include "debug/assist_log.hpp"
#include "debug/lag_probe.hpp"
#include "debug/perf_trace.hpp"
#include "debug/plan_gold_corpus.hpp"
#include "dev/dev_config.hpp"
#include "ui/overlays/status_overlay.hpp"
#include "ui/settings/action_button_setting.hpp"
#include "ui/settings/in_level_settings_filter.hpp"
#include "ui/visualizers/bake_path_visualizer.hpp"
#include "ui/visualizers/trajectory_visualizer.hpp"
#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>

class EarlyBufferModule {
public:
    static void onLoaded();

private:
    static void syncPerfTrace();
    static void syncLagProbe();
    static void onAnySetting();

    EarlyBufferModule() = delete;
};

#endif
