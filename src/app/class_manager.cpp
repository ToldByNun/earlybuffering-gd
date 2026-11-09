#include "class_manager.hpp"
#include "class_registry.hpp"
#include "level_lifecycle.hpp"
#include "module_bindings.hpp"
#include "../core/bake/bake_scheduler.hpp"

#include "../config/stage_config.hpp"
#include "../core/assist/assist_controller.hpp"
#include "../core/assist/contracts/assist_contracts140.hpp"
#include "../core/bake/bake_map.hpp"
#include "../core/platform/save_drop_paths.hpp"
#include "../core/sim/shadow_simulator.hpp"
#include "../core/trigger_world/trigger_classify.hpp"
#include "../core/trigger_world/trigger_world_store.hpp"
#include "../debug/assist_log.hpp"
#include "../debug/lag_probe.hpp"
#include "../debug/perf_trace.hpp"
#include "../debug/plan_gold_corpus.hpp"
#include "../dev/dev_config.hpp"
#include "../ui/overlays/status_overlay.hpp"
#include "../ui/settings/action_button_setting.hpp"
#include "../ui/settings/in_level_settings_filter.hpp"
#include "../ui/visualizers/bake_path_visualizer.hpp"
#include "../ui/visualizers/trajectory_visualizer.hpp"
#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>

EbClassManager::EbClassManager() : m_services(&EbClassManager::prepareService) {}

EbClassManager& EbClassManager::get() {
    static EbClassManager manager;
    return manager;
}

void EbClassManager::initialize() {
    if (m_initialized)
        return;

    // Custom eb-action rows (Bake/Assist buttons) — must resolve before settings UI opens.
    EbActionButtonSettingImplementation::registerActionButtonSettingType();
    // Tabs for every EarlyBuffer settings open (Geode menu + pause EB).
    EbInLevelSettingsFilterKeeper::startWatchdog();

    EbStageConfig::refreshCache();
    EbDevConfig::refreshCache();
    this->syncDiagnostics();
    // Mobile drop folders (replay-drop / config) — soft-fail if save dir is read-only.
    {
        std::string dropErr;
        if (!EbSaveDropPaths::get().ensureDirs(dropErr) && !dropErr.empty()) {
            EbAssistLog::info(fmt::format("WARN SaveDropPaths: {}", dropErr));
        }
    }
    EbAssistLog::printBanner();
    // Commit 2 LOCK: freeze GhostParityProbe ε before any runtime / PlanGold probe.
    EbAssistContracts140::applyLockedEps();
    EbAssistLog::info(fmt::format("AssistContracts140 LOCK schema={} corpus={} eps=({:.0f},{:.0f})",
                                EbAssistContracts140::kSchema, EbAssistContracts140::kCorpusLabel,
                                EbAssistContracts140::kLockedEpsX, EbAssistContracts140::kLockedEpsY));
    if (EbStageConfig::perfTrace()) {
        EbAssistLog::info(fmt::format("PerfTrace logging to {}", EbPerfTrace::logPath()));
    }
    if (EbStageConfig::lagProbe()) {
        EbAssistLog::info(fmt::format("LagProbe logging to {}", EbLagProbe::logPath()));
    }


    EbAssistLog::info(fmt::format("Class registry: {} classes / {} managed services",
        EbClassRegistry::all().size(), EbClassRegistry::serviceCount()));
    runStartupChecks();
    EbModuleBindings::registerSettings();
    m_initialized = true;
}

void EbClassManager::syncDiagnostics() {
    service<EbPerfTrace>().setEnabled(EbStageConfig::perfTrace());
    service<EbLagProbe>().setEnabled(EbStageConfig::lagProbe());
}

void EbClassManager::refreshSettings() {

    if (EbStageConfig::isApplying())
        return;
    EbStageConfig::refreshCache();
    EbDevConfig::refreshCache();
    this->syncDiagnostics();
    EbAssistController::get().updateOverlay();

}

void EbClassManager::runStartupChecks() {
    {
        std::string report;
        bool const ok = EbShadowSimulator::regressionSelfCheck(report);
        if (ok)
            EbAssistLog::info(report);
        else
            EbAssistLog::info(fmt::format("WARN {}", report));
    }
    {
        std::string report;
        bool const ok = EbTriggerClassify::selfCheck(report);
        if (ok)
            EbAssistLog::info(report);
        else
            EbAssistLog::info(fmt::format("WARN {}", report));
    }
    {
        std::string report;
        bool const ok = EbTriggerWorldStore::selfCheck(report);
        if (ok)
            EbAssistLog::info(report);
        else
            EbAssistLog::info(fmt::format("WARN {}", report));
    }
    {
        std::string report;
        bool const ok = EbPlanGoldCorpus::run(report);
        if (ok)
            EbAssistLog::info(report);
        else
            EbAssistLog::info(fmt::format("WARN {}", report));
    }


}

void EbClassManager::onLevelInit(PlayLayer* layer) {
    if (!layer || m_level == layer || m_quitting)
        return;
    m_level = layer;
    m_closedLevel = nullptr;
    EbLevelLifecycle::onLevelInit(service<EbAssistController>(), layer);
}

void EbClassManager::resetAttempt(PlayLayer* layer) {
    if (m_quitting)
        return;
    EbLevelLifecycle::resetAttempt(service<EbAssistController>(), layer);
}

void EbClassManager::beforeLevelReset(PlayLayer* layer) {
    if (!layer || m_quitting)
        return;
    {
        EbLagProbe::Scope const scope("eb.bakeCancel");
        service<EbBakeScheduler>().cancel();
    }
    service<EbShadowSimulator>().evacuateGhosts(layer);
    resetAttempt(layer);
}

void EbClassManager::onCheckpointLoad(PlayLayer* layer) {
    if (!layer || m_quitting)
        return;
    {
        EbLagProbe::Scope const scope("eb.bakeCancel");
        service<EbBakeScheduler>().cancel();
    }
    service<EbShadowSimulator>().evacuateGhosts(layer);
    EbLagProbe::Scope const scope("eb.onCheckpointLoad");
    EbLevelLifecycle::onCheckpointLoad(service<EbAssistController>(), layer);
}

void EbClassManager::onLevelComplete(PlayLayer* layer) {
    if (!layer || m_quitting)
        return;
    EbLevelLifecycle::onLevelComplete(service<EbAssistController>(), layer);
}

void EbClassManager::onLevelQuit(PlayLayer* layer) {
    if (m_quitting || (layer && layer == m_closedLevel))
        return;
    if (m_level && layer != m_level)
        return;
    if (!layer && !m_level)
        return;
    m_quitting = true;
    try {
        EbLevelLifecycle::onLevelQuit(service<EbAssistController>(), layer);
    } catch (...) {
        m_quitting = false;
        throw;
    }
    m_closedLevel = layer;
    m_level = nullptr;
    m_quitting = false;
}
