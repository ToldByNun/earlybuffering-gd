#include "SaveFeedback.hpp"
#include "SaveFrameCounter.hpp"
#include "../config/StageConfig.hpp"
#include "../debug/AssistLog.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>

using namespace geode::prelude;

SaveFeedback& SaveFeedback::get() {
    static SaveFeedback inst;
    return inst;
}

void SaveFeedback::restoreFlash() {
    if (!this->m_flashing || !this->m_flashPlayer) return;
    if (this->m_flashPlayer->m_isDead) return;

    this->m_flashPlayer->setColor(this->m_savedColor1);
    this->m_flashPlayer->setSecondColor(this->m_savedColor2);
    if (this->m_hadGlow) {
        this->m_flashPlayer->m_glowColor = this->m_savedGlow;
    }
}

void SaveFeedback::reset() {
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
    SaveFrameCounter::get().clear();
}

std::string SaveFeedback::cueFileName(int delayTicks) {
    int const bucket = delayTicks >= 8 ? 8 : std::max(1, delayTicks);
    return fmt::format("{}-tick.mp3", bucket);
}

std::filesystem::path SaveFeedback::cuePathFor(std::string const& file) {
    auto* mod = Mod::get();
    if (!mod) return {};

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
        if (p.empty()) continue;
        if (!std::filesystem::exists(p, ec)) continue;
        return p;
    }
    return {};
}

void SaveFeedback::playSound(int delayTicks) {
    if (delayTicks <= 0) return;

    auto* eng = FMODAudioEngine::sharedEngine();
    if (!eng) {
        AssistLog::info("save cue: no FMOD engine");
        return;
    }

    auto* mod = Mod::get();
    if (!mod) return;

    auto const file = cueFileName(delayTicks);
    auto const path = cuePathFor(file);
    // Prefer Geode-resolved name (mod-id/file) — works with FMOD's relative lookup.
    auto const sprName = fmt::format("{}/{}", mod->getID(), file);

    if (!path.empty()) {
        eng->playEffect(path.generic_string());
        AssistLog::info(fmt::format("save cue +{} → {}", delayTicks, path.generic_string()));
        return;
    }

    // Last resort: spr-style relative name even if we couldn't stat the file yet
    // (unzipped path can lag one frame after install).
    eng->playEffect(sprName);
    AssistLog::info(fmt::format(
        "save cue +{} via spr '{}' (resourcesDir={})",
        delayTicks, sprName, mod->getResourcesDir()
    ));
}

void SaveFeedback::flashIcon(PlayerObject* player) {
    if (!player || player->m_isDead) return;

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

    auto const rgb = StageConfig::saveFeedbackColor();
    cocos2d::ccColor3B const flash{ rgb.r, rgb.g, rgb.b };
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

void SaveFeedback::onSave(PlayLayer* playLayer, PlayerObject* player, int delayTicks) {
    if (delayTicks <= 0) return;

    if (StageConfig::saveFeedbackSound()) {
        this->playSound(delayTicks);
    }
    if (StageConfig::saveFeedbackIcon() && player) {
        this->flashIcon(player);
    }
    if (!StageConfig::saveFeedbackCounter()) return;
    if (!playLayer || !player) return;

    auto& counter = SaveFrameCounter::get();
    counter.attach(playLayer);
    counter.spawn(playLayer, player->getPosition(), delayTicks);
}

void SaveFeedback::onPhysicsTick() {
    if (!this->m_flashing) return;
    if (--this->m_flashTicksLeft > 0) return;

    this->restoreFlash();
    this->m_flashing = false;
    this->m_flashPlayer = nullptr;
    this->m_flashTicksLeft = 0;
}
