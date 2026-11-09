#include "../../app/class_manager.hpp"
#include "save_feedback.hpp"
#include "../../config/stage_config.hpp"
#include "../../debug/assist_log.hpp"
#include "../../debug/perf_trace.hpp"
#include "save_frame_counter.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>

EbSaveFeedback& EbSaveFeedback::get() {
    return EbClassManager::get().service<EbSaveFeedback>();
}

void EbSaveFeedback::restoreFlash() {
    if (!this->m_flashing || !this->m_flashPlayer)
        return;
    if (this->m_flashPlayer->m_isDead)
        return;

    this->m_flashPlayer->setColor(this->m_savedColor1);
    this->m_flashPlayer->setSecondColor(this->m_savedColor2);
    if (this->m_hadGlow) {
        this->m_flashPlayer->m_glowColor = this->m_savedGlow;
    }
}

void EbSaveFeedback::reset() {
    if (this->m_flashing && this->m_flashPlayer && !this->m_flashPlayer->m_isDead) {
        this->m_flashPlayer->setColor(this->m_savedColor1);
        this->m_flashPlayer->setSecondColor(this->m_savedColor2);
        if (this->m_hadGlow) {
            this->m_flashPlayer->m_glowColor = this->m_savedGlow;
        }
    }
    this->m_flashing = false;
    this->m_flashPlayer = nullptr;
    this->m_flashTicksLeft = 0;
    EbSaveFrameCounter::get().clear();
}

std::string EbSaveFeedback::cueFileName(int delayTicks) {
    int const bucket = delayTicks >= 8 ? 8 : std::max(1, delayTicks);
    return fmt::format("{}-tick.mp3", bucket);
}

std::filesystem::path EbSaveFeedback::cuePathFor(std::string const& file) {
    auto* mod = geode::prelude::Mod::get();
    if (!mod)
        return {};

    auto const id = mod->getID();
    // Geode packs resources/cues/*.mp3 as resources/<mod-id>/<file>
    std::filesystem::path candidates[] = {
        mod->getResourcesDir() / file,
        mod->getResourcesDir() / id / file,
        mod->getResourcesDir() / "cues" / file,
        mod->getPackagePath().parent_path() / "resources" / id / file,
        mod->getPackagePath().parent_path() / "resources" / "cues" / file,
    };
    for (auto const& p : candidates) {
        std::error_code ec;
        if (p.empty())
            continue;
        if (!std::filesystem::exists(p, ec))
            continue;
        return p;
    }
    return {};
}

void EbSaveFeedback::playSound(int delayTicks) {
    if (delayTicks <= 0)
        return;

    auto* eng = FMODAudioEngine::sharedEngine();
    if (!eng) {
        EbAssistLog::info("save cue: no FMOD engine");
        return;
    }

    auto* mod = geode::prelude::Mod::get();
    if (!mod)
        return;

    auto const file = cueFileName(delayTicks);
    auto const path = cuePathFor(file);
    // Prefer Geode-resolved name (mod-id/file) — works with FMOD's relative lookup.
    auto const sprName = fmt::format("{}/{}", mod->getID(), file);

    if (!path.empty()) {
        eng->playEffect(path.generic_string());
        EbAssistLog::debug(fmt::format("save cue +{} → {}", delayTicks, path.generic_string()));
        return;
    }

    // Last resort: spr-style relative name even if we couldn't stat the file yet
    // (unzipped path can lag one frame after install).
    eng->playEffect(sprName);
    EbAssistLog::debug(fmt::format("save cue +{} via spr '{}' (resourcesDir={})", delayTicks, sprName,
                                 mod->getResourcesDir()));
}

void EbSaveFeedback::flashIcon(PlayerObject* player) {
    if (!player || player->m_isDead)
        return;

    // Restore any previous flash first so we don't lose the true base colors.
    if (this->m_flashing && this->m_flashPlayer && this->m_flashPlayer != player) {
        if (!this->m_flashPlayer->m_isDead) {
            this->m_flashPlayer->setColor(this->m_savedColor1);
            this->m_flashPlayer->setSecondColor(this->m_savedColor2);
            if (this->m_hadGlow) {
                this->m_flashPlayer->m_glowColor = this->m_savedGlow;
            }
        }
    }

    if (!this->m_flashing || this->m_flashPlayer != player) {
        this->m_savedColor1 = player->getColor();
        this->m_savedColor2 = player->getSecondColor();
        this->m_hadGlow = player->m_hasGlow;
        this->m_savedGlow = player->m_glowColor;
    }

    auto const rgb = EbStageConfig::saveFeedbackColor();
    cocos2d::ccColor3B const flash{rgb.r, rgb.g, rgb.b};
    player->setColor(flash);
    player->setSecondColor(flash);
    if (player->m_hasGlow) {
        player->m_glowColor = flash;
    }

    this->m_flashPlayer = player;
    this->m_flashing = true;
    // ~0.2s at 240tps
    this->m_flashTicksLeft = 48;
}

void EbSaveFeedback::onSave(PlayLayer* playLayer, PlayerObject* player, int delayTicks) {
    EbPerfTrace::Scope const scope("onSave");
    if (delayTicks <= 0)
        return;

    if (EbStageConfig::saveFeedbackSound()) {
        EbPerfTrace::Scope const soundScope("saveSound");
        this->playSound(delayTicks);
    }
    if (EbStageConfig::saveFeedbackIcon() && player) {
        EbPerfTrace::Scope const iconScope("saveIcon");
        this->flashIcon(player);
    }
    if (!EbStageConfig::saveFeedbackCounter())
        return;
    if (!playLayer || !player)
        return;

    EbPerfTrace::Scope const counterScope("saveCounter");
    auto& counter = EbSaveFrameCounter::get();
    counter.attach(playLayer);
    counter.spawn(playLayer, player->getPosition(), delayTicks);
}

void EbSaveFeedback::onPhysicsTick() {
    if (!this->m_flashing)
        return;
    if (--this->m_flashTicksLeft > 0)
        return;

    this->restoreFlash();
    this->m_flashing = false;
    this->m_flashPlayer = nullptr;
    this->m_flashTicksLeft = 0;
}
