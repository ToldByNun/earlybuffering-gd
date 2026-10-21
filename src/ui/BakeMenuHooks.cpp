#include "../core/AssistController.hpp"
#include "../core/BakeMap.hpp"
#include "../core/ReplayImport.hpp"
#include "../debug/AssistLog.hpp"
#include "../config/StageConfig.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/utils/cocos.hpp>

#ifdef GEODE_IS_WINDOWS
# ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
# endif
# include <windows.h>
# include <commdlg.h>
#endif

using namespace geode::prelude;
using namespace frire;

namespace {

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
#endif

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
            "Loaded {}\n{} events @ {:.0f} tps ({})\nThen press Bake Map.",
            r->sourceName, r->events.size(), r->tps, r->format
        ).c_str(),
        "OK"
    )->show();
}

void doImportReplay() {
#ifdef GEODE_IS_WINDOWS
    auto picked = pickReplayFileWin32();
    if (!picked) return;
    handleImportedPath(*picked);
#else
    FLAlertLayer::create(
        "frirecracker",
        "Replay import UI is Windows-only for now. Place a bake JSON under the mod save bakes/ folder.",
        "OK"
    )->show();
#endif
}

void doBakeMap() {
    auto* pl = PlayLayer::get();
    if (!pl) {
        FLAlertLayer::create("frirecracker", "No level loaded.", "OK")->show();
        return;
    }
    if (!BakeMap::get().pendingReplay()) {
        FLAlertLayer::create(
            "frirecracker",
            "Import a replay first (Import Replay).",
            "OK"
        )->show();
        return;
    }

    auto res = AssistController::get().runBakeMap(pl);
    AssistController::get().updateOverlay();
    if (res.isErr()) {
        FLAlertLayer::create("frirecracker", res.unwrapErr().c_str(), "OK")->show();
        return;
    }
    FLAlertLayer::create(
        "frirecracker",
        fmt::format(
            "Baked {} click windows.\nAssist mode: {}",
            res.unwrap(),
            StageConfig::assistModeName(StageConfig::assistMode())
        ).c_str(),
        "OK"
    )->show();
}

}

class $modify(FrirePauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        if (this->getChildByID("frirecracker-bake-menu"_spr)) return;

        auto* col = CCMenu::create();
        col->setID("frirecracker-bake-menu"_spr);

        auto makeLabelBtn = [](char const* text, auto cb) {
            auto* label = CCLabelBMFont::create(text, "bigFont.fnt");
            label->setScale(0.32f);
            return CCMenuItemExt::createSpriteExtra(label, std::move(cb));
        };

        col->addChild(makeLabelBtn("Import Replay", [](CCMenuItemSpriteExtra*) {
            doImportReplay();
        }));
        col->addChild(makeLabelBtn("Bake Map", [](CCMenuItemSpriteExtra*) {
            doBakeMap();
        }));

        col->setLayout(
            ColumnLayout::create()
                ->setGap(6.f)
                ->setAxisAlignment(AxisAlignment::Start)
                ->setCrossAxisAlignment(AxisAlignment::Center)
        );
        auto win = this->getContentSize();
        col->setPosition({ win.width - 72.f, win.height - 90.f });
        col->updateLayout();
        this->addChild(col, 999);
    }
};
