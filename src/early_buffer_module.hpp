#ifndef EARLY_BUFFER_MODULE_HPP
#define EARLY_BUFFER_MODULE_HPP

#include "config/stage_config.hpp"
#include "core/assist_contracts140.hpp"
#include "core/assist_controller.hpp"
#include "core/bake_map.hpp"
#include "core/save_drop_paths.hpp"
#include "core/shadow_simulator.hpp"
#include "core/trigger_classify.hpp"
#include "core/trigger_world_store.hpp"
#include "debug/assist_log.hpp"
#include "debug/lag_probe.hpp"
#include "debug/perf_trace.hpp"
#include "debug/plan_gold_corpus.hpp"
#include "dev/dev_config.hpp"
#include "ui/action_button_setting.hpp"
#include "ui/bake_path_visualizer.hpp"
#include "ui/in_level_settings_filter.hpp"
#include "ui/status_overlay.hpp"
#include "ui/trajectory_visualizer.hpp"
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
