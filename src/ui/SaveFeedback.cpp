#include "SaveFeedback.hpp"
#include "../config/StageConfig.hpp"
#include "../debug/AssistLog.hpp"

#include <Geode/Geode.hpp>
#include <filesystem>
#include <string>

using namespace geode::prelude;

SaveFeedback& SaveFeedback::get() {
    static SaveFeedback inst;
    return inst;
}

void SaveFeedback::reset() {
    if (m_flashing && m_flashPlayer) {
        m_flashPlayer->setColor(m_savedColor1);
        m_flashPlayer->setSecondColor(m_savedColor2);
        if (m_hadGlow) {
            m_flashPlayer->m_glowColor = m_savedGlow;
        }
    }
    m_flashing = false;
    m_flashPlayer = nullptr;
    m_flashTicksLeft = 0;
}

static std::string cueFileName(int delayTicks) {
    int const bucket = delayTicks >= 8 ? 8 : std::max(1, delayTicks);
    return fmt::format("{}-tick.mp3", bucket);
}

static std::filesystem::path cuePathFor(std::string const& file) {
    auto* mod = Mod::get();
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
        if (!p.empty() && std::filesystem::exists(p, ec)) return p;
    }
    return {};
}

void SaveFeedback::playSound(int delayTicks) {
    if (delayTicks <= 0) return;

    auto const file = cueFileName(delayTicks);
    auto path = cuePathFor(file);

    auto* eng = FMODAudioEngine::sharedEngine();
    if (!eng) {
        AssistLog::info("save cue: no FMOD engine");
        return;
    }

    // Prefer Geode-resolved name (mod-id/file) — works with FMOD's relative lookup.
    auto const sprName = fmt::format("{}/{}", Mod::get()->getID(), file);
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
        delayTicks, sprName, Mod::get()->getResourcesDir()
    ));
}

void SaveFeedback::flashIcon(PlayerObject* player) {
    if (!player || player->m_isDead) return;

    // Restore any previous flash first so we don't lose the true base colors.
    if (m_flashing && m_flashPlayer && m_flashPlayer != player) {
        m_flashPlayer->setColor(m_savedColor1);
        m_flashPlayer->setSecondColor(m_savedColor2);
        if (m_hadGlow) m_flashPlayer->m_glowColor = m_savedGlow;
    }

    if (!m_flashing || m_flashPlayer != player) {
        m_savedColor1 = player->getColor();
        m_savedColor2 = player->getSecondColor();
        m_hadGlow = player->m_hasGlow;
        m_savedGlow = player->m_glowColor;
    }

    auto const rgb = StageConfig::saveFeedbackColor();
    cocos2d::ccColor3B const flash{ rgb.r, rgb.g, rgb.b };
    player->setColor(flash);
    player->setSecondColor(flash);
    if (player->m_hasGlow) {
        player->m_glowColor = flash;
    }

    m_flashPlayer = player;
    m_flashing = true;
    // ~0.2s at 240tps
    m_flashTicksLeft = 48;
}

void SaveFeedback::onSave(PlayLayer* playLayer, PlayerObject* player, int delayTicks) {
    (void)playLayer;
    if (delayTicks <= 0) return;

    if (StageConfig::saveFeedbackSound()) {
        playSound(delayTicks);
    }
    if (StageConfig::saveFeedbackIcon() && player) {
        flashIcon(player);
    }
}

void SaveFeedback::onPhysicsTick() {
    if (!m_flashing) return;
    if (--m_flashTicksLeft > 0) return;

    if (m_flashPlayer && !m_flashPlayer->m_isDead) {
        m_flashPlayer->setColor(m_savedColor1);
        m_flashPlayer->setSecondColor(m_savedColor2);
        if (m_hadGlow) {
            m_flashPlayer->m_glowColor = m_savedGlow;
        }
    }
    m_flashing = false;
    m_flashPlayer = nullptr;
    m_flashTicksLeft = 0;
}
