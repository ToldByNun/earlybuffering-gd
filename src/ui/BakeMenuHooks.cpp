#include "../core/AssistController.hpp"
#include "../core/BakeMap.hpp"
#include "../core/BakeScheduler.hpp"
#include "../core/ConfigBundle.hpp"
#include "../core/ReplayImport.hpp"
#include "../debug/AssistLog.hpp"
#include "../config/StageConfig.hpp"
#include "../ui/TrajectoryVisualizer.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>

#ifdef GEODE_IS_WINDOWS
# ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
# endif
# include <windows.h>
# include <commdlg.h>
#endif

using namespace geode::prelude;


#ifdef GEODE_IS_WINDOWS
std::optional<std::filesystem::path> pickReplayFileWin32() {
    wchar_t fileBuf[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter =
        L"Replays (GDR/MHR/SLC)\0*.gdr;*.gdr2;*.mhr;*.json;*.slc\0"
        L"All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return std::nullopt;
    return std::filesystem::path(fileBuf);
}

std::optional<std::filesystem::path> pickSaveJsonWin32(bool saveAs) {
    wchar_t fileBuf[MAX_PATH] = {};
    wcscpy_s(fileBuf, L"frirecracker-config.json");
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"JSON\0*.json\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (saveAs ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (saveAs) {
        if (!GetSaveFileNameW(&ofn)) return std::nullopt;
    }
    else {
        if (!GetOpenFileNameW(&ofn)) return std::nullopt;
    }
    return std::filesystem::path(fileBuf);
}
#endif

CCLabelBMFont* findStatusLabel(PauseLayer* self) {
    return typeinfo_cast<CCLabelBMFont*>(self->getChildByID("frirecracker-bake-status"_spr));
}

CCLabelBMFont* findModeLabel(PauseLayer* self) {
    return typeinfo_cast<CCLabelBMFont*>(self->getChildByID("frirecracker-mode-label"_spr));
}

void refreshPauseLabels(PauseLayer* self) {
    if (!self) return;
    if (auto* mode = findModeLabel(self)) {
        auto m = StageConfig::assistMode();
        mode->setString(fmt::format(
            "Mode: {} — {}",
            StageConfig::assistModeName(m),
            StageConfig::assistModeHint(m)
        ).c_str());
    }
    if (auto* st = findStatusLabel(self)) {
        auto& bake = BakeMap::get();
        if (bake.isBaking() || BakeScheduler::get().running()) {
            st->setString(fmt::format(
                "{}\n{:.0f}%  ({}/{})",
                bake.progress().getStatus(),
                bake.progress().fraction() * 100.f,
                bake.progress().done.load(),
                bake.progress().total.load()
            ).c_str());
        }
        else {
            st->setString(bake.pauseStatusLine().c_str());
        }
    }
}

void handleImportedPath(std::filesystem::path const& path) {
    auto loaded = ReplayImport::load(path);
    if (loaded.isErr()) {
        AssistLog::info(fmt::format("Import failed: {}", loaded.unwrapErr()));
        FLAlertLayer::create("frirecracker", loaded.unwrapErr().c_str(), "OK")->show();
        return;
    }
    BakeMap::get().setPendingReplay(loaded.unwrap());
    AssistController::get().updateOverlay();
    auto const* r = BakeMap::get().pendingReplay();
    FLAlertLayer::create(
        "frirecracker",
        fmt::format(
            "Imported {}\n{} events @ {:.0f} tps ({})\n\n"
            "Press Bake Map for replay windows.\n"
            "Brute Bake ignores this and searches the level itself.",
            r->sourceName, r->events.size(), r->tps, r->format
        ).c_str(),
        "OK"
    )->show();
    if (auto* pause = CCScene::get()->getChildByType<PauseLayer>(0)) {
        refreshPauseLabels(pause);
    }
}

void doImportReplay() {
    if (BakeMap::get().isBaking() || BakeScheduler::get().running()) {
        FLAlertLayer::create("frirecracker", "Bake in progress — wait or cancel.", "OK")->show();
        return;
    }
#ifdef GEODE_IS_WINDOWS
    auto picked = pickReplayFileWin32();
    if (!picked) return;
    handleImportedPath(*picked);
#else
    FLAlertLayer::create(
        "frirecracker",
        "Replay import is Windows-only for now.",
        "OK"
    )->show();
#endif
}

void startBake(bool brute) {
    auto* pl = PlayLayer::get();
    if (!pl) {
        FLAlertLayer::create("frirecracker", "No level loaded.", "OK")->show();
        return;
    }
    if (BakeMap::get().isBaking() || BakeScheduler::get().running()) {
        FLAlertLayer::create("frirecracker", "Bake already running.", "OK")->show();
        return;
    }
    if (!brute && !BakeMap::get().pendingReplay()) {
        FLAlertLayer::create(
            "frirecracker",
            "Bake Map needs a replay.\nImport Replay first — or use Brute Bake\n(no macro, explores safe clicks).",
            "OK"
        )->show();
        return;
    }
    auto snap = AssistController::get().ensureStartSnapshot(pl);
    if (snap.isErr()) {
        FLAlertLayer::create(
            "frirecracker",
            "Reset the level once (play from start),\nthen pause and bake again.",
            "OK"
        )->show();
        return;
    }

    int slot = AssistController::get().startSnapshotSlot();
    AssistLog::info(fmt::format("BAKE UI: starting {} (main-thread)", brute ? "BruteForce" : "Replay"));

    bool ok = BakeScheduler::get().start(
        pl, slot, StageConfig::bakeRadius(),
        brute ? BakeMethod::BruteForce : BakeMethod::Replay
    );
    if (!ok) {
        FLAlertLayer::create("frirecracker", "Could not start bake.", "OK")->show();
        return;
    }
    AssistController::get().updateOverlay();
    if (auto* pause = CCScene::get()->getChildByType<PauseLayer>(0)) {
        refreshPauseLabels(pause);
    }
}

void doCycleMode() {
    StageConfig::cycleAssistMode();
    auto mode = StageConfig::assistMode();
    AssistLog::info(fmt::format(
        "ASSIST MODE: {} — {}",
        StageConfig::assistModeName(mode),
        StageConfig::assistModeHint(mode)
    ));
    AssistController::get().updateOverlay();
    if (auto* pause = CCScene::get()->getChildByType<PauseLayer>(0)) {
        refreshPauseLabels(pause);
    }
}

void doCancelBake() {
    if (!BakeMap::get().isBaking() && !BakeScheduler::get().running()) return;
    BakeScheduler::get().cancel();
    AssistLog::info("BAKE: cancel requested");
}

void doCyclePreset() {
    auto const stage = StageConfig::cycleNext();
    AssistLog::printCycle(stage, AssistController::get().estimatedTps());
    AssistController::get().updateOverlay();
}

void doToggleDebug() {
    StageConfig::toggleDebugLogging();
    AssistLog::info(fmt::format("debug-logging: {}", StageConfig::debugLogging() ? "ON" : "OFF"));
}

void doResetLearning() {
    AssistController::get().resetLevelProfile();
    AssistLog::info("LEARN: level profile reset");
}

void doToggleAutoTune() {
    StageConfig::toggleAutoTune();
    AssistLog::info(fmt::format("LEARN: smart learning {}", StageConfig::autoTune() ? "ON" : "OFF"));
    AssistController::get().updateOverlay();
}

void doToggleTrajectory() {
    StageConfig::toggleShowTrajectory();
    bool const on = StageConfig::showTrajectory();
    TrajectoryVisualizer::get().setEnabled(on);
    if (on) {
        if (auto* pl = PlayLayer::get()) TrajectoryVisualizer::get().attach(pl);
    }
    AssistLog::info(fmt::format("TRAJ: visualizer {}", on ? "ON" : "OFF"));
    AssistController::get().updateOverlay();
}

void doExportConfig() {
#ifdef GEODE_IS_WINDOWS
    auto path = pickSaveJsonWin32(true);
    if (!path) return;
    auto r = ConfigBundle::exportTo(*path);
    if (r.isErr()) FLAlertLayer::create("frirecracker", r.unwrapErr().c_str(), "OK")->show();
    else FLAlertLayer::create("frirecracker", "Config exported.", "OK")->show();
#else
    auto path = Mod::get()->getSaveDir() / "frirecracker-config.json";
    auto r = ConfigBundle::exportTo(path);
    FLAlertLayer::create(
        "frirecracker",
        r.isOk() ? fmt::format("Exported to {}", path.string()).c_str() : r.unwrapErr().c_str(),
        "OK"
    )->show();
#endif
}

void doImportConfig() {
#ifdef GEODE_IS_WINDOWS
    auto path = pickSaveJsonWin32(false);
    if (!path) return;
    auto r = ConfigBundle::importFrom(*path);
#else
    auto path = Mod::get()->getSaveDir() / "frirecracker-config.json";
    auto r = ConfigBundle::importFrom(path);
#endif
    if (r.isErr()) FLAlertLayer::create("frirecracker", r.unwrapErr().c_str(), "OK")->show();
    else {
        AssistController::get().updateOverlay();
        FLAlertLayer::create("frirecracker", "Config imported.", "OK")->show();
    }
}

CCMenuItemSpriteExtra* makeTextBtn(char const* text, float width, auto cb) {
    auto* btn = CCMenuItemExt::createSpriteExtra(
        ButtonSprite::create(text, static_cast<int>(width), true, "bigFont.fnt", "GJ_button_01.png", 28.f, 0.6f),
        std::move(cb)
    );
    btn->setScale(0.72f);
    return btn;
}


class $modify(FrirePauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        bool const showBake = StageConfig::showPauseBake();
        bool const showAssist = StageConfig::showPauseAssist();
        if (!showBake && !showAssist) return;

        if (this->getChildByID("frirecracker-bake-menu"_spr)
            || this->getChildByID("frirecracker-assist-menu"_spr)) {
            refreshPauseLabels(this);
            return;
        }

        auto win = this->getContentSize();

        if (showBake) {
            auto* title = CCLabelBMFont::create("frirecracker bake", "goldFont.fnt");
            if (!title) title = CCLabelBMFont::create("frirecracker bake", "bigFont.fnt");
            title->setID("frirecracker-bake-title"_spr);
            title->setScale(0.45f);
            title->setAnchorPoint({ 0.f, 1.f });
            title->setPosition({ 14.f, win.height - 52.f });
            this->addChild(title, 999);

            auto* modeLbl = CCLabelBMFont::create("Mode: …", "chatFont.fnt");
            if (!modeLbl) modeLbl = CCLabelBMFont::create("Mode: …", "bigFont.fnt");
            modeLbl->setID("frirecracker-mode-label"_spr);
            modeLbl->setScale(0.5f);
            modeLbl->setAnchorPoint({ 0.f, 1.f });
            modeLbl->setPosition({ 14.f, win.height - 72.f });
            this->addChild(modeLbl, 999);

            auto* status = CCLabelBMFont::create("…", "chatFont.fnt");
            if (!status) status = CCLabelBMFont::create("…", "bigFont.fnt");
            status->setID("frirecracker-bake-status"_spr);
            status->setScale(0.45f);
            status->setAnchorPoint({ 0.f, 1.f });
            status->setOpacity(210);
            status->setPosition({ 14.f, win.height - 92.f });
            this->addChild(status, 999);

            auto* col = CCMenu::create();
            col->setID("frirecracker-bake-menu"_spr);
            col->setContentSize({ 160.f, 220.f });
            col->ignoreAnchorPointForPosition(false);
            col->setAnchorPoint({ 0.f, 1.f });
            col->setPosition({ 14.f, win.height - 130.f });

            col->addChild(makeTextBtn("Cycle Mode", 140.f, [](CCMenuItemSpriteExtra*) { doCycleMode(); }));
            col->addChild(makeTextBtn("Import Replay", 140.f, [](CCMenuItemSpriteExtra*) { doImportReplay(); }));
            col->addChild(makeTextBtn("Bake Map", 140.f, [](CCMenuItemSpriteExtra*) { startBake(false); }));
            col->addChild(makeTextBtn("Brute Bake", 140.f, [](CCMenuItemSpriteExtra*) { startBake(true); }));
            col->addChild(makeTextBtn("Cancel", 140.f, [](CCMenuItemSpriteExtra*) { doCancelBake(); }));

            col->setLayout(
                ColumnLayout::create()
                    ->setGap(4.f)
                    ->setAxisAlignment(AxisAlignment::Start)
                    ->setCrossAxisAlignment(AxisAlignment::Start)
                    ->setAutoScale(false)
            );
            col->updateLayout();
            this->addChild(col, 1000);
        }

        if (showAssist) {
            auto* acol = CCMenu::create();
            acol->setID("frirecracker-assist-menu"_spr);
            acol->setContentSize({ 160.f, 280.f });
            acol->ignoreAnchorPointForPosition(false);
            acol->setAnchorPoint({ 1.f, 1.f });
            acol->setPosition({ win.width - 14.f, win.height - 52.f });

            acol->addChild(makeTextBtn("Cycle Preset", 140.f, [](CCMenuItemSpriteExtra*) { doCyclePreset(); }));
            acol->addChild(makeTextBtn("Toggle Debug", 140.f, [](CCMenuItemSpriteExtra*) { doToggleDebug(); }));
            acol->addChild(makeTextBtn("Reset Learn", 140.f, [](CCMenuItemSpriteExtra*) { doResetLearning(); }));
            acol->addChild(makeTextBtn("Toggle Learn", 140.f, [](CCMenuItemSpriteExtra*) { doToggleAutoTune(); }));
            acol->addChild(makeTextBtn("Trajectory", 140.f, [](CCMenuItemSpriteExtra*) { doToggleTrajectory(); }));
            acol->addChild(makeTextBtn("Export Cfg", 140.f, [](CCMenuItemSpriteExtra*) { doExportConfig(); }));
            acol->addChild(makeTextBtn("Import Cfg", 140.f, [](CCMenuItemSpriteExtra*) { doImportConfig(); }));

            acol->setLayout(
                ColumnLayout::create()
                    ->setGap(4.f)
                    ->setAxisAlignment(AxisAlignment::Start)
                    ->setCrossAxisAlignment(AxisAlignment::End)
                    ->setAutoScale(false)
            );
            acol->updateLayout();
            this->addChild(acol, 1000);
        }

        refreshPauseLabels(this);
        this->schedule(schedule_selector(FrirePauseLayer::onBakePoll), 0.15f);
    }

    void onBakePoll(float) {
        refreshPauseLabels(this);
        BakeScheduler::get().tickSlice();
    }
};
